#include <gtest/gtest.h>
#include "engine/storage_engine.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <random>

namespace
{

class StorageEngineE2ETest : public ::testing::Test
{
protected:
    const std::string test_wal = "e2e_storage_engine.wal";

    void SetUp() override
    {
        ::unlink(test_wal.c_str());
    }

    void TearDown() override
    {
        ::unlink(test_wal.c_str());
    }
};

// ============================================================================
// Group 1: Basic Operations & Contract Guarantees (Tests 1 - 8)
// ============================================================================

TEST_F(StorageEngineE2ETest, 01_BasicPutAndGet)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("Apple", "100"));
    auto res = db.Get("Apple");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "100");
}

TEST_F(StorageEngineE2ETest, 02_GetNonExistentKey)
{
    engine::StorageEngine db(test_wal);
    EXPECT_FALSE(db.Get("MissingKey").has_value());
}

TEST_F(StorageEngineE2ETest, 03_OverwriteSingleKey)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("Key", "Value1"));
    EXPECT_TRUE(db.Put("Key", "Value2"));
    auto res = db.Get("Key");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "Value2");
}

TEST_F(StorageEngineE2ETest, 04_DeleteExistingKey)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("Key", "Value"));
    EXPECT_TRUE(db.Delete("Key"));
    EXPECT_FALSE(db.Get("Key").has_value());
}

TEST_F(StorageEngineE2ETest, 05_DeleteNonExistentKey)
{
    engine::StorageEngine db(test_wal);
    // Deleting a non-existent key appends a tombstone without crashing
    EXPECT_TRUE(db.Delete("GhostKey"));
    EXPECT_FALSE(db.Get("GhostKey").has_value());
}

TEST_F(StorageEngineE2ETest, 06_PutAfterDelete)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("ReviveKey", "Old"));
    EXPECT_TRUE(db.Delete("ReviveKey"));
    EXPECT_TRUE(db.Put("ReviveKey", "New"));
    auto res = db.Get("ReviveKey");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "New");
}

TEST_F(StorageEngineE2ETest, 07_ExplicitSyncCall)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("Key", "Val"));
    EXPECT_TRUE(db.Sync());
}

TEST_F(StorageEngineE2ETest, 08_EmptyLogInitialTelemetry)
{
    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 0u);
    EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.WalDiskSize(), 0u);
}

// ============================================================================
// Group 2: Binary Boundaries & Exotic Keys/Values (Tests 9 - 16)
// ============================================================================

TEST_F(StorageEngineE2ETest, 09_EmptyKeyWithNormalValue)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("", "EmptyKeyValue"));
    auto res = db.Get("");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "EmptyKeyValue");
}

TEST_F(StorageEngineE2ETest, 10_PutEmptyValueTreatedAsTombstoneOnGet)
{
    engine::StorageEngine db(test_wal);
    // Under engine facade contract, an empty value acts as a tombstone
    EXPECT_TRUE(db.Put("EmptyValKey", ""));
    EXPECT_FALSE(db.Get("EmptyValKey").has_value());
}

TEST_F(StorageEngineE2ETest, 11_KeyWithEmbeddedNullBytes)
{
    engine::StorageEngine db(test_wal);
    std::string binary_key("key\0hidden\0null", 15);
    EXPECT_TRUE(db.Put(binary_key, "Val"));
    auto res = db.Get(binary_key);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "Val");
}

TEST_F(StorageEngineE2ETest, 12_ValueWithEmbeddedNullBytes)
{
    engine::StorageEngine db(test_wal);
    std::string binary_val("val\0payload\0test", 16);
    EXPECT_TRUE(db.Put("BinKey", binary_val));
    auto res = db.Get("BinKey");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->size(), 16u);
    EXPECT_EQ(std::string(*res), binary_val);
}

TEST_F(StorageEngineE2ETest, 13_BothKeyAndValueWithEmbeddedNullBytes)
{
    engine::StorageEngine db(test_wal);
    std::string binary_key("null\0k", 6);
    std::string binary_val("null\0v", 6);
    EXPECT_TRUE(db.Put(binary_key, binary_val));
    auto res = db.Get(binary_key);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(std::string(*res), binary_val);
}

