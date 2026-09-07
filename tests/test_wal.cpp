#include <gtest/gtest.h>
#include "engine/wal.hpp"

#include <fstream>
#include <unistd.h>

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

        // Record 1: 16B (header) + 8B (key) + 3B (val) = 27B
        // Record 2: 16B (header) + 12B (key) + 3B (val) = 31B
        // Expected total on-disk footprint = 58 bytes
        EXPECT_EQ(writer.FileSize(), 58);
    }

    // Verify binary layout directly on disk
    std::ifstream file(test_log_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    engine::WalHeader header1;
    file.read(reinterpret_cast<char*>(&header1), sizeof(header1));
    ASSERT_EQ(file.gcount(), sizeof(header1));

    EXPECT_EQ(header1.op_type, static_cast<uint8_t>(engine::OpType::kPut));
    EXPECT_EQ(header1.key_len, 8);
    EXPECT_EQ(header1.val_len, 3);
    EXPECT_NE(header1.crc32, 0u);

    std::string key1(header1.key_len, '\0');
    file.read(key1.data(), header1.key_len);
    EXPECT_EQ(key1, "Atta_5kg");

    std::string val1(header1.val_len, '\0');
    file.read(val1.data(), header1.val_len);
    EXPECT_EQ(val1, "120");
}

TEST_F(WalTest, HandlesTombstoneDeletions)
{
    {
        engine::WalWriter writer(test_log_path);
        // Tombstone operation: DELETE key with empty value
        ASSERT_TRUE(writer.AppendRecord(engine::OpType::kDelete, "Atta_5kg", ""));
        
        // Expected: 16B (header) + 8B (key) + 0B (val) = 24B
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

} // namespace