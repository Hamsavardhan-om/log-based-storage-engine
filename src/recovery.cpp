#include "engine/recovery.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <cstring>

namespace engine
{

namespace
{

// Software IEEE 802.3 CRC32 implementation matching wal.cpp
uint32_t SoftwareCRC32(const uint8_t* data, size_t length, uint32_t previous_crc = 0)
{
    uint32_t crc = ~previous_crc;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; ++j)
        {
            crc = (crc >> 1) ^ (0xEDB88320u * (crc & 1));
        }
    }
    return ~crc;
}

} // namespace

RecoveryEngine::RecoveryEngine(const std::string& wal_path)
    : wal_path_(wal_path)
{
}

uint32_t RecoveryEngine::ComputeCRC32(OpType op, const std::string& key, const std::string& value) const
{
    uint8_t op_byte = static_cast<uint8_t>(op);
    uint32_t crc = SoftwareCRC32(&op_byte, sizeof(op_byte), 0);

    const uint32_t klen = static_cast<uint32_t>(key.size());
    const uint32_t vlen = static_cast<uint32_t>(value.size());

    crc = SoftwareCRC32(reinterpret_cast<const uint8_t*>(&klen), sizeof(klen), crc);
    crc = SoftwareCRC32(reinterpret_cast<const uint8_t*>(&vlen), sizeof(vlen), crc);

    if (!key.empty())
    {
        crc = SoftwareCRC32(reinterpret_cast<const uint8_t*>(key.data()), key.size(), crc);
    }
    if (!value.empty())
    {
        crc = SoftwareCRC32(reinterpret_cast<const uint8_t*>(value.data()), value.size(), crc);
    }

    return crc;
}

RecoveryStats RecoveryEngine::Recover(SkipList& skiplist)
{
    RecoveryStats stats;

    // 1. Open wal.log with O_RDWR so ftruncate() can modify the file if torn tails are present
    int fd = ::open(wal_path_.c_str(), O_RDWR);
    if (fd < 0)
    {
        // Fresh boot with no previous WAL file
        return stats;
    }

    uint64_t current_offset = 0;

    while (true)
    {
        WalHeader header;

        // 2. Read the fixed 16-byte header
        ssize_t h_read = ::read(fd, &header, sizeof(WalHeader));
        if (h_read == 0)
        {
            // Clean EOF reached
            break;
        }

        if (h_read < static_cast<ssize_t>(sizeof(WalHeader)))
        {
            // Partial header read: crashed during header append
            stats.torn_tail_detected = true;
            break;
        }

        // 3. Read raw key payload
        std::string key(header.key_len, '\0');
        if (header.key_len > 0)
        {
            ssize_t k_read = ::read(fd, key.data(), header.key_len);
            if (k_read != static_cast<ssize_t>(header.key_len))
            {
                stats.torn_tail_detected = true;
                break;
            }
        }

        // 4. Read raw value payload
        std::string val(header.val_len, '\0');
        if (header.val_len > 0)
        {
            ssize_t v_read = ::read(fd, val.data(), header.val_len);
            if (v_read != static_cast<ssize_t>(header.val_len))
            {
                stats.torn_tail_detected = true;
                break;
            }
        }

        // 5. Verify payload integrity via CRC32
        OpType op = static_cast<OpType>(header.op_type);
        uint32_t computed_crc = ComputeCRC32(op, key, val);

        if (computed_crc != header.crc32)
        {
            // Checksum mismatch: bit corruption or severed record body
            stats.torn_tail_detected = true;
            break;
        }

        // 6. Replay valid mutations into the in-memory index
        if (op == OpType::kPut)
        {
            skiplist.Put(key, val);
        }
        else if (op == OpType::kDelete)
        {
            // Tombstone marker for deleted records
            skiplist.Put(key, "");
        }

        // Move the clean file offset marker forward
        current_offset += sizeof(WalHeader) + header.key_len + header.val_len;
        stats.records_replayed++;
    }

    stats.last_valid_offset = current_offset;

    // 7. Prune damaged bytes if an interrupted append was found
    if (stats.torn_tail_detected)
    {
        ::ftruncate(fd, static_cast<off_t>(stats.last_valid_offset));
        ::fdatasync(fd);
    }

    ::close(fd);
    return stats;
}

}