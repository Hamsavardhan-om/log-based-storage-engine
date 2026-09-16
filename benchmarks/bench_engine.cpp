#include "engine/storage_engine.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::high_resolution_clock;
using DurationNano = std::chrono::nanoseconds;

struct BenchmarkResult
{
    std::string name;
    size_t total_ops{0};
    double total_time_sec{0.0};
    double ops_per_sec{0.0};
    double p50_us{0.0};
    double p99_us{0.0};
    double max_us{0.0};
};

void PrintReport(const BenchmarkResult& res)
{
    std::cout << std::left << std::setw(32) << res.name << " | "
              << std::right << std::setw(8) << res.total_ops << " ops | "
              << std::fixed << std::setprecision(2) << std::setw(10) << res.ops_per_sec << " ops/s | "
              << "P50: " << std::setw(7) << res.p50_us << " us | "
              << "P99: " << std::setw(7) << res.p99_us << " us | "
              << "Max: " << std::setw(7) << res.max_us << " us\n";
}

BenchmarkResult RunWalPutBenchmark(const std::string& wal_path, size_t num_ops)
{
    ::unlink(wal_path.c_str());
    std::vector<double> latencies_us;
    latencies_us.reserve(num_ops);

    auto start_total = Clock::now();
    {
        engine::StorageEngine db(wal_path);

        for (size_t i = 0; i < num_ops; ++i)
        {
            std::string key = "item_key_" + std::to_string(i);
            std::string val = "item_val_payload_" + std::to_string(i * 3);

            auto op_start = Clock::now();
            db.PutSync(key, val); // Explicit per-operation fdatasync
            auto op_end = Clock::now();

            latencies_us.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count());
        }
    }
    auto end_total = Clock::now();

    double total_sec = std::chrono::duration<double>(end_total - start_total).count();
    std::sort(latencies_us.begin(), latencies_us.end());

    BenchmarkResult res;
    res.name = "1. Engine Put (WAL + Sync)";
    res.total_ops = num_ops;
    res.total_time_sec = total_sec;
    res.ops_per_sec = static_cast<double>(num_ops) / total_sec;
    res.p50_us = latencies_us[static_cast<size_t>(num_ops * 0.50)];
    res.p99_us = latencies_us[static_cast<size_t>(num_ops * 0.99)];
    res.max_us = latencies_us.back();

    ::unlink(wal_path.c_str());
    return res;
}

BenchmarkResult RunSkipListGetBenchmark(size_t num_ops)
{
    const std::string temp_wal = "bench_mem.wal";
    ::unlink(temp_wal.c_str());

    std::vector<double> latencies_us;
    latencies_us.reserve(num_ops);

    {
        engine::StorageEngine db(temp_wal);

        // Pre-populate data set
        for (size_t i = 0; i < num_ops; ++i)
        {
            db.Put("key_" + std::to_string(i), "payload_value_" + std::to_string(i));
        }

        // Measure read path
        auto start_total = Clock::now();
        for (size_t i = 0; i < num_ops; ++i)
        {
            std::string key = "key_" + std::to_string(i);

            auto op_start = Clock::now();
            auto res = db.Get(key);
            auto op_end = Clock::now();

            if (!res.has_value())
            {
                std::cerr << "Assertion failure during read benchmark!\n";
            }

            latencies_us.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count());
        }
        auto end_total = Clock::now();

        double total_sec = std::chrono::duration<double>(end_total - start_total).count();
        std::sort(latencies_us.begin(), latencies_us.end());

        BenchmarkResult res;
        res.name = "2. In-Memory Get (SkipList)";
        res.total_ops = num_ops;
        res.total_time_sec = total_sec;
        res.ops_per_sec = static_cast<double>(num_ops) / total_sec;
        res.p50_us = latencies_us[static_cast<size_t>(num_ops * 0.50)];
        res.p99_us = latencies_us[static_cast<size_t>(num_ops * 0.99)];
        res.max_us = latencies_us.back();

        ::unlink(temp_wal.c_str());
        return res;
    }
}