TEST_F(StorageEngineE2ETest, 14_WhitespaceAndControlCharacters)
{
    engine::StorageEngine db(test_wal);
    std::string ws_key = "\t\n\r Special Key \a\b";
    std::string ws_val = "\x01\x02\x03\x7F Value";
    EXPECT_TRUE(db.Put(ws_key, ws_val));
    auto res = db.Get(ws_key);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, ws_val);
}

TEST_F(StorageEngineE2ETest, 15_SingleByteKeyAndValue)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("a", "b"));
    auto res = db.Get("a");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "b");
}

TEST_F(StorageEngineE2ETest, 16_LargePayloadSlabSpan)
{
    engine::StorageEngine db(test_wal);
    std::string large_val(1024 * 1024, 'X'); // 1 MB payload
    EXPECT_TRUE(db.Put("1MBKey", large_val));
    auto res = db.Get("1MBKey");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, large_val);
}

// ============================================================================
// Group 3: Memory Safety & Lifetime Persistence (Tests 17 - 24)
// ============================================================================

TEST_F(StorageEngineE2ETest, 17_TemporaryStringLifetimeOutlivingScope)
{
    engine::StorageEngine db(test_wal);
    {
        std::string temp_key = "OutOfScopeKey";
        std::string temp_val = "OutOfScopeVal";
        EXPECT_TRUE(db.Put(temp_key, temp_val));
    }
    auto res = db.Get("OutOfScopeKey");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "OutOfScopeVal");
}

TEST_F(StorageEngineE2ETest, 18_RepeatedOverwritesPreservesAlignmentAndMemory)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 50; ++i)
    {
        EXPECT_TRUE(db.Put("RecycleKey", std::to_string(i)));
    }
    auto res = db.Get("RecycleKey");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, "49");
}

TEST_F(StorageEngineE2ETest, 19_AscendingKeySequentialSplay)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 100; ++i)
    {
        std::string k = "Asc_" + std::to_string(i);
        std::string v = "Val_" + std::to_string(i);
        EXPECT_TRUE(db.Put(k, v));
    }
    for (int i = 0; i < 100; ++i)
    {
        std::string k = "Asc_" + std::to_string(i);
        EXPECT_EQ(*db.Get(k), "Val_" + std::to_string(i));
    }
}

TEST_F(StorageEngineE2ETest, 20_DescendingKeySequentialSplay)
{
    engine::StorageEngine db(test_wal);
    for (int i = 100; i >= 0; --i)
    {
        std::string k = "Desc_" + std::to_string(i);
        std::string v = "Val_" + std::to_string(i);
        EXPECT_TRUE(db.Put(k, v));
    }
    for (int i = 100; i >= 0; --i)
    {
        std::string k = "Desc_" + std::to_string(i);
        EXPECT_EQ(*db.Get(k), "Val_" + std::to_string(i));
    }
}

TEST_F(StorageEngineE2ETest, 21_InterleavedPutsAndDeletesInSingleRun)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 100; ++i)
    {
        db.Put("Key_" + std::to_string(i), "Val");
    }
    for (int i = 0; i < 100; i += 2)
    {
        db.Delete("Key_" + std::to_string(i));
    }
    for (int i = 0; i < 100; ++i)
    {
        auto res = db.Get("Key_" + std::to_string(i));
        if (i % 2 == 0)
        {
            EXPECT_FALSE(res.has_value());
        }
        else
        {
            ASSERT_TRUE(res.has_value());
            EXPECT_EQ(*res, "Val");
        }
    }
}

TEST_F(StorageEngineE2ETest, 22_RepeatedDeletesOnSameKey)
{
    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.Put("Key", "Val"));
    EXPECT_TRUE(db.Delete("Key"));
    EXPECT_TRUE(db.Delete("Key"));
    EXPECT_TRUE(db.Delete("Key"));
    EXPECT_FALSE(db.Get("Key").has_value());
}

