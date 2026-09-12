#include <gtest/gtest.h>
#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"

#include <string>
#include <vector>
#include <thread>
#include <algorithm>

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

TEST(SkipListTest, HandlesEmptyKeyAndValue)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    list.Put("", "EmptyKeyVal");
    list.Put("KeyWithEmptyVal", "");

    auto v1 = list.Get("");
    auto v2 = list.Get("KeyWithEmptyVal");

    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, "EmptyKeyVal");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, "");
}

TEST(SkipListTest, HandlesAscendingSequentialInserts)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    for (int i = 0; i < 200; ++i)
    {
        std::string key = "Key_" + std::to_string(i);
        std::string val = "Val_" + std::to_string(i);
        list.Put(key, val);
    }

    for (int i = 0; i < 200; ++i)
    {
        std::string key = "Key_" + std::to_string(i);
        std::string expected_val = "Val_" + std::to_string(i);
        auto val = list.Get(key);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, expected_val);
    }
}

TEST(SkipListTest, HandlesDescendingSequentialInserts)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    for (int i = 200; i >= 0; --i)
    {
        std::string key = "Key_" + std::to_string(i);
        std::string val = "Val_" + std::to_string(i);
        list.Put(key, val);
    }

    for (int i = 200; i >= 0; --i)
    {
        std::string key = "Key_" + std::to_string(i);
        auto val = list.Get(key);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, "Val_" + std::to_string(i));
    }
}

TEST(SkipListTest, MultipleConsecutiveUpdates)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    for (int i = 0; i < 50; ++i)
    {
        list.Put("MutableKey", std::to_string(i));
        auto val = list.Get("MutableKey");
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, std::to_string(i));
    }
}

TEST(SkipListTest, LargePayloadHandling)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    std::string large_key(1024, 'K');
    std::string large_val(1024 * 16, 'V');

    list.Put(large_key, large_val);
    auto res = list.Get(large_key);

    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, large_val);
}

TEST(SkipListTest, QueryOnEmptyList)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    EXPECT_FALSE(list.Get("NonExistent").has_value());
    EXPECT_EQ(list.CurrentHeight(), 1);
}

TEST(SkipListTest, ConcurrentReadsDuringWrites)
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    list.Put("BaseKey", "InitialVal");

    std::atomic<bool> stop_signal{false};

    // Reader thread continuously queries
    std::thread reader([&]() {
        while (!stop_signal.load())
        {
            auto val = list.Get("BaseKey");
            ASSERT_TRUE(val.has_value());
        }
    });

    // Writer thread performs updates
    for (int i = 0; i < 500; ++i)
    {
        list.Put("BaseKey", std::to_string(i));
        list.Put("Key_" + std::to_string(i), "Val");
    }

    stop_signal.store(true);
    reader.join();
}

} // namespace