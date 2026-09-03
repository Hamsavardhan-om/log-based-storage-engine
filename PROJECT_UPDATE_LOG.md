# Log-Based Storage Engine using C++

## About the Project:

few lines

## Update 1: Basic setup

- Installed WSL since I possess windows machine. Setup a new user in linux remote and installed WSL for VS code to make sure that I operate the Ubuntu OS in remote using VS code

- Create basic folder structure

## Update 2: Learning the proposed project details

Here is the proposed architecture:

```json
                        ┌──────────────────────────────┐
                        │      1. User Request         │
                        │    Put(Key, Val) / Get(Key)  │
                        └──────────────┬───────────────┘
                                       │
                        ┌──────────────▼──────────────┐
                        │   2. Storage Engine Facade   │
                        │    (storage_engine.hpp)      │
                        └──────┬───────────────┬───────┘
                               │               │
            ┌──────────────────┘               └──────────────────┐
            ▼ (Write Track)                                       ▼ (Read Track)
┌───────────────────────────────────────┐             ┌────────────────────────────────────┐
│ 3A. Write-Ahead Log (WAL)             │             │ 3B. In-Memory Index (SkipList)     │
│ (wal.hpp / wal.cpp)                   │             │ (skiplist.hpp / skiplist.cpp)      │
│ • Appends raw binary slip to disk     │             │ • Multi-level sorted index in RAM  │
│ • Includes CRC32 tamper-proof seal    │             │ • Instant nanosecond lookups       │
│ • Flushes physically via fdatasync()  │             │ • Completely bypasses disk I/O     │
└──────────────────┬────────────────────┘             └─────────────────▲──────────────────┘
                   │                                                    │
                   │ (Write Receipt Confirmed)                          │ (Stores Nodes)
                   └────────────────────┬───────────────────────────────┘
                                        │
                                        ▼
                      ┌───────────────────────────────────┐
                      │ 4. Custom Arena Allocator         │
                      │ (arena.hpp / arena.cpp)           │
                      │ • Pre-allocates bulk 2 MB chunks  │
                      │ • Slices memory via bump-pointer  │
                      │ • Bypasses standard malloc churn  │
                      └─────────────────┬─────────────────┘
                                        │
                                        ▼ (Startup / Reboot Only)
                      ┌───────────────────────────────────┐
                      │ 5. Crash Recovery Engine          │
                      │ (recovery.hpp / recovery.cpp)     │
                      │ • Locks engine on cold boot       │
                      │ • Replays wal.log from byte zero  │
                      │ • Prunes half-written crash tails │
                      │ • Rebuilds SkipList back into RAM │
                      └───────────────────────────────────┘
                      
```

- Custom arena allocator, borrows quite a chunk of memory upfront instead of doing `malloc` each time a new DB change happens. Analogous to getting a whole notebook from a store rather than borrowing a small sticky note from the shop everytime we have to write something.

- Right after custom allocator, a skiplist is created, which basically serves as fast lookup method instead of linear search. It simulates binary search which keeps the search times logarithmic. 

- "Why Not a Hash Table (std::unordered_map) or a Binary Tree (std::map)?"If a normal list is too slow, why pick a SkipList over common C++ data structures?Why not a Hash Table (std::unordered_map): Hash tables give $O(1)$ lookups, but they are unordered. They cannot do range scans (e.g., "Find all items priced between ₹50 and ₹100", or "List all keys starting with user_"). A SkipList keeps all keys strictly sorted at all times.  Why not a Balanced Binary Tree (std::map / Red-Black Tree): Trees also give $O(log N)$ lookups, but inserting items requires complex tree rotations to stay balanced. Under multi-threaded concurrent access, locking a tree during rotations is complex and creates heavy mutex contention.  Why SkipLists Win Here: SkipLists maintain balance using simple random coin tosses (probabilistic height) rather than tree rotations. This makes them significantly simpler to implement, lock, and synchronize concurrently using reader-writer locks (std::shared_mutex).  

- `recovery.cpp` is the engine's startup safety guard that rebuilds the volatile RAM index from the permanent disk log whenever the database boots or restarts after a crash. **Log Replay:** Sequentially iterates through `wal.log` starting from byte zero. **Integrity Checks:** Computes and verifies the CRC32 checksum for each binary record to ensure transactions were not corrupted. **SkipList Reconstruction:** Dispatches every valid key-value pair directly into the in-memory SkipList using the Arena Allocator, restoring pre-crash state.**Crash Tail Pruning:** Detects incomplete or torn trailing writes caused by sudden power cuts (`kill -9`) and cleanly truncates the broken bytes off the end of the file using POSIX `ftruncate()`.

### System Overview

The engine is an **Embedded In-Memory Key-Value Storage Engine** built in modern C++ that eliminates random disk I/O while guaranteeing crash resilience.

* **The Problem It Solves:** Naive disk databases suffer from high latency because every update requires seeking sectors and reorganizing on-disk pages. Pure RAM caches are fast but lose all data during power loss.


* **The Architecture:** All live read and write operations interact directly with an in-memory **SkipList** index for sub-microsecond responses. Durability is guaranteed by sequentially appending every transaction to an on-disk **Write-Ahead Log (WAL)** before confirming the write.

### End-to-End Operational Lifecycle

| Phase / Operation | Target Module | What Physically Happens |
| --- | --- | --- |
| **1. Read Request (`Get`)** | `skiplist.cpp`<br> | Searches the in-memory SkipList using multi-level express shortcuts in $O(\log N)$ time. **Disk is completely bypassed**.

 |
| **2. Write Request (`Put`)** | `wal.cpp` $\rightarrow$ `arena.cpp` $\rightarrow$ `skiplist.cpp`<br> | **Step 1 (Disk):** Appends a binary transaction record with a CRC32 checksum to `wal.log` and calls `fdatasync()`.

<br>

<br>**Step 2 (RAM):** Carves memory via the Arena bump pointer and inserts the node into the SkipList.

 |
| **3. Memory Allocation** | `arena.cpp`<br> | Allocates bulk 2 MB contiguous chunks upfront. Hands out 64-bit aligned memory via an addition-based bump pointer, bypassing slow `malloc` churn, lock contention, and fragmentation.

 |
| **4. Cold Boot / Recovery** | `recovery.cpp`<br> | Temporarily blocks client traffic. Replays `wal.log` from byte zero, re-verifies CRC32 checksums, truncates torn tail writes via `ftruncate()`, and rebuilds the RAM SkipList.

 |

### Component Summary

* **Storage Engine Facade (`storage_engine.hpp` / `.cpp`):** Exposes clean `Put(k, v)` and `Get(k)` interfaces, routing reads to RAM and executing the two-step safety pipeline for writes.


* **Write-Ahead Log (`wal.hpp` / `.cpp`):** Handles raw sequential binary appending. Uses hardware-accelerated CRC32 checksums and POSIX `fdatasync()` to guarantee durability on SSDs without random seek overhead.


* **In-Memory Index (`skiplist.hpp` / `.cpp`):** A multi-level probabilistic sorted list that scales lookups and range queries to $O(\log N)$ without complex tree rotations. Employs `std::shared_mutex` for concurrent reads.


* **Arena Allocator (`arena.hpp` / `.cpp`):** Manages pre-allocated 2 MB blocks to serve node structures and payloads with $O(1)$ amortized allocations and single-operation bulk destruction.


* **Recovery Engine (`recovery.hpp` / `.cpp`):** Reconstructs the volatile RAM index upon boot, repairing power-cut damage (`kill -9`) by discarding incomplete tail records.

## Update 3: System Architecture Diagram

![alt text](image.png)

