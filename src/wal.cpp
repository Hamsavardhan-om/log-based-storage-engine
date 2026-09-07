#include "engine/wal.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace engine
{

namespace
{

// Standard software CRC32 fallback (IEEE 802.3 polynomial: 0xEDB88320)
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

WalWriter::WalWriter(const std::string& path)
    : path_(path)
{
    // POSIX append-only binary descriptor
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0)
    {
        throw std::runtime_error("Failed to open or create WAL file at: " + path_);
    }
}

WalWriter::~WalWriter()
{
    if (fd_ >= 0)
    {
        ::fdatasync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

uint32_t WalWriter::ComputeCRC32(OpType op, std::string_view key, std::string_view value) const
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

bool WalWriter::AppendRecord(OpType op, std::string_view key, std::string_view value)
{
    if (fd_ < 0)
    {
        return false;
    }

    WalHeader header;
    header.op_type = static_cast<uint8_t>(op);
    header.key_len = static_cast<uint32_t>(key.size());
    header.val_len = static_cast<uint32_t>(value.size());
    header.crc32 = ComputeCRC32(op, key, value);

    const size_t total_bytes = sizeof(WalHeader) + key.size() + value.size();
    std::vector<uint8_t> buffer(total_bytes);

    // 1. Pack 16-byte fixed header
    std::memcpy(buffer.data(), &header, sizeof(WalHeader));

    // 2. Pack raw payloads
    size_t offset = sizeof(WalHeader);
    if (!key.empty())
    {
        std::memcpy(buffer.data() + offset, key.data(), key.size());
        offset += key.size();
    }
    if (!value.empty())
    {
        std::memcpy(buffer.data() + offset, value.data(), value.size());
    }

    // 3. Write contiguous frame buffer to disk
    ssize_t written = ::write(fd_, buffer.data(), total_bytes);
    if (written != static_cast<ssize_t>(total_bytes))
    {
        return false;
    }

    // 4. Force kernel page cache flush to flash storage
    return Sync();
}

bool WalWriter::Sync()
{
    if (fd_ < 0)
    {
        return false;
    }
    return ::fdatasync(fd_) == 0;
}

uint64_t WalWriter::FileSize() const
{
    struct stat st{};
    if (::fstat(fd_, &st) == 0)
    {
        return static_cast<uint64_t>(st.st_size);
    }
    return 0;
}

} // namespace engine