TEST_F(StorageEngineE2ETest, 23_KeyPrefixCollisionsInIndex)
{
    engine::StorageEngine db(test_wal);
    db.Put("prefix", "v1");
    db.Put("prefix_a", "v2");
    db.Put("prefix_b", "v3");
    db.Put("prefix_aa", "v4");

    EXPECT_EQ(*db.Get("prefix"), "v1");
    EXPECT_EQ(*db.Get("prefix_a"), "v2");
    EXPECT_EQ(*db.Get("prefix_b"), "v3");
    EXPECT_EQ(*db.Get("prefix_aa"), "v4");
}

TEST_F(StorageEngineE2ETest, 24_TotalExactDiskFootprintAccounting)
{
    engine::StorageEngine db(test_wal);
    // Put: 16 (hdr) + 3 (k) + 3 (v) = 22
    db.Put("key", "val");
    // Delete: 16 (hdr) + 3 (k) + 0 (v) = 19
    db.Delete("key");
    EXPECT_EQ(db.WalDiskSize(), 41u);
}

// ============================================================================
// Group 4: Cold Boot, Crash Replay & State Continuity (Tests 25 - 34)
// ============================================================================

TEST_F(StorageEngineE2ETest, 25_CleanRebootRebuildsAllKeys)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("K1", "V1");
        db.Put("K2", "V2");
        db.Put("K3", "V3");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(*db.Get("K1"), "V1");
        EXPECT_EQ(*db.Get("K2"), "V2");
        EXPECT_EQ(*db.Get("K3"), "V3");
    }
}

TEST_F(StorageEngineE2ETest, 26_RebootPreservesInPlaceUpdates)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Item", "10");
        db.Put("Item", "20");
        db.Put("Item", "30");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_EQ(*db.Get("Item"), "30");
    }
}

TEST_F(StorageEngineE2ETest, 27_RebootPreservesDeletionsAcrossSessions)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Permanent", "Stay");
        db.Put("Ephemeral", "Leave");
        db.Delete("Ephemeral");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_EQ(*db.Get("Permanent"), "Stay");
        EXPECT_FALSE(db.Get("Ephemeral").has_value());
    }
}

TEST_F(StorageEngineE2ETest, 28_AppendOperationsAfterRebootPreservesTailAlignment)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Day1", "Cold");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(*db.Get("Day1"), "Cold");
        db.Put("Day2", "Warm");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 2u);
        EXPECT_EQ(*db.Get("Day1"), "Cold");
        EXPECT_EQ(*db.Get("Day2"), "Warm");
    }
}

TEST_F(StorageEngineE2ETest, 29_DoubleRebootWithoutInterveningWrites)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Constant", "Unchanged");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 1u);
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 1u);
        EXPECT_EQ(*db.Get("Constant"), "Unchanged");
    }
}

TEST_F(StorageEngineE2ETest, 30_MultipleRebootSessionChain)
{
    for (int session = 1; session <= 10; ++session)
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, static_cast<size_t>(session - 1));
        db.Put("Session_" + std::to_string(session), std::to_string(session * 10));
    }
    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 10u);
    for (int session = 1; session <= 10; ++session)
    {
        EXPECT_EQ(*db.Get("Session_" + std::to_string(session)), std::to_string(session * 10));
    }
}

TEST_F(StorageEngineE2ETest, 31_RebootPreservesBinaryKeysWithNulls)
{
    std::string null_key("bin\0reboot", 10);
    std::string null_val("payload\0reboot", 14);
    {
        engine::StorageEngine db(test_wal);
        db.Put(null_key, null_val);
    }
    {
        engine::StorageEngine db(test_wal);
        auto res = db.Get(null_key);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(std::string(*res), null_val);
    }
}

TEST_F(StorageEngineE2ETest, 32_RebootPreservesLargePayload)
{
    std::string big(64 * 1024, 'Z');
    {
        engine::StorageEngine db(test_wal);
        db.Put("BigKey", big);
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(*db.Get("BigKey"), big);
    }
}

