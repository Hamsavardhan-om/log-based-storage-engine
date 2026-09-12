#include <gtest/gtest.h>
#include "engine/region_allocator.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

TEST(RegionAllocatorTest, EnforcesEightByteAlignment)
{
    engine::RegionAllocator allocator(1024 * 1024);
    void* p1 = allocator.Allocate(3);
    void* p2 = allocator.Allocate(7);
    void* p3 = allocator.Allocate(13);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);

    EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) % 8, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 8, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p3) % 8, 0);
}

TEST(RegionAllocatorTest, DeepCopiesStringsIntoContiguousMemory)
{
    engine::RegionAllocator allocator;
    std::string_view key = allocator.AllocateString("Atta_5kg");
    std::string_view val = allocator.AllocateString("99");

    EXPECT_EQ(key, "Atta_5kg");
    EXPECT_EQ(val, "99");
    EXPECT_NE(key.data(), "Atta_5kg");
}

TEST(RegionAllocatorTest, HandlesZeroByteAllocation)
{
    engine::RegionAllocator allocator;
    void* ptr = allocator.Allocate(0);
    EXPECT_EQ(ptr, nullptr);
}

TEST(RegionAllocatorTest, HandlesEmptyStringAllocation)
{
    engine::RegionAllocator allocator;
    std::string_view sv = allocator.AllocateString("");
    EXPECT_TRUE(sv.empty());
}

TEST(RegionAllocatorTest, AllocatesContiguouslyWithoutOverlap)
{
    engine::RegionAllocator allocator(1024 * 1024);
    char* a = static_cast<char*>(allocator.Allocate(16));
    char* b = static_cast<char*>(allocator.Allocate(16));

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_GE(reinterpret_cast<uintptr_t>(b), reinterpret_cast<uintptr_t>(a) + 16);
}

TEST(RegionAllocatorTest, MemoryWriteIntegrity)
{
    engine::RegionAllocator allocator;
    constexpr size_t kSize = 128;
    uint8_t* buffer = static_cast<uint8_t*>(allocator.Allocate(kSize));
    ASSERT_NE(buffer, nullptr);

    for (size_t i = 0; i < kSize; ++i)
    {
        buffer[i] = static_cast<uint8_t>(i & 0xFF);
    }

    for (size_t i = 0; i < kSize; ++i)
    {
        EXPECT_EQ(buffer[i], static_cast<uint8_t>(i & 0xFF));
    }
}

TEST(RegionAllocatorTest, AllocationsSpanningMultipleChunks)
{
    // Initialize with a tiny chunk size to force spawning new slabs
    engine::RegionAllocator allocator(1024); // 1 KB chunks
    std::vector<void*> ptrs;

    for (int i = 0; i < 50; ++i)
    {
        void* p = allocator.Allocate(128);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 8, 0);
        ptrs.push_back(p);
    }

    EXPECT_GT(allocator.TotalChunkMemory(), 1024u);
}

TEST(RegionAllocatorTest, OversizedAllocationExceedingChunkSize)
{
    // Request an allocation far larger than the default chunk size
    engine::RegionAllocator allocator(1024);
    void* large_ptr = allocator.Allocate(1024 * 64); // 64 KB
    ASSERT_NE(large_ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(large_ptr) % 8, 0);
}

TEST(RegionAllocatorTest, TracksMetricsAccurately)
{
    engine::RegionAllocator allocator(4096);
    EXPECT_EQ(allocator.TotalAllocatedBytes(), 0u);

    allocator.Allocate(64);
    EXPECT_GE(allocator.TotalAllocatedBytes(), 64u);
    EXPECT_GE(allocator.TotalChunkMemory(), 4096u);
}

TEST(RegionAllocatorTest, AllocatesStringWithEmbeddedNulls)
{
    engine::RegionAllocator allocator;
    std::string raw_data("binary\0data\0test", 16);
    std::string_view sv = allocator.AllocateString(raw_data);

    EXPECT_EQ(sv.size(), 16u);
    EXPECT_EQ(std::memcmp(sv.data(), raw_data.data(), 16), 0);
}

} // namespace