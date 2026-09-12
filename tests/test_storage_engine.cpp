#include <gtest/gtest.h>
#include "engine/storage_engine.hpp"

#include <unistd.h>
#include <string>
#include <thread>
#include <vector>

namespace
{

class StorageEngineTest : public ::testing::Test
{
protected:
    const std::string test_wal = "test_facade.wal";

    void SetUp() override
    {
        ::unlink(test_wal.c_str());
    }

    void TearDown() override
    {
        ::unlink(test_wal.c_str());
    }
};

TEST_F(StorageEngineTest, HandlesBasicPutAndGet)
{
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("Atta_5kg", "120"));
        EXPECT_TRUE(db.Put("Basmati_Rice", "250"));

        auto v1 = db.Get("Atta_5kg");
        auto v2 = db.Get("Basmati_Rice");
        auto missing = db.Get("NonExistent");

        ASSERT_TRUE(v1.has_value());
        EXPECT_EQ(*v1, "120");
        ASSERT_TRUE(v2.has_value());
        EXPECT_EQ(*v2, "250");
        EXPECT_FALSE(missing.has_value());
    }
}

TEST_F(StorageEngineTest, HandlesOverwrites)
{
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("Atta_5kg", "120"));
        EXPECT_TRUE(db.Put("Atta_5kg", "119"));

        auto val = db.Get("Atta_5kg");
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, "119");
    }
}

TEST_F(StorageEngineTest, HandlesTombstoneDeletions)
{
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("Item1", "Active"));
        EXPECT_TRUE(db.Delete("Item1"));

        // Get on deleted item returns std::nullopt
        auto val = db.Get("Item1");
        EXPECT_FALSE(val.has_value());
    }
}

TEST_F(StorageEngineTest, ReconstructsRAMIndexOnReboot)
{
    // Write records and simulate graceful shutdown
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("K1", "V1"));
        EXPECT_TRUE(db.Put("K2", "V2"));
        EXPECT_TRUE(db.Put("K3", "V3"));
    }

    // Reboot engine: should rebuild from disk WAL automatically
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);

        auto v1 = db.Get("K1");
        auto v2 = db.Get("K2");
        auto v3 = db.Get("K3");

        ASSERT_TRUE(v1.has_value());
        EXPECT_EQ(*v1, "V1");
        ASSERT_TRUE(v2.has_value());
        EXPECT_EQ(*v2, "V2");
        ASSERT_TRUE(v3.has_value());
        EXPECT_EQ(*v3, "V3");
    }
}

TEST_F(StorageEngineTest, RebootPreservesDeletions)
{
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("K1", "V1"));
        EXPECT_TRUE(db.Delete("K1"));
    }

    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 2u);
        EXPECT_FALSE(db.Get("K1").has_value());
    }
}

TEST_F(StorageEngineTest, ContinuesWritingAfterReboot)
{
    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("K1", "V1"));
    }

    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("K2", "V2"));

        EXPECT_TRUE(db.Get("K1").has_value());
        EXPECT_TRUE(db.Get("K2").has_value());
    }

    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 2u);
        EXPECT_EQ(*db.Get("K1"), "V1");
        EXPECT_EQ(*db.Get("K2"), "V2");
    }
}

TEST_F(StorageEngineTest, ExplicitSyncCallSucceeds)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("SyncKey", "SyncVal"));
    EXPECT_TRUE(db.Sync());
}

TEST_F(StorageEngineTest, ReportsDiskFileSizeAccurately)
{
    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.WalDiskSize(), 0u);

    EXPECT_TRUE(db.Put("Key", "Value"));
    // 16-byte header + 3-byte key + 5-byte val = 24 bytes
    EXPECT_EQ(db.WalDiskSize(), 24u);
}

TEST_F(StorageEngineTest, HandlesLargePayloads)
{
    engine::StorageEngine db(test_wal);
    std::string large_val(1024 * 32, 'D'); // 32 KB value

    EXPECT_TRUE(db.Put("LargeItem", large_val));

    auto res = db.Get("LargeItem");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, large_val);
}

TEST_F(StorageEngineTest, ConcurrentReadsAndWrites)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("SharedKey", "Initial"));

    std::atomic<bool> done{false};

    std::thread reader([&]() {
        while (!done.load())
        {
            auto val = db.Get("SharedKey");
            ASSERT_TRUE(val.has_value());
        }
    });

    for (int i = 0; i < 200; ++i)
    {
        EXPECT_TRUE(db.Put("SharedKey", std::to_string(i)));
        EXPECT_TRUE(db.Put("Item_" + std::to_string(i), "Constant"));
    }

    done.store(true);
    reader.join();
}

} // namespace