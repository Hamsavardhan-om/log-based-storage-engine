#include "engine/skiplist.hpp"

#include <new>
#include <array>
#include <mutex>

namespace engine
{

SkipList::SkipList(RegionAllocator& allocator)
    : allocator_(allocator)
{
    // Sentinel head node spans all possible levels
    head_ = CreateNode("", "", kMaxHeight);
}

SkipList::Node* SkipList::CreateNode(std::string_view key, std::string_view value, uint8_t height)
{
    const size_t total_node_bytes = Node::AllocationSize(height);

    // 1. Carve 8-byte aligned raw memory slice directly from RegionAllocator
    void* raw_mem = allocator_.Allocate(total_node_bytes);
    Node* node = new (raw_mem) Node();

    // 2. Persist transient keys & values into persistent region-owned memory
    node->key = allocator_.AllocateString(key);
    node->value = allocator_.AllocateString(value);
    node->height = height;

    // 3. Clear forward express pointers
    for (uint8_t i = 0; i < height; ++i)
    {
        node->forward[i] = nullptr;
    }

    return node;
}

uint8_t SkipList::GenerateRandomHeight()
{
    uint8_t height = 1;
    while (height < kMaxHeight && distribution_(rng_) < kBranchingProbability)
    {
        ++height;
    }
    return height;
}

uint8_t SkipList::CurrentHeight() const noexcept
{
    std::shared_lock<std::shared_mutex> lock(rw_lock_);
    return current_height_;
}

void SkipList::Put(std::string_view key, std::string_view value)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock_);

    std::array<Node*, kMaxHeight> update{};
    Node* current = head_;

    // 1. Descend express lanes to locate splice offsets per level
    for (int i = current_height_ - 1; i >= 0; --i)
    {
        while (current->forward[i] != nullptr && current->forward[i]->key < key)
        {
            current = current->forward[i];
        }
        update[static_cast<size_t>(i)] = current;
    }

    current = current->forward[0];

    // 2. Existing key found: overwrite value in region memory (zero link adjustments)
    if (current != nullptr && current->key == key)
    {
        current->value = allocator_.AllocateString(value);
        return;
    }

    // 3. New entry: compute coin-toss level elevation
    const uint8_t new_height = GenerateRandomHeight();
    if (new_height > current_height_)
    {
        for (uint8_t i = current_height_; i < new_height; ++i)
        {
            update[i] = head_;
        }
        current_height_ = new_height;
    }

    // 4. Carve node from RegionAllocator and splice pointers across active levels
    Node* new_node = CreateNode(key, value, new_height);
    for (uint8_t i = 0; i < new_height; ++i)
    {
        new_node->forward[i] = update[static_cast<size_t>(i)]->forward[i];
        update[static_cast<size_t>(i)]->forward[i] = new_node;
    }
}

std::optional<std::string_view> SkipList::Get(std::string_view key) const
{
    std::shared_lock<std::shared_mutex> lock(rw_lock_);

    const Node* current = head_;

    // Drop through shortcuts from top express lane down to base level 0
    for (int i = current_height_ - 1; i >= 0; --i)
    {
        while (current->forward[i] != nullptr && current->forward[i]->key < key)
        {
            current = current->forward[i];
        }
    }

    current = current->forward[0];

    if (current != nullptr && current->key == key)
    {
        return current->value;
    }

    return std::nullopt;
}

} // namespace engine