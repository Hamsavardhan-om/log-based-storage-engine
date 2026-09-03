#include "engine/region_allocator.hpp"

#include <cstring>
#include <algorithm>
#include <cassert>

namespace engine
{

RegionAllocator::RegionAllocator(size_t chunk_size)
    : chunk_size_(chunk_size)
{
    AllocateNewChunk(chunk_size_);
}

RegionAllocator::~RegionAllocator()
{
    // chunks_ vector clears automatically, freeing all 2MB blocks in a single pass
}

void* RegionAllocator::Allocate(size_t bytes)
{
    if (bytes == 0)
    {
        return nullptr;
    }

    // 1. Calculate the padding needed to satisfy 8-byte hardware alignment
    const uintptr_t current_addr = reinterpret_cast<uintptr_t>(alloc_ptr_);
    const size_t misalignment = current_addr % kDefaultAlignment;
    const size_t padding = (misalignment == 0) ? 0 : (kDefaultAlignment - misalignment);

    const size_t total_needed = bytes + padding;

    // 2. If the current block runs out of room, allocate a fresh chunk
    if (total_needed > bytes_remaining_)
    {
        // Support individual items that exceed the default 2MB capacity
        AllocateNewChunk(std::max(chunk_size_, bytes + kDefaultAlignment));
        return Allocate(bytes);
    }

    // 3. Slide the bump pointer past the alignment padding
    char* aligned_ptr = alloc_ptr_ + padding;
    alloc_ptr_ += total_needed;
    bytes_remaining_ -= total_needed;
    total_allocated_ += total_needed;

    // Verify pointer is aligned to an 8-byte boundary
    assert(reinterpret_cast<uintptr_t>(aligned_ptr) % kDefaultAlignment == 0);

    return aligned_ptr;
}

std::string_view RegionAllocator::AllocateString(std::string_view str)
{
    if (str.empty())
    {
        return {};
    }

    char* dest = static_cast<char*>(Allocate(str.size()));
    std::memcpy(dest, str.data(), str.size());
    return std::string_view(dest, str.size());
}

void RegionAllocator::AllocateNewChunk(size_t min_bytes)
{
    const size_t sz = std::max(chunk_size_, min_bytes);
    chunks_.emplace_back(sz);

    alloc_ptr_ = chunks_.back().memory.get();
    bytes_remaining_ = sz;
}

size_t RegionAllocator::TotalAllocatedBytes() const noexcept
{
    return total_allocated_;
}

size_t RegionAllocator::TotalChunkMemory() const noexcept
{
    size_t total = 0;
    for (const auto& chunk : chunks_)
    {
        total += chunk.size;
    }
    return total;
}

} // namespace engine