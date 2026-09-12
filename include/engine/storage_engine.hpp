#pragma once

#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"
#include "engine/wal.hpp"
#include "engine/recovery.hpp"

#include <string>
#include <string_view>
#include <optional>
#include <memory>

namespace engine
{

class StorageEngine
{
public:
    // Cold-boot constructor:
    // 1. Instantiates the in-memory arena and SkipList.
    // 2. Runs RecoveryEngine to replay transactions and heal torn crash tails.
    // 3. Opens the append-only WalWriter at the verified file tail.
    explicit StorageEngine(const std::string& wal_path);
    ~StorageEngine() = default;

    // Non-copyable and non-movable to preserve memory stability and unique WAL ownership
    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) = delete;
    StorageEngine& operator=(StorageEngine&&) = delete;

    // Writes an update: commits sequentially to disk WAL, then updates RAM SkipList
    bool Put(std::string_view key, std::string_view value);

    // Reads a value directly from the RAM SkipList without touching the SSD
    [[nodiscard]] std::optional<std::string_view> Get(std::string_view key) const;

    // Deletes an item: writes a tombstone to disk WAL, then tags the RAM index
    bool Delete(std::string_view key);

    // Forces any outstanding operating system file caches onto persistent disk
    bool Sync();

    // Telemetry and status queries
    [[nodiscard]] const RecoveryStats& GetRecoveryStats() const noexcept;
    [[nodiscard]] uint64_t WalDiskSize() const;

private:
    std::string wal_path_;

    // Recovery metrics retained from the initial startup scan
    RecoveryStats recovery_stats_;

    // Volatile in-memory storage layer
    RegionAllocator allocator_;
    SkipList skiplist_;

    // Persistent disk storage layer
    std::unique_ptr<WalWriter> wal_;
};

} // namespace engine