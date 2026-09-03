#include <gtest/gtest.h>
#include "engine/region_allocator.hpp"

namespace
{

TEST(RegionAllocatorTest, EnforcesEightByteAlignment)
{
    engine::RegionAllocator allocator(1024 * 1024); // 1 MB test pool

    // Request odd byte sizes to force potential misalignments
    void* p1 = allocator.Allocate(3);
    void* p2 = allocator.Allocate(7);
    void* p3 = allocator.Allocate(13);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);

    // Verify memory addresses land exactly on 8-byte boundaries
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
    EXPECT_NE(key.data(), "Atta_5kg"); // Must point to allocated RAM, not the string literal
}

} // namespace