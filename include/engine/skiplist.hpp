#pragma once

#include "engine/region_allocator.hpp"

#include <cstdint>
#include <string_view>
#include <optional>
#include <shared_mutex>
#include <random>

namespace engine
{

class SkipList
{
public:
    // 16 levels balances up to ~65,536 elements optimally with p = 0.5
    static constexpr uint8_t kMaxHeight = 16;
    static constexpr float kBranchingProbability = 0.5f;

    explicit SkipList(RegionAllocator& allocator);
    ~SkipList() = default;

    // Non-copyable and non-movable to preserve raw memory pointer stability
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    SkipList(SkipList&&) = delete;
    SkipList& operator=(SkipList&&) = delete;

    // Core point operations
    void Put(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string_view> Get(std::string_view key) const;

    [[nodiscard]] uint8_t CurrentHeight() const noexcept;

private:
    struct Node
    {
        std::string_view key;
        std::string_view value;
        uint8_t height;

        // Flexible array tail for multi-level forward pointers
        Node* forward[1];

        static size_t AllocationSize(uint8_t height)
        {
            return sizeof(Node) + (sizeof(Node*) * (height - 1));
        }
    };

    Node* CreateNode(std::string_view key, std::string_view value, uint8_t height);
    uint8_t GenerateRandomHeight();

    RegionAllocator& allocator_;
    Node* head_{nullptr};
    uint8_t current_height_{1};

    // Shared reader-writer latch: concurrent Get() reads, serialized Put() writes
    mutable std::shared_mutex rw_lock_;

    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> distribution_{0.0f, 1.0f};
};

} // namespace engine