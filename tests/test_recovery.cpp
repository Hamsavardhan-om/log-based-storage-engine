#include <gtest/gtest.h>
#include "engine/recovery.hpp"
#include "engine/wal.hpp"
#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

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
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_A", "100"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_B", "200"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Product_A", "150"));
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 3);
    EXPECT_FALSE(stats.torn_tail_detected);

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

    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key1", "Val1"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key2", "Val2"));
        valid_size = writer.FileSize();
    }

    // Corrupt the tail with garbage bytes
    {
        int fd = ::open(test_log.c_str(), O_WRONLY | O_APPEND);
        ASSERT_GE(fd, 0);
        const char garbage[] = "PARTIAL_HEADER_BYTES";
        ASSERT_GT(::write(fd, garbage, sizeof(garbage)), 0);
        ::fdatasync(fd);
        ::close(fd);
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 2);
    EXPECT_TRUE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, valid_size);

    struct stat st{};
    ASSERT_EQ(::stat(test_log.c_str(), &st), 0);
    EXPECT_EQ(static_cast<uint64_t>(st.st_size), valid_size);
}

TEST_F(RecoveryTest, HandlesNonExistentFileGracefully)
{
    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery("missing_file.wal");
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 0u);
    EXPECT_FALSE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, 0u);
}

TEST_F(RecoveryTest, HandlesEmptyWalFile)
{
    // Create 0-byte file
    int fd = ::open(test_log.c_str(), O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    ::close(fd);

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 0u);
    EXPECT_FALSE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, 0u);
}

TEST_F(RecoveryTest, DetectsTornHeaderLessThan16Bytes)
{
    // Write only 10 bytes to disk
    {
        int fd = ::open(test_log.c_str(), O_CREAT | O_WRONLY, 0644);
        const char bad_header[10] = {0};
        ASSERT_EQ(::write(fd, bad_header, 10), 10);
        ::close(fd);
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 0u);
    EXPECT_TRUE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, 0u);

    // Verify truncated to 0
    struct stat st{};
    ASSERT_EQ(::stat(test_log.c_str(), &st), 0);
    EXPECT_EQ(st.st_size, 0);
}

TEST_F(RecoveryTest, DetectsTornPayloadMissingValueBytes)
{
    uint64_t valid_offset = 0;
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Alpha", "100"));
        valid_offset = writer.FileSize();
    }

    // Write a valid header claiming 20 bytes of payload, but supply only 5
    {
        engine::WalHeader fake_header;
        fake_header.crc32 = 12345;
        fake_header.op_type = static_cast<uint8_t>(engine::OpType::kPut);
        fake_header.key_len = 10;
        fake_header.val_len = 10;

        int fd = ::open(test_log.c_str(), O_WRONLY | O_APPEND);
        ASSERT_EQ(::write(fd, &fake_header, sizeof(fake_header)), sizeof(fake_header));
        ASSERT_EQ(::write(fd, "12345", 5), 5); // Severed mid-payload
        ::close(fd);
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 1u);
    EXPECT_TRUE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, valid_offset);
}

TEST_F(RecoveryTest, DetectsBitFlipPayloadChecksumMismatch)
{
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key1", "Value1"));
    }

    // Corrupt single payload byte in place
    {
        int fd = ::open(test_log.c_str(), O_RDWR);
        ::lseek(fd, sizeof(engine::WalHeader) + 2, SEEK_SET); // Seek into key
        char corrupt_byte = 'X';
        ASSERT_EQ(::write(fd, &corrupt_byte, 1), 1);
        ::close(fd);
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 0u);
    EXPECT_TRUE(stats.torn_tail_detected);
    EXPECT_EQ(stats.last_valid_offset, 0u);
}

TEST_F(RecoveryTest, ReplaysDeletionsAsTombstones)
{
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "TargetKey", "Alive"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kDelete, "TargetKey", ""));
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 2u);
    auto val = skiplist.Get("TargetKey");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, ""); // Tombstone
}

TEST_F(RecoveryTest, RecoveryIsIdempotentAcrossMultipleRuns)
{
    {
        engine::WalWriter writer(test_log);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "K1", "V1"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "K2", "V2"));
    }

    engine::RegionAllocator alloc1;
    engine::SkipList list1(alloc1);
    engine::RecoveryEngine rec1(test_log);
    rec1.Recover(list1);

    // Second recovery run over the same file
    engine::RegionAllocator alloc2;
    engine::SkipList list2(alloc2);
    engine::RecoveryEngine rec2(test_log);
    engine::RecoveryStats stats2 = rec2.Recover(list2);

    EXPECT_EQ(stats2.records_replayed, 2u);
    EXPECT_EQ(*list1.Get("K1"), *list2.Get("K1"));
    EXPECT_EQ(*list1.Get("K2"), *list2.Get("K2"));
}

TEST_F(RecoveryTest, ReplaysInterleavedPutsAndDeletes)
{
    {
        engine::WalWriter writer(test_log);
        for (int i = 0; i < 50; ++i)
        {
            writer.AppendRecord(engine::OpType::kPut, "K_" + std::to_string(i), "V_" + std::to_string(i));
        }
        // Delete all even keys
        for (int i = 0; i < 50; i += 2)
        {
            writer.AppendRecord(engine::OpType::kDelete, "K_" + std::to_string(i), "");
        }
    }

    engine::RegionAllocator allocator;
    engine::SkipList skiplist(allocator);

    engine::RecoveryEngine recovery(test_log);
    engine::RecoveryStats stats = recovery.Recover(skiplist);

    EXPECT_EQ(stats.records_replayed, 75u);

    // Check states
    for (int i = 0; i < 50; ++i)
    {
        auto val = skiplist.Get("K_" + std::to_string(i));
        ASSERT_TRUE(val.has_value());
        if (i % 2 == 0)
        {
            EXPECT_EQ(*val, ""); // Tombstone
        }
        else
        {
            EXPECT_EQ(*val, "V_" + std::to_string(i));
        }
    }
}

} // namespace