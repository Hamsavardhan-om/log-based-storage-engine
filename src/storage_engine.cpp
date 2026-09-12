#include "engine/storage_engine.hpp"

namespace engine
{

StorageEngine::StorageEngine(const std::string& wal_path)
    : wal_path_(wal_path),
      allocator_(),
      skiplist_(allocator_)
{
    // 1. Run crash recovery over the specified WAL file.
    // Reconstructs the SkipList in RAM and truncates any torn trailing writes.
    RecoveryEngine recovery(wal_path_);
    recovery_stats_ = recovery.Recover(skiplist_);

    // 2. Open the append-only WAL writer at the verified file tail.
    // Any future operations append directly to this clean boundary.
    wal_ = std::make_unique<WalWriter>(wal_path_);
}

bool StorageEngine::Put(std::string_view key, std::string_view value)
{
    if (!wal_)
    {
        return false;
    }

    // Two-step commit: 
    // Step 1: Append sequentially to disk WAL and enforce hardware persistence.
    if (!wal_->AppendRecord(OpType::kPut, key, value))
    {
        return false;
    }

    // Step 2: Insert into live RAM SkipList once disk persistence is guaranteed.
    skiplist_.Put(key, value);
    return true;
}

std::optional<std::string_view> StorageEngine::Get(std::string_view key) const
{
    // Point reads query the in-memory SkipList in nanoseconds, completely bypassing the SSD.
    auto val = skiplist_.Get(key);

    // Empty value represents a tombstone marker indicating a deleted record.
    if (!val.has_value() || val->empty())
    {
        return std::nullopt;
    }

    return val;
}

bool StorageEngine::Delete(std::string_view key)
{
    if (!wal_)
    {
        return false;
    }

    // Two-step tombstone write:
    // Step 1: Append tombstone marker to disk WAL and flush to flash media.
    if (!wal_->AppendRecord(OpType::kDelete, key, ""))
    {
        return false;
    }

    // Step 2: Write tombstone empty string into RAM SkipList.
    skiplist_.Put(key, "");
    return true;
}

bool StorageEngine::Sync()
{
    if (!wal_)
    {
        return false;
    }
    return wal_->Sync();
}

const RecoveryStats& StorageEngine::GetRecoveryStats() const noexcept
{
    return recovery_stats_;
}

uint64_t StorageEngine::WalDiskSize() const
{
    if (!wal_)
    {
        return 0;
    }
    return wal_->FileSize();
}

} // namespace engine