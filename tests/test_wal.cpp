#include <gtest/gtest.h>
#include "engine/wal.hpp"

#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>

namespace
{

class WalTest : public ::testing::Test
{
protected:
    const std::string test_log_path = "test_run.wal";

    void SetUp() override
    {
        ::unlink(test_log_path.c_str());
    }

    void TearDown() override
    {
        ::unlink(test_log_path.c_str());
    }
};

TEST_F(WalTest, AppendsRecordsAndCalculatesFileSize)
{
    {
        engine::WalWriter writer(test_log_path);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Atta_5kg", "120"));
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Basmati_Rice", "250"));
        EXPECT_EQ(writer.FileSize(), 58);
    }

    std::ifstream file(test_log_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    engine::WalHeader header1;
    file.read(reinterpret_cast<char*>(&header1), sizeof(header1));
    ASSERT_EQ(file.gcount(), sizeof(header1));
    EXPECT_EQ(header1.op_type, static_cast<uint8_t>(engine::OpType::kPut));
    EXPECT_EQ(header1.key_len, 8);
    EXPECT_EQ(header1.val_len, 3);
    EXPECT_NE(header1.crc32, 0u);
}

TEST_F(WalTest, HandlesTombstoneDeletions)
{
    {
        engine::WalWriter writer(test_log_path);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kDelete, "Atta_5kg", ""));
        EXPECT_EQ(writer.FileSize(), 24);
    }

    std::ifstream file(test_log_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    engine::WalHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    EXPECT_EQ(header.op_type, static_cast<uint8_t>(engine::OpType::kDelete));
    EXPECT_EQ(header.key_len, 8);
    EXPECT_EQ(header.val_len, 0);
}

TEST_F(WalTest, EmptyLogHasZeroSize)
{
    engine::WalWriter writer(test_log_path);
    EXPECT_EQ(writer.FileSize(), 0u);
}

TEST_F(WalTest, HandlesEmptyKey)
{
    {
        engine::WalWriter writer(test_log_path);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "", "ValOnly"));
        EXPECT_EQ(writer.FileSize(), 16 + 0 + 7);
    }

    std::ifstream file(test_log_path, std::ios::binary);
    engine::WalHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    EXPECT_EQ(header.key_len, 0u);
    EXPECT_EQ(header.val_len, 7u);
}

TEST_F(WalTest, HandlesEmptyValueOnPut)
{
    engine::WalWriter writer(test_log_path);
    ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "KeyOnly", ""));
    EXPECT_EQ(writer.FileSize(), 16 + 7 + 0);
}

TEST_F(WalTest, MultipleConsecutiveAppends)
{
    engine::WalWriter writer(test_log_path);
    uint64_t expected_size = 0;

    for (int i = 0; i < 20; ++i)
    {
        std::string k = "K" + std::to_string(i);
        std::string v = "V" + std::to_string(i);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, k, v));
        expected_size += sizeof(engine::WalHeader) + k.size() + v.size();
    }

    EXPECT_EQ(writer.FileSize(), expected_size);
}

TEST_F(WalTest, ExplicitSyncCallSucceeds)
{
    engine::WalWriter writer(test_log_path);
    ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key", "Value"));
    EXPECT_TRUE(writer.Sync());
}

TEST_F(WalTest, ReopensExistingFileInAppendMode)
{
    {
        engine::WalWriter writer1(test_log_path);
        ASSERT_TRUE(writer1.AppendRecord(engine::OpType::kPut, "Key1", "Val1"));
    }

    {
        engine::WalWriter writer2(test_log_path);
        EXPECT_EQ(writer2.FileSize(), 16 + 4 + 4);
        ASSERT_TRUE(writer2.AppendRecord(engine::OpType::kPut, "Key2", "Val2"));
        EXPECT_EQ(writer2.FileSize(), (16 + 4 + 4) * 2);
    }
}

TEST_F(WalTest, LargePayloadAppend)
{
    std::string big_k(512, 'k');
    std::string big_v(1024 * 32, 'v'); // 32 KB value

    engine::WalWriter writer(test_log_path);
    ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, big_k, big_v));
    EXPECT_EQ(writer.FileSize(), sizeof(engine::WalHeader) + big_k.size() + big_v.size());
}

TEST_F(WalTest, BinaryPayloadIntegrity)
{
    std::string binary_key("null\0key\0test", 13);
    std::string binary_val("val\0ue\0with\0nulls", 17);

    {
        engine::WalWriter writer(test_log_path);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, binary_key, binary_val));
    }

    std::ifstream file(test_log_path, std::ios::binary);
    engine::WalHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    EXPECT_EQ(header.key_len, 13u);
    EXPECT_EQ(header.val_len, 17u);

    std::string read_k(header.key_len, '\0');
    file.read(read_k.data(), header.key_len);
    EXPECT_EQ(read_k, binary_key);

    std::string read_v(header.val_len, '\0');
    file.read(read_v.data(), header.val_len);
    EXPECT_EQ(read_v, binary_val);
}

TEST_F(WalTest, WriteBufferingDefersDiskSyncUntilExplicitSyncOrDestructor)
{
    {
        engine::WalWriter writer(test_log_path);
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kPut, "Key1", "Val1"));
        
        // Logical file size reflects the pending record
        EXPECT_EQ(writer.FileSize(), 16 + 4 + 4);
        EXPECT_GT(writer.UnflushedBytes(), 0u);

        // Explicit flush drains the in-memory buffer
        EXPECT_TRUE(writer.Flush());
        EXPECT_EQ(writer.UnflushedBytes(), 0u);
    }

    // After RAII destructor closes, disk holds data cleanly
    std::ifstream file(test_log_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    engine::WalHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    EXPECT_EQ(file.gcount(), sizeof(header));
    EXPECT_EQ(header.key_len, 4u);
    EXPECT_EQ(header.val_len, 4u);
}

TEST_F(WalTest, BatchAppendCommitsAllRecordsContiguously)
{
    {
        engine::WalWriter writer(test_log_path);
        std::vector<std::pair<engine::OpType, std::pair<std::string_view, std::string_view>>> batch;
        batch.push_back({engine::OpType::kPut, {"batch_k1", "batch_v1"}});
        batch.push_back({engine::OpType::kPut, {"batch_k2", "batch_v2"}});
        batch.push_back({engine::OpType::kDelete, {"batch_k1", ""}});

        ASSERT_TRUE(writer.AppendBatch(batch, true));
        EXPECT_EQ(writer.UnflushedBytes(), 0u);
        EXPECT_EQ(writer.FileSize(), (16 + 8 + 8) + (16 + 8 + 8) + (16 + 8 + 0));
    }

    std::ifstream file(test_log_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    engine::WalHeader h1, h2, h3;
    file.read(reinterpret_cast<char*>(&h1), sizeof(h1));
    file.seekg(h1.key_len + h1.val_len, std::ios::cur);
    file.read(reinterpret_cast<char*>(&h2), sizeof(h2));
    file.seekg(h2.key_len + h2.val_len, std::ios::cur);
    file.read(reinterpret_cast<char*>(&h3), sizeof(h3));

    EXPECT_EQ(h1.op_type, static_cast<uint8_t>(engine::OpType::kPut));
    EXPECT_EQ(h2.op_type, static_cast<uint8_t>(engine::OpType::kPut));
    EXPECT_EQ(h3.op_type, static_cast<uint8_t>(engine::OpType::kDelete));
}

} // namespace