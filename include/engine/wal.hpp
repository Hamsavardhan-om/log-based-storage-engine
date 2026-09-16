#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

enum class OpType : uint8_t
{
    kPut = 0x01,
    kDelete = 0x02
};

// The following code acts as a fixed size (16 bytes) envelope for every record committed to the disk. #pragma pack(push, 1) alters the compiler behavior, by disabling the default padding done by it, packs the sturct members into one byte alignment. CRC32 checksum ensures that half-baked writes don't happen. optype tells the type of operation whether it was PUT or DELETE.
#pragma pack(push, 1)
struct WalHeader
{
    uint32_t crc32{0}; // 4 bytes
    uint8_t op_type{0}; // 1 byte
    uint8_t reserved[3]{0, 0, 0}; // 3 bytes for compensation
    uint32_t key_len{0}; // 4 bytes
    uint32_t val_len{0}; // 4 bytes 
};
#pragma pack(pop)

// Quite literally stops the compiler and returns an error if the allocation isn't 16 bytes.
static_assert(sizeof(WalHeader) == 16, "WalHeader must be exactly 16 bytes");

// Sole job of this class is to contain the skeleton of the write ahead log, defining methods like AppendRecord(), ComputeCRC() and FileSize()
class WalWriter
{
public:
    explicit WalWriter(const std::string& path);
    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;
    WalWriter(WalWriter&&) = delete;
    WalWriter& operator=(WalWriter&&) = delete;

    bool AppendRecord(OpType op, std::string_view key, std::string_view value = "");

    // Commits an entire batch of records to the buffer and syncs to disk
    bool AppendBatch(const std::vector<std::pair<OpType, std::pair<std::string_view, std::string_view>>>& records, bool sync = true);

    // Flushes in-memory buffer to kernel cache and forces physical drive sync via fdatasync()
    bool Sync();

    // Flushes in-memory buffer to kernel cache without calling fdatasync()
    bool Flush();

    // Inspector returning total logical file size (on-disk bytes + unflushed buffer bytes)
    [[nodiscard]] uint64_t FileSize() const;

    // Direct access to unflushed buffer size for metrics/monitoring
    [[nodiscard]] size_t UnflushedBytes() const noexcept { return write_buffer_.size(); }

private:
    uint32_t ComputeCRC32(OpType op, std::string_view key, std::string_view value) const;
    bool FlushBuffer();

    int fd_{-1};
    std::string path_;

    // In-memory write buffer to batch disk I/O and reduce fdatasync overhead
    std::vector<uint8_t> write_buffer_;
    static constexpr size_t kDefaultFlushThresholdBytes = 64 * 1024; // 64 KB buffer threshold
};

}