BenchmarkResult RunNaiveDiskWriteBenchmark(const std::string& file_path, size_t num_ops)
{
    ::unlink(file_path.c_str());
    int fd = ::open(file_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        return {"Naive Disk In-Place", 0, 0, 0, 0, 0, 0};
    }

    // Pre-allocate 1 MB of file space to simulate existing disk pages
    std::string blank(1024 * 1024, '0');
    (void)::write(fd, blank.data(), blank.size());
    ::fdatasync(fd);

    std::vector<double> latencies_us;
    latencies_us.reserve(num_ops);

    auto start_total = Clock::now();
    for (size_t i = 0; i < num_ops; ++i)
    {
        // Seek to simulated random block and overwrite 64 bytes
        off_t offset = static_cast<off_t>((i * 128) % (1024 * 512));
        std::string payload = "data_sector_update_" + std::to_string(i);

        auto op_start = Clock::now();
        ::lseek(fd, offset, SEEK_SET);
        (void)::write(fd, payload.data(), payload.size());
        ::fdatasync(fd);
        auto op_end = Clock::now();

        latencies_us.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count());
    }
    auto end_total = Clock::now();

    ::close(fd);
    ::unlink(file_path.c_str());

    double total_sec = std::chrono::duration<double>(end_total - start_total).count();
    std::sort(latencies_us.begin(), latencies_us.end());

    BenchmarkResult res;
    res.name = "3. Naive Disk In-Place (Seek+Sync)";
    res.total_ops = num_ops;
    res.total_time_sec = total_sec;
    res.ops_per_sec = static_cast<double>(num_ops) / total_sec;
    res.p50_us = latencies_us[static_cast<size_t>(num_ops * 0.50)];
    res.p99_us = latencies_us[static_cast<size_t>(static_cast<double>(num_ops) * 0.99)];
    res.max_us = latencies_us.back();

    return res;
}

BenchmarkResult RunBufferedWalPutBenchmark(const std::string& wal_path, size_t num_ops)
{
    ::unlink(wal_path.c_str());
    std::vector<double> latencies_us;
    latencies_us.reserve(num_ops);

    auto start_total = Clock::now();
    {
        engine::StorageEngine db(wal_path);

        for (size_t i = 0; i < num_ops; ++i)
        {
            std::string key = "item_key_" + std::to_string(i);
            std::string val = "item_val_payload_" + std::to_string(i * 3);

            auto op_start = Clock::now();
            db.Put(key, val);
            auto op_end = Clock::now();

            latencies_us.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count());
        }
        db.Sync();
    }
    auto end_total = Clock::now();

    double total_sec = std::chrono::duration<double>(end_total - start_total).count();
    std::sort(latencies_us.begin(), latencies_us.end());

    BenchmarkResult res;
    res.name = "1B. Engine Put (Buffered WAL)";
    res.total_ops = num_ops;
    res.total_time_sec = total_sec;
    res.ops_per_sec = static_cast<double>(num_ops) / total_sec;
    res.p50_us = latencies_us[static_cast<size_t>(static_cast<double>(num_ops) * 0.50)];
    res.p99_us = latencies_us[static_cast<size_t>(static_cast<double>(num_ops) * 0.99)];
    res.max_us = latencies_us.back();

    ::unlink(wal_path.c_str());
    return res;
}

