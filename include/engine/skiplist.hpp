#pragma once

#include "engine/region_allocator.hpp"

#include <cstdint> // Contains uint8_t special type
#include <string_view>
#include <optional>
#include <shared_mutex> // This is to imported to ensure multiple writes don't happen simultaneously. ie., PUT operations gets X Lock
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

    // Non-copyable and non-movable to preserve raw memory pointer stability. They prevent duplicate object pointing to same memory, same object pointing to two different memory and other bugs.
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    SkipList(SkipList&&) = delete;
    SkipList& operator=(SkipList&&) = delete;

    // Core point operations
    // Here std::string_view ensures that unnecessary allocations don't happen when passing string in parameters. For example when "atta_5kg" is passed in Put() method, string_view directly points to the bytes allocated in temp memory instead of it creating a own heap memory. [[nodiscard]] is a dev helper tool that gives warning if the caller calls the function and throws away the returned value, just a DX betterment tool. A trailing const declares that this member function is a read-only inspector that promises not to mutate any member variables of the SkipList class
    void Put(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string_view> Get(std::string_view key) const;
    [[nodiscard]] uint8_t CurrentHeight() const noexcept;

private:
    struct Node
    {
        std::string_view key;
        std::string_view value;
        uint8_t height;

        // Flexible array tail for multi-level forward pointers. Instead of writing Node* forward[kMaxHeight] (forward[16]), we write just 1 and allocate memory when needed. This prevents wastage. vector can be used for dynamic allocation but it internally uses malloc which defeats the whole point of using region_allocator. Important point is every item has only one node. It's just that some node have multiple forward pointers which create an illusion of multiple levels. Also for multiple forward pointers, we just use manual memory allocation with required height.
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

    // Mersenne Twister pseudo random number generator
    std::mt19937 rng_{std::random_device{}()};

    // Maps random bits onto flat continuous floating point curve between 0.0 and 1.0. Both these makes sure that the probabilistic coin toss is always close to 50%
    std::uniform_real_distribution<float> distribution_{0.0f, 1.0f};
};

} // namespace engine