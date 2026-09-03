#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <string_view>

namespace engine
{

class RegionAllocator
{
public:
    // Default 2MB contiguous chunk size to minimize OS syscall overhead
    static constexpr size_t kDefaultChunkSize = 2 * 1024 * 1024;
    // Hardware boundary alignment: 8 bytes on standard 64-bit architectures
    static constexpr size_t kDefaultAlignment = alignof(std::max_align_t);

    explicit RegionAllocator(size_t chunk_size = kDefaultChunkSize);
    ~RegionAllocator();

    // Memory regions manage unique system resources: non-copyable and non-movable
    RegionAllocator(const RegionAllocator&) = delete;
    RegionAllocator& operator=(const RegionAllocator&) = delete;
    RegionAllocator(RegionAllocator&&) = delete;
    RegionAllocator& operator=(RegionAllocator&&) = delete;

    // Slices out raw bytes guaranteed to be 8-byte aligned
    void* Allocate(size_t bytes);

    // Deep-copies a transient string_view into persistent, region-owned RAM
    std::string_view AllocateString(std::string_view str);

    // Diagnostics and telemetry
    [[nodiscard]] size_t TotalAllocatedBytes() const noexcept;
    [[nodiscard]] size_t TotalChunkMemory() const noexcept;

private:
    struct Chunk
    {
        std::unique_ptr<char[]> memory;
        size_t size;

        explicit Chunk(size_t sz)
            : memory(std::make_unique<char[]>(sz)), size(sz)
        {
        }
    };

    void AllocateNewChunk(size_t min_bytes);

    size_t chunk_size_;
    char* alloc_ptr_{nullptr};
    size_t bytes_remaining_{0};

    // Tracks every chunk allocated so they can be bulk-freed upon destruction
    std::vector<Chunk> chunks_;
    size_t total_allocated_{0};
};

} // namespace engine