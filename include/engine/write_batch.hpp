#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

enum class BatchOpType : uint8_t
{
    kPut = 0x01,
    kDelete = 0x02
};

struct BatchEntry
{
    BatchOpType op;
    std::string key;
    std::string value;
};

class WriteBatch
{
public:
    WriteBatch() = default;
    ~WriteBatch() = default;

    void Put(std::string_view key, std::string_view value)
    {
        entries_.push_back(BatchEntry{BatchOpType::kPut, std::string(key), std::string(value)});
    }

    void Delete(std::string_view key)
    {
        entries_.push_back(BatchEntry{BatchOpType::kDelete, std::string(key), ""});
    }

    [[nodiscard]] const std::vector<BatchEntry>& Entries() const noexcept
    {
        return entries_;
    }

    void Clear() noexcept
    {
        entries_.clear();
    }

    [[nodiscard]] size_t Size() const noexcept
    {
        return entries_.size();
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return entries_.empty();
    }

private:
    std::vector<BatchEntry> entries_;
};

} // namespace engine