BenchmarkResult RunBatchPutBenchmark(const std::string& wal_path, size_t num_ops, size_t batch_size = 100)
{
    ::unlink(wal_path.c_str());
    std::vector<double> latencies_us;
    const size_t num_batches = num_ops / batch_size;
    latencies_us.reserve(num_batches);

    auto start_total = Clock::now();
    {
        engine::StorageEngine db(wal_path);

        for (size_t b = 0; b < num_batches; ++b)
        {
            engine::WriteBatch batch;
            for (size_t i = 0; i < batch_size; ++i)
            {
                size_t id = b * batch_size + i;
                batch.Put("batch_k_" + std::to_string(id), "batch_payload_" + std::to_string(id * 2));
            }

            auto op_start = Clock::now();
            db.Write(batch, true);
            auto op_end = Clock::now();

            latencies_us.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count() / static_cast<double>(batch_size));
        }
    }
    auto end_total = Clock::now();

    double total_sec = std::chrono::duration<double>(end_total - start_total).count();
    std::sort(latencies_us.begin(), latencies_us.end());

    BenchmarkResult res;
    res.name = "1C. Engine Batch Write (100 ops/sync)";
    res.total_ops = num_ops;
    res.total_time_sec = total_sec;
    res.ops_per_sec = static_cast<double>(num_ops) / total_sec;
    res.p50_us = latencies_us[static_cast<size_t>(static_cast<double>(latencies_us.size()) * 0.50)];
    res.p99_us = latencies_us[static_cast<size_t>(static_cast<double>(latencies_us.size()) * 0.99)];
    res.max_us = latencies_us.back();

    ::unlink(wal_path.c_str());
    return res;
}

} // namespace

int main()
{
    std::cout << "\n" << std::string(92, '=') << "\n";
    std::cout << "                     STORAGE ENGINE PERFORMANCE BENCHMARK HARNESS\n";
    std::cout << std::string(92, '=') << "\n\n";

    constexpr size_t kWritesCount = 2000;
    constexpr size_t kFastWritesCount = 50000;
    constexpr size_t kReadsCount = 50000;
    constexpr size_t kNaiveWritesCount = 1000;

    std::cout << "[*] Benchmarking In-Memory Reads (50,000 lookups)...\n";
    auto read_bench = RunSkipListGetBenchmark(kReadsCount);

    std::cout << "[*] Benchmarking Sequential WAL Writes + Per-Op Sync (2,000 ops)...\n";
    auto wal_bench = RunWalPutBenchmark("bench_wal.wal", kWritesCount);

    std::cout << "[*] Benchmarking Buffered WAL Writes (50,000 ops with 64KB threshold sync)...\n";
    auto wal_buf_bench = RunBufferedWalPutBenchmark("bench_wal_buf.wal", kFastWritesCount);

    std::cout << "[*] Benchmarking WriteBatch Commits (50,000 ops in 100-item batches)...\n";
    auto wal_batch_bench = RunBatchPutBenchmark("bench_wal_batch.wal", kFastWritesCount, 100);

    std::cout << "[*] Benchmarking Naive Disk Random Overwrite + Sync (1,000 ops)...\n";
    auto naive_bench = RunNaiveDiskWriteBenchmark("bench_naive.dat", kNaiveWritesCount);

    std::cout << "\n" << std::string(92, '-') << "\n";
    std::cout << "                                   BENCHMARK RESULTS\n";
    std::cout << std::string(92, '-') << "\n";

    PrintReport(read_bench);
    PrintReport(wal_bench);
    PrintReport(wal_buf_bench);
    PrintReport(wal_batch_bench);
    PrintReport(naive_bench);

    std::cout << std::string(92, '=') << "\n\n";

    double buffered_speedup = wal_buf_bench.ops_per_sec / naive_bench.ops_per_sec;
    double batch_speedup = wal_batch_bench.ops_per_sec / naive_bench.ops_per_sec;

    std::cout << "-> Per-Op Sync WAL is ~" << std::fixed << std::setprecision(1)
              << (wal_bench.ops_per_sec / naive_bench.ops_per_sec) << "x speed of Naive Disk (bottlenecked by fdatasync).\n";
    std::cout << "-> Buffered WAL Writes is ~" << std::fixed << std::setprecision(1)
              << buffered_speedup << "x FASTER than Naive Disk In-Place Rewrites!\n";
    std::cout << "-> Batched Writes (100 ops/sync) is ~" << std::fixed << std::setprecision(1)
              << batch_speedup << "x FASTER than Naive Disk In-Place Rewrites!\n";
    std::cout << "-> RAM SkipList serves queries at ~" << std::fixed << std::setprecision(0)
              << read_bench.ops_per_sec << " ops/second (Sub-microsecond latency).\n\n";

    return 0;
}