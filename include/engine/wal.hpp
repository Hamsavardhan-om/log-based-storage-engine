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

#pragma pack(push, 1)
struct WalHeader
{
    uint32_t crc32{0};
    uint8_t op_type{0};
    uint8_t reserved[3]{0, 0, 0};
    uint32_t key_len{0};
    uint32_t val_len{0};
};
#pragma pack(pop)

static_assert(sizeof(WalHeader) == 16, "WalHeader must be exactly 16 bytes");

class WalWriter
{
public:
    explicit WalWriter(const std::string& path);
    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;
    WalWriter(WalWriter&&) = delete;
    WalWriter& operator=(WalWriter&&) = delete;

    // Binary frames, writes, and syncs record to persistent storage
    bool AppendRecord(OpType op, std::string_view key, std::string_view value = "");

    // Explicit disk sync hook via POSIX fdatasync
    bool Sync();

    // Inspector returning current physical file size
    [[nodiscard]] uint64_t FileSize() const;

private:
    uint32_t ComputeCRC32(OpType op, std::string_view key, std::string_view value) const;

    int fd_{-1};
    std::string path_;
};

} // namespace engine