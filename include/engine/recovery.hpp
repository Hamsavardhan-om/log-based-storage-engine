#pragma once

#include "engine/wal.hpp"
#include "engine/skiplist.hpp"

#include <cstdint>
#include <string>

namespace engine
{

// Telemetry returned to the caller upon finishing a log scan. It denotes the state of the WAL without us needing to inspect the file directly.records_replayed shows the number of valid insertions inside ths skiplist. torn_tail_detected is set to true if CRC mismatch or data loss during powercut. Stores the exact byte boundary upto which all records were clean and valid. 
struct RecoveryStats
{
    size_t records_replayed{0};
    bool torn_tail_detected{false};
    uint64_t last_valid_offset{0};
};

// The data structure responsile for the whole recovery engine execution. Contains a public method Recover which serves as the entry point of this engine. accepts a skiplist and modifies it in place and then returns the stats. 
class RecoveryEngine
{
public:
    explicit RecoveryEngine(const std::string& wal_path);
    ~RecoveryEngine() = default;

    RecoveryEngine(const RecoveryEngine&) = delete;
    RecoveryEngine& operator=(const RecoveryEngine&) = delete;
    RecoveryEngine(RecoveryEngine&&) = delete;
    RecoveryEngine& operator=(RecoveryEngine&&) = delete;

    // Scans wal.log from offset 0, verifies CRC32 checksums,
    // re-inserts transactions into the SkipList, and truncates torn tail writes.
    RecoveryStats Recover(SkipList& skiplist);

private:
    uint32_t ComputeCRC32(OpType op, const std::string& key, const std::string& value) const;

    std::string wal_path_;
};

}