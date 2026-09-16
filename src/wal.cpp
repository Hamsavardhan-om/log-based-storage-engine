#include "engine/wal.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace engine
{

// we declare anonymous namespace because in cpp functions declared within this namespace have internal linkage ie., they are stirctly private to this unit wal.cpp and cannot clash with other functions during linking. 
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

} // anonymous namespace close

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
        Sync();
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

bool WalWriter::FlushBuffer()
{
    if (fd_ < 0)
    {
        return false;
    }

    if (write_buffer_.empty())
    {
        return true;
    }

    ssize_t written = ::write(fd_, write_buffer_.data(), write_buffer_.size());
    if (written != static_cast<ssize_t>(write_buffer_.size()))
    {
        return false;
    }

    write_buffer_.clear();
    return true;
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
    const size_t old_size = write_buffer_.size();
    write_buffer_.resize(old_size + total_bytes);

    uint8_t* dest = write_buffer_.data() + old_size;

    // 1. Pack 16-byte fixed header
    std::memcpy(dest, &header, sizeof(WalHeader));

    // 2. Pack raw payloads
    size_t offset = sizeof(WalHeader);
    if (!key.empty())
    {
        std::memcpy(dest + offset, key.data(), key.size());
        offset += key.size();
    }
    if (!value.empty())
    {
        std::memcpy(dest + offset, value.data(), value.size());
    }

    // Auto-flush when buffer reaches configured threshold (64 KB)
    if (write_buffer_.size() >= kDefaultFlushThresholdBytes)
    {
        return Sync();
    }

    return true;
}

bool WalWriter::AppendBatch(const std::vector<std::pair<OpType, std::pair<std::string_view, std::string_view>>>& records, bool sync)
{
    if (fd_ < 0)
    {
        return false;
    }

    for (const auto& record : records)
    {
        WalHeader header;
        header.op_type = static_cast<uint8_t>(record.first);
        header.key_len = static_cast<uint32_t>(record.second.first.size());
        header.val_len = static_cast<uint32_t>(record.second.second.size());
        header.crc32 = ComputeCRC32(record.first, record.second.first, record.second.second);

        const size_t total_bytes = sizeof(WalHeader) + record.second.first.size() + record.second.second.size();
        const size_t old_size = write_buffer_.size();
        write_buffer_.resize(old_size + total_bytes);

        uint8_t* dest = write_buffer_.data() + old_size;
        std::memcpy(dest, &header, sizeof(WalHeader));

        size_t offset = sizeof(WalHeader);
        if (!record.second.first.empty())
        {
            std::memcpy(dest + offset, record.second.first.data(), record.second.first.size());
            offset += record.second.first.size();
        }
        if (!record.second.second.empty())
        {
            std::memcpy(dest + offset, record.second.second.data(), record.second.second.size());
        }
    }

    if (sync || write_buffer_.size() >= kDefaultFlushThresholdBytes)
    {
        return Sync();
    }

    return true;
}

bool WalWriter::Flush()
{
    return FlushBuffer();
}

bool WalWriter::Sync()
{
    if (fd_ < 0)
    {
        return false;
    }

    if (!FlushBuffer())
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
        // Include unflushed bytes sitting in memory buffer
        return static_cast<uint64_t>(st.st_size) + write_buffer_.size();
    }
    return 0;
}

} // namespace engine