TEST_F(StorageEngineE2ETest, 33_ZeroByteInitialWalFileHandledGracefully)
{
    int fd = ::open(test_wal.c_str(), O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    ::close(fd);

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 0u);
    EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_TRUE(db.Put("NewKey", "NewVal"));
    EXPECT_EQ(*db.Get("NewKey"), "NewVal");
}

TEST_F(StorageEngineE2ETest, 34_DeleteThenRebootThenPut)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Item", "Live");
        db.Delete("Item");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_FALSE(db.Get("Item").has_value());
        db.Put("Item", "Resurrected");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(*db.Get("Item"), "Resurrected");
    }
}

// ============================================================================
// Group 5: Fault Injection & Torn Tail Healing (Tests 35 - 44)
// ============================================================================

TEST_F(StorageEngineE2ETest, 35_TornTailPrunedAndSubsequentWritesSucceed)
{
    uint64_t valid_size = 0;
    {
        engine::StorageEngine db(test_wal);
        db.Put("Safe1", "Val1");
        db.Put("Safe2", "Val2");
        valid_size = db.WalDiskSize();
    }

    // Append 5 garbage bytes simulating mid-crash header write
    {
        int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(::write(fd, "TRASH", 5), 5);
        ::fdatasync(fd);
        ::close(fd);
    }

    // Boot engine: should heal tail and accept new writes
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 2u);
        EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(db.GetRecoveryStats().last_valid_offset, valid_size);

        // Next write must cleanly append to valid_size
        EXPECT_TRUE(db.Put("Safe3", "Val3"));
    }

    // Next boot must see all 3 records clean
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(*db.Get("Safe1"), "Val1");
        EXPECT_EQ(*db.Get("Safe2"), "Val2");
        EXPECT_EQ(*db.Get("Safe3"), "Val3");
    }
}

TEST_F(StorageEngineE2ETest, 36_CorruptSingleBytePayloadCheckSumMismatch)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("K1", "V1");
    }

    // Bit-flip payload byte on disk
    {
        int fd = ::open(test_wal.c_str(), O_RDWR);
        ::lseek(fd, sizeof(engine::WalHeader) + 1, SEEK_SET);
        char bad = 'Z';
        ASSERT_EQ(::write(fd, &bad, 1), 1);
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 0u);
}

TEST_F(StorageEngineE2ETest, 37_CorruptedCRC32InHeader)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Key", "Value");
    }

    // Modify CRC bytes in header
    {
        int fd = ::open(test_wal.c_str(), O_RDWR);
        uint32_t fake_crc = 0xDEADBEEF;
        ASSERT_EQ(::write(fd, &fake_crc, sizeof(fake_crc)), sizeof(fake_crc));
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 0u);
}

TEST_F(StorageEngineE2ETest, 38_TruncatedPartialKeyPayload)
{
    uint64_t valid_offset = 0;
    {
        engine::StorageEngine db(test_wal);
        db.Put("P1", "V1");
        valid_offset = db.WalDiskSize();
    }

    // Append a fake header advertising 20-byte key, write only 4 bytes
    {
        engine::WalHeader fake_hdr{};
        fake_hdr.op_type = static_cast<uint8_t>(engine::OpType::kPut);
        fake_hdr.key_len = 20;
        fake_hdr.val_len = 5;
        fake_hdr.crc32 = 9999;

        int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_EQ(::write(fd, &fake_hdr, sizeof(fake_hdr)), sizeof(fake_hdr));
        ASSERT_EQ(::write(fd, "four", 4), 4);
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 1u);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().last_valid_offset, valid_offset);
}

