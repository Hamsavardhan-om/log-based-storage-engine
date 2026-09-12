#include <gtest/gtest.h>
#include "engine/storage_engine.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <fcntl.h>
#include <string>
#include <vector>

namespace
{

class CrashDurabilityTest : public ::testing::Test
{
protected:
    const std::string crash_wal = "crash_durability.wal";

    void SetUp() override
    {
        ::unlink(crash_wal.c_str());
    }

    void TearDown() override
    {
        ::unlink(crash_wal.c_str());
    }
};

// Test 1: Simulates immediate hardware power-loss (kill -9) mid-operation.
// Ensures that when RAM vanishes, all writes confirmed prior to the kill
// are rebuilt in the empty SkipList upon reboot.
TEST_F(CrashDurabilityTest, RecoversStateAfterAbruptProcessSIGKILL)
{
    int pipe_fd[2];
    ASSERT_EQ(::pipe(pipe_fd), 0);

    pid_t pid = ::fork();
    ASSERT_GE(pid, 0);

    if (pid == 0)
    {
        // --- CHILD PROCESS (Simulated Database Server) ---
        ::close(pipe_fd[0]); // Close read end

        {
            engine::StorageEngine db(crash_wal);

            // Populate at least 30 confirmed elements into the SkipList & WAL
            for (int i = 0; i < 35; ++i)
            {
                std::string key = "Key_" + std::to_string(i);
                std::string val = "Val_" + std::to_string(i);
                if (!db.Put(key, val))
                {
                    ::_exit(1);
                }
            }

            // Signal parent that 35 items have been committed to disk WAL
            char ack = 'K';
            (void)::write(pipe_fd[1], &ack, 1);
            ::close(pipe_fd[1]);

            // Enter a rapid write loop to guarantee the kill strikes mid-activity
            int extra = 35;
            while (true)
            {
                db.Put("ExtraKey_" + std::to_string(extra), "StreamingData");
                ++extra;
            }
        }

        ::_exit(0);
    }

    // --- PARENT PROCESS (Fault Injector & Watchdog) ---
    ::close(pipe_fd[1]); // Close write end

    // Wait until child signals that at least 30 records are durably written
    char sync_ack = 0;
    ssize_t bytes_read = ::read(pipe_fd[0], &sync_ack, 1);
    ::close(pipe_fd[0]);
    ASSERT_EQ(bytes_read, 1);
    ASSERT_EQ(sync_ack, 'K');

    // Give child a microscopic time slice to be actively writing mid-record
    usleep(1500);

    // Abruptly pull the power cord: send SIGKILL (kill -9).
    // The OS drops the child memory instantly without invoking any C++ destructors.
    ASSERT_EQ(::kill(pid, SIGKILL), 0);

    int status = 0;
    ::waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);

    // --- POST-CRASH RECOVERY REBOOT ---
    // Volatile RAM was completely lost with the child process.
    // Re-open StorageEngine to trigger RecoveryEngine and rebuild the SkipList.
    engine::StorageEngine recovered_db(crash_wal);

    const auto& stats = recovered_db.GetRecoveryStats();

    // Verify at least the 35 pre-crash acknowledged records were restored
    EXPECT_GE(stats.records_replayed, 35u);

    // Assert all 35 confirmed records exist and match their expected values
    for (int i = 0; i < 35; ++i)
    {
        std::string key = "Key_" + std::to_string(i);
        std::string expected_val = "Val_" + std::to_string(i);
        auto val = recovered_db.Get(key);

        ASSERT_TRUE(val.has_value()) << "Missing key after crash recovery: " << key;
        EXPECT_EQ(*val, expected_val);
    }
}

// Test 2: Injects an explicit severed/torn write into the 36th record.
// Asserts that the recovery engine detects the torn write, trims the log back
// to the 35th record boundary, and allows brand new writes to append safely.
TEST_F(CrashDurabilityTest, HealsSeveredMidWriteRecordAndResumesOperations)
{
    uint64_t valid_offset = 0;

    // 1. Commit 35 records normally
    {
        engine::StorageEngine db(crash_wal);
        for (int i = 0; i < 35; ++i)
        {
            ASSERT_TRUE(db.Put("Batch_" + std::to_string(i), "OriginalPayload_" + std::to_string(i)));
        }
        valid_offset = db.WalDiskSize();
    }

    // 2. Simulate catastrophic power cut mid-write of record 36:
    // Append a 16-byte WalHeader claiming 20 bytes of payload, but write only 7 bytes
    {
        engine::WalHeader torn_header{};
        torn_header.op_type = static_cast<uint8_t>(engine::OpType::kPut);
        torn_header.key_len = 10;
        torn_header.val_len = 10;
        torn_header.crc32 = 0x99AABBCC; // Bad/partial CRC

        int fd = ::open(crash_wal.c_str(), O_WRONLY | O_APPEND);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(::write(fd, &torn_header, sizeof(torn_header)), sizeof(torn_header));
        ASSERT_EQ(::write(fd, "SEVERED", 7), 7); // Cut mid-transmission
        ::fdatasync(fd);
        ::close(fd);
    }

    // 3. Cold boot recovery: RAM is empty; SkipList is carved from scratch
    {
        engine::StorageEngine db(crash_wal);

        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 35u);
        EXPECT_TRUE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(db.GetRecoveryStats().last_valid_offset, valid_offset);

        // Verify all 35 pre-crash items survived intact
        for (int i = 0; i < 35; ++i)
        {
            auto res = db.Get("Batch_" + std::to_string(i));
            ASSERT_TRUE(res.has_value());
            EXPECT_EQ(*res, "OriginalPayload_" + std::to_string(i));
        }

        // Verify the severed 36th item is not present
        EXPECT_FALSE(db.Get("SEVERED").has_value());

        // 4. Resume new mutations: engine must append cleanly after truncated tail
        EXPECT_TRUE(db.Put("PostCrashKey", "SafeValue"));
        EXPECT_EQ(*db.Get("PostCrashKey"), "SafeValue");
    }

    // 4. Second reboot to verify log alignment was permanently maintained
    {
        engine::StorageEngine db(crash_wal);
        EXPECT_EQ(db.GetRecoveryStats().records_replayed, 36u);
        EXPECT_FALSE(db.GetRecoveryStats().torn_tail_detected);
        EXPECT_EQ(*db.Get("PostCrashKey"), "SafeValue");
    }
}

} // namespace