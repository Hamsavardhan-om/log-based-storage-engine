#include <gtest/gtest.h>
#include "engine/recovery.hpp"
#include "engine/wal.hpp"
#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace
{

class RecoveryTest : public ::testing::Test
{
protected:
    const std::string test_log = "scratch_recovery_test.wal";

    void SetUp() override
    {
        ::unlink(test_log.c_str());
    }

    void TearDown() override
    {
        ::unlink(test_log.c_str());
    }
};

TEST_F(RecoveryTest, ReplaysValidRecordsIntoEmptySkipList)
{
    // Write entries to disk
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_A", "100"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_B", "200"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_A", "150")); // Update
    }

    // Allocate an empty in-memory index
    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    // Run recovery
    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 3);
    EXPECT_FALSE(stats.torn_tail_detected);

    // Verify SkipList contains pre-crash values
    auto val_a = skiplist.Get("Product_A");
    ASSERT_TRUE(val_a.has_value());
    EXPECT_EQ(*val_a, "150");

    auto val_b = skiplist.Get("Product_B");
    ASSERT_TRUE(val_b.has_value());
    EXPECT_EQ(*val_b, "200");
}

TEST_F(RecoveryTest, DetectsAndTruncatesTornTail)
{
    uint64_t valid_size = 0;

    // Write valid records
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key1", "Val1"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key2", "Val2"));
        valid_size = writer.FileSize();
    }

    // Inject torn write by appending corrupted tail bytes
    {
        int fd = ::open(test_log.c_str(), O_WRONLY | O_APPEND);
        ASSERT_GE(fd, 0);
        const char garbage[] = "PARTIAL_HEADER_BYTES";
        ASSERT_GT(::write(fd, garbage, sizeof(garbage)), 0);
        ::fdatasync(fd);
        ::close(fd);
    }

    // Replay into fresh memory
    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 2);
    EXPECT_TRUE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, valid_size);

    // Verify file on disk was trimmed back to valid_size
    struct stat st{};
    ASSERT_EQ(::stat(test_log.c_str(), &st), 0);
    EXPECT_EQ(static_cast<uint64_t>(st.st_size), valid_size);

    auto k1 = skiplist.Get("Key1");
    ASSERT_TRUE(k1.has_value());
    EXPECT_EQ(*k1, "Val1");
}

}