TEST_F(StorageEngineE2ETest, 39_TruncatedPartialValuePayload)
{
    uint64_t valid_offset = 0;
    {
        engine::StorageEngine db(test_wal);
        db.Put("P1", "V1");
        valid_offset = db.WalDiskSize();
    }

    // Append fake header advertising 4-byte key and 30-byte value, write key and 2 value bytes
    {
        engine::WalHeader fake_hdr{};
        fake_hdr.op_type = static_cast<uint8_t>(engine::OpType::kPut);
        fake_hdr.key_len = 4;
        fake_hdr.val_len = 30;
        fake_hdr.crc32 = 8888;

        int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_EQ(::write(fd, &fake_hdr, sizeof(fake_hdr)), sizeof(fake_hdr));
        ASSERT_EQ(::write(fd, "key1", 4), 4);
        ASSERT_EQ(::write(fd, "12", 2), 2);
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 1u);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().last_valid_offset, valid_offset);
}

TEST_F(StorageEngineE2ETest, 40_CorruptedOpTypeByte)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("K1", "V1");
    }

    // Tamper OpType byte from 0x01 to 0xFF
    {
        int fd = ::open(test_wal.c_str(), O_RDWR);
        ::lseek(fd, offsetof(engine::WalHeader, op_type), SEEK_SET);
        uint8_t bad_op = 0xFF;
        ASSERT_EQ(::write(fd, &bad_op, 1), 1);
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    // Checksum mismatch triggers torn-tail detection
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 0u);
}

TEST_F(StorageEngineE2ETest, 41_OnlyGarbageInFileTruncatesToZero)
{
    {
        int fd = ::open(test_wal.c_str(), O_CREAT | O_WRONLY, 0644);
        ASSERT_EQ(::write(fd, "TOTAL_JUNK_DATA_ACROSS_THE_FILE", 31), 31);
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(db.GetRecoveryStats().last_valid_offset, 0u);
    EXPECT_EQ(db.WalDiskSize(), 0u);
}

TEST_F(StorageEngineE2ETest, 42_TornTombstoneRecordPruned)
{
    uint64_t valid_offset = 0;
    {
        engine::StorageEngine db(test_wal);
        db.Put("Key", "Value");
        valid_offset = db.WalDiskSize();
    }

    // Partial delete tombstone append
    {
        engine::WalHeader tomb_hdr{};
        tomb_hdr.op_type = static_cast<uint8_t>(engine::OpType::kDelete);
        tomb_hdr.key_len = 3;
        tomb_hdr.val_len = 0;
        tomb_hdr.crc32 = 1234;

        int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_EQ(::write(fd, &tomb_hdr, sizeof(tomb_hdr)), sizeof(tomb_hdr));
        ASSERT_EQ(::write(fd, "k", 1), 1); // Only 1 of 3 bytes written
        ::close(fd);
    }

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 1u);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    // Original key must remain present
    EXPECT_EQ(*db.Get("Key"), "Value");
}

TEST_F(StorageEngineE2ETest, 43_MultipleTornCyclesWithProgressiveRecovery)
{
    for (int i = 0; i < 3; ++i)
    {
        {
            engine::StorageEngine db(test_wal);
            db.Put("Key_" + std::to_string(i), "Val");
        }
        // Inject corruption at each iteration
        {
            int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
            ASSERT_EQ(::write(fd, "BAD", 3), 3);
            ::close(fd);
        }
    }

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    EXPECT_EQ(*db.Get("Key_0"), "Val");
    EXPECT_EQ(*db.Get("Key_1"), "Val");
    EXPECT_EQ(*db.Get("Key_2"), "Val");
}

TEST_F(StorageEngineE2ETest, 44_TruncationResetsFileDescriptorWriteHeadCleanly)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("A", "1");
    }
    // Sever file
    {
        int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_EQ(::write(fd, "SEVER", 5), 5);
        ::close(fd);
    }
    {
        engine::StorageEngine db(test_wal);
        db.Put("B", "2");
        db.Put("C", "3");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 3u);
        EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(*db.Get("A"), "1");
        EXPECT_EQ(*db.Get("B"), "2");
        EXPECT_EQ(*db.Get("C"), "3");
    }
}

// ============================================================================
// Group 6: Concurrency, Thread Safety & Contention (Tests 45 - 54)
// ============================================================================

