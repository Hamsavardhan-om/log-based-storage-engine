#include <gtest/gtest.h>
#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"

#include <string>

namespace
{

TEST(SkipListTest, HandlesPointInsertsAndQueries)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    list.Put("Atta_5kg", "120");
    list.Put("Basmati_Rice", "250");
    list.Put("Tata_Salt", "30");

    auto val1 = list.Get("Atta_5kg");
    auto val2 = list.Get("Basmati_Rice");
    auto val3 = list.Get("Tata_Salt");
    auto missing = list.Get("NonExistent_Item");

    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, "120");

    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val2, "250");

    ASSERT_TRUE(val3.has_value());
    EXPECT_EQ(*val3, "30");

    EXPECT_FALSE(missing.has_value());
}

TEST(SkipListTest, HandlesInPlaceUpdates)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    list.Put("Atta_5kg", "120");
    list.Put("Atta_5kg", "119");

    auto updated_val = list.Get("Atta_5kg");
    ASSERT_TRUE(updated_val.has_value());
    EXPECT_EQ(*updated_val, "119");
}

TEST(SkipListTest, PersistsAcrossScopeDestruction)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    {
        std::string temp_key = "Transient_Milk";
        std::string temp_val = "60";
        list.Put(temp_key, temp_val);
    }

    auto persistent_val = list.Get("Transient_Milk");
    ASSERT_TRUE(persistent_val.has_value());
    EXPECT_EQ(*persistent_val, "60");
}

} // namespace