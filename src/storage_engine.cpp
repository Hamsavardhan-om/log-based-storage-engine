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

    // Step 1: Append record to WAL buffer (auto-syncs if threshold is reached)
    if (!wal_->AppendRecord(OpType::kPut, key, value))
    {
        return false;
    }

    // Step 2: Insert into live RAM SkipList
    skiplist_.Put(key, value);
    return true;
}

bool StorageEngine::PutSync(std::string_view key, std::string_view value)
{
    if (!Put(key, value))
    {
        return false;
    }
    return Sync();
}

bool StorageEngine::Write(const WriteBatch& batch, bool sync)
{
    if (!wal_)
    {
        return false;
    }

    if (batch.Empty())
    {
        return true;
    }

    // 1. Pack records for WalWriter
    std::vector<std::pair<OpType, std::pair<std::string_view, std::string_view>>> records;
    records.reserve(batch.Size());

    for (const auto& entry : batch.Entries())
    {
        OpType op = (entry.op == BatchOpType::kPut) ? OpType::kPut : OpType::kDelete;
        records.emplace_back(op, std::make_pair(std::string_view(entry.key), std::string_view(entry.value)));
    }

    // 2. Append batch to WAL buffer and optionally sync
    if (!wal_->AppendBatch(records, sync))
    {
        return false;
    }

    // 3. Apply mutations to in-memory SkipList
    for (const auto& entry : batch.Entries())
    {
        if (entry.op == BatchOpType::kPut)
        {
            skiplist_.Put(entry.key, entry.value);
        }
        else if (entry.op == BatchOpType::kDelete)
        {
            skiplist_.Put(entry.key, "");
        }
    }

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

    // Step 1: Append tombstone marker to disk WAL buffer
    if (!wal_->AppendRecord(OpType::kDelete, key, ""))
    {
        return false;
    }

    // Step 2: Write tombstone empty string into RAM SkipList
    skiplist_.Put(key, "");
    return true;
}

bool StorageEngine::DeleteSync(std::string_view key)
{
    if (!Delete(key))
    {
        return false;
    }
    return Sync();
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