TEST_F(StorageEngineE2ETest, 45_ConcurrentReadsDuringSerializedWrites)
{
    engine::StorageEngine db(test_wal);
    db.Put("LiveKey", "0");

    std::atomic<bool> stop{false};

    std::thread reader([&]() {
        while (!stop.load())
        {
            auto val = db.Get("LiveKey");
            ASSERT_TRUE(val.has_value());
        }
    });

    for (int i = 1; i <= 300; ++i)
    {
        EXPECT_TRUE(db.Put("LiveKey", std::to_string(i)));
    }

    stop.store(true);
    reader.join();
}

TEST_F(StorageEngineE2ETest, 46_MultipleConcurrentReadersOneKey)
{
    engine::StorageEngine db(test_wal);
    db.Put("BenchmarkKey", "ReadPayload");

    std::atomic<bool> start{false};
    std::vector<std::thread> readers;

    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&]() {
            while (!start.load()) {}
            for (int i = 0; i < 500; ++i)
            {
                auto val = db.Get("BenchmarkKey");
                ASSERT_TRUE(val.has_value());
                EXPECT_EQ(*val, "ReadPayload");
            }
        });
    }

    start.store(true);
    for (auto& th : readers)
    {
        th.join();
    }
}

TEST_F(StorageEngineE2ETest, 47_MultipleReadersAcrossDisjointKeys)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 20; ++i)
    {
        db.Put("K_" + std::to_string(i), "V_" + std::to_string(i));
    }

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&, t]() {
            for (int i = 0; i < 20; ++i)
            {
                int target = (t * 5 + i) % 20;
                auto val = db.Get("K_" + std::to_string(target));
                ASSERT_TRUE(val.has_value());
                EXPECT_EQ(*val, "V_" + std::to_string(target));
            }
        });
    }

    for (auto& th : readers)
    {
        th.join();
    }
}

TEST_F(StorageEngineE2ETest, 48_ReadersObserveTombstoneInstantaneously)
{
    engine::StorageEngine db(test_wal);
    db.Put("Flicker", "Alive");

    std::atomic<bool> saw_null{false};
    std::atomic<bool> stop{false};

    std::thread reader([&]() {
        while (!stop.load())
        {
            if (!db.Get("Flicker").has_value())
            {
                saw_null.store(true);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    db.Delete("Flicker");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop.store(true);
    reader.join();

    EXPECT_TRUE(saw_null.load());
}

TEST_F(StorageEngineE2ETest, 49_ConcurrentSyncAndReads)
{
    engine::StorageEngine db(test_wal);
    db.Put("SyncK", "SyncV");

    std::atomic<bool> stop{false};
    std::thread syncer([&]() {
        while (!stop.load())
        {
            db.Sync();
        }
    });

    for (int i = 0; i < 300; ++i)
    {
        auto val = db.Get("SyncK");
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, "SyncV");
    }

    stop.store(true);
    syncer.join();
}

TEST_F(StorageEngineE2ETest, 50_HighFrequencyPutsAndDeletesSameKey)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(db.Put("Pulse", "1"));
        EXPECT_TRUE(db.Delete("Pulse"));
    }
    EXPECT_FALSE(db.Get("Pulse").has_value());
}

TEST_F(StorageEngineE2ETest, 51_ConcurrentReaderAgainstContinuousTombstones)
{
    engine::StorageEngine db(test_wal);
    std::atomic<bool> stop{false};

    std::thread reader([&]() {
        while (!stop.load())
        {
            // Point reads should either see a valid value or nullopt, never crash
            auto res = db.Get("ChrunKey");
            if (res.has_value())
            {
                EXPECT_NE(*res, "");
            }
        }
    });

    for (int i = 0; i < 200; ++i)
    {
        db.Put("ChrunKey", "Payload");
        db.Delete("ChrunKey");
    }

    stop.store(true);
    reader.join();
}

TEST_F(StorageEngineE2ETest, 52_KeyMutationPreservesSortedIndexIntegrity)
{
    engine::StorageEngine db(test_wal);
    std::vector<std::string> keys = {"m", "b", "z", "a", "x", "c"};
    for (const auto& k : keys)
    {
        db.Put(k, "val");
    }
    for (const auto& k : keys)
    {
        EXPECT_EQ(*db.Get(k), "val");
    }
}

TEST_F(StorageEngineE2ETest, 53_ConcurrentReadersWithReboots)
{
    {
        engine::StorageEngine db(test_wal);
        for (int i = 0; i < 50; ++i)
        {
            db.Put("K" + std::to_string(i), "V" + std::to_string(i));
        }
    }

    // Multiple reader threads immediately on cold boot
    engine::StorageEngine db(test_wal);
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&, t]() {
            for (int i = 0; i < 50; ++i)
            {
                int idx = (t + i) % 50;
                EXPECT_EQ(*db.Get("K" + std::to_string(idx)), "V" + std::to_string(idx));
            }
        });
    }

    for (auto& th : readers)
    {
        th.join();
    }
}

TEST_F(StorageEngineE2ETest, 54_HeavyInterleavedTombstoneRecovery)
{
    {
        engine::StorageEngine db(test_wal);
        for (int i = 0; i < 100; ++i)
        {
            db.Put("Item_" + std::to_string(i), "Original");
        }
        for (int i = 0; i < 100; i += 3)
        {
            db.Delete("Item_" + std::to_string(i));
        }
    }
    {
        engine::StorageEngine db(test_wal);
        for (int i = 0; i < 100; ++i)
        {
            auto res = db.Get("Item_" + std::to_string(i));
            if (i % 3 == 0)
            {
                EXPECT_FALSE(res.has_value());
            }
            else
            {
                ASSERT_TRUE(res.has_value());
                EXPECT_EQ(*res, "Original");
            }
        }
    }
}

// ============================================================================
// Group 7: Stress Testing, Limits & Complex Replays (Tests 55 - 65)
// ============================================================================

TEST_F(StorageEngineE2ETest, 55_SequentialFiftyThousandBytesPayload)
{
    engine::StorageEngine db(test_wal);
    std::string big_payload(50000, 'M');
    EXPECT_TRUE(db.Put("Large50K", big_payload));
    EXPECT_EQ(*db.Get("Large50K"), big_payload);
}

TEST_F(StorageEngineE2ETest, 56_RapidFiveHundredMutations)
{
    engine::StorageEngine db(test_wal);
    for (int i = 0; i < 500; ++i)
    {
        EXPECT_TRUE(db.Put("BatchKey_" + std::to_string(i), std::to_string(i * 2)));
    }
    for (int i = 0; i < 500; ++i)
    {
        EXPECT_EQ(*db.Get("BatchKey_" + std::to_string(i)), std::to_string(i * 2));
    }
}

TEST_F(StorageEngineE2ETest, 57_FiveHundredMutationsRebootVerification)
{
    {
        engine::StorageEngine db(test_wal);
        for (int i = 0; i < 500; ++i)
        {
            db.Put("Persist_" + std::to_string(i), std::to_string(i));
        }
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 500u);
        for (int i = 0; i < 500; ++i)
        {
            EXPECT_EQ(*db.Get("Persist_" + std::to_string(i)), std::to_string(i));
        }
    }
}

TEST_F(StorageEngineE2ETest, 58_AscendingOverwritesOnSameFiftyKeys)
{
    engine::StorageEngine db(test_wal);
    for (int round = 0; round < 5; ++round)
    {
        for (int i = 0; i < 50; ++i)
        {
            db.Put("Key_" + std::to_string(i), "Round_" + std::to_string(round));
        }
    }
    for (int i = 0; i < 50; ++i)
    {
        EXPECT_EQ(*db.Get("Key_" + std::to_string(i)), "Round_4");
    }
}

TEST_F(StorageEngineE2ETest, 59_RebootAfterAscendingOverwrites)
{
    {
        engine::StorageEngine db(test_wal);
        for (int round = 0; round < 4; ++round)
        {
            for (int i = 0; i < 25; ++i)
            {
                db.Put("Key_" + std::to_string(i), "V_" + std::to_string(round));
            }
        }
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 100u);
        for (int i = 0; i < 25; ++i)
        {
            EXPECT_EQ(*db.Get("Key_" + std::to_string(i)), "V_3");
        }
    }
}

TEST_F(StorageEngineE2ETest, 60_LongKeyLengths)
{
    engine::StorageEngine db(test_wal);
    std::string long_key(1024, 'K');
    EXPECT_TRUE(db.Put(long_key, "ValForLongKey"));
    EXPECT_EQ(*db.Get(long_key), "ValForLongKey");
}

TEST_F(StorageEngineE2ETest, 61_LongKeyRebootSurvival)
{
    std::string long_key(2048, 'P');
    {
        engine::StorageEngine db(test_wal);
        db.Put(long_key, "PersistedLongKeyVal");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(*db.Get(long_key), "PersistedLongKeyVal");
    }
}

TEST_F(StorageEngineE2ETest, 62_ExtremeFragmentedTombstoneReplaySequence)
{
    {
        engine::StorageEngine db(test_wal);
        db.Put("Target", "1");
        db.Delete("Target");
        db.Put("Target", "2");
        db.Delete("Target");
        db.Put("Target", "3");
        db.Delete("Target");
        db.Put("Target", "FinalSurvivingValue");
    }
    {
        engine::StorageEngine db(test_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 7u);
        EXPECT_EQ(*db.Get("Target"), "FinalSurvivingValue");
    }
}

TEST_F(StorageEngineE2ETest, 63_SequentialTornTailHealingOverFiveSessions)
{
    for (int session = 0; session < 5; ++session)
    {
        {
            engine::StorageEngine db(test_wal);
            db.Put("Anchor_" + std::to_string(session), "Safe");
        }
        // Poison file tail
        {
            int fd = ::open(test_wal.c_str(), O_WRONLY | O_APPEND);
            ASSERT_EQ(::write(fd, "GARBAGE_FRAGMENT", 16), 16);
            ::close(fd);
        }
    }

    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 5u);
    EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
    for (int session = 0; session < 5; ++session)
    {
        EXPECT_EQ(*db.Get("Anchor_" + std::to_string(session)), "Safe");
    }
}

TEST_F(StorageEngineE2ETest, 64_HighEntropyPayloadChecksumVerification)
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    std::string entropy_val(1024, '\0');
    for (auto& ch : entropy_val)
    {
        ch = static_cast<char>(dist(rng));
    }

    {
        engine::StorageEngine db(test_wal);
        EXPECT_TRUE(db.Put("EntropyKey", entropy_val));
    }
    {
        engine::StorageEngine db(test_wal);
        auto res = db.Get("EntropyKey");
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(std::string(*res), entropy_val);
    }
}

TEST_F(StorageEngineE2ETest, 65_FullSystemLoopFinalSanityCheck)
{
    {
        engine::StorageEngine db(test_wal);
        for (int i = 0; i < 200; ++i)
        {
            db.Put("Prod_" + std::to_string(i), "Price_" + std::to_string(i * 5));
        }
        for (int i = 0; i < 50; ++i)
        {
            db.Delete("Prod_" + std::to_string(i));
        }
        for (int i = 50; i < 100; ++i)
        {
            db.Put("Prod_" + std::to_string(i), "Discount_" + std::to_string(i));
        }
    }

    // Cold reboot to simulate complete power outage
    engine::StorageEngine db(test_wal);
    EXPECT_EQ(db.GetRecoveryStats().records_replayed, 300u);

    // Assert first 50 were deleted
    for (int i = 0; i < 50; ++i)
    {
        EXPECT_FALSE(db.Get("Prod_" + std::to_string(i)).has_value());
    }
    // Assert 50-99 were updated
    for (int i = 50; i < 100; ++i)
    {
        EXPECT_EQ(*db.Get("Prod_" + std::to_string(i)), "Discount_" + std::to_string(i));
    }
    // Assert 100-199 remained original
    for (int i = 100; i < 200; ++i)
    {
        EXPECT_EQ(*db.Get("Prod_" + std::to_string(i)), "Price_" + std::to_string(i * 5));
    }
}

} // namespace