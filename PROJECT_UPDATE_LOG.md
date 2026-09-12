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

## Update 4: Region_allocator module.

- It was originally known as arena allocator. This file is responsible for allocating memory in chunks and using it later instead of borrowing memory each time a new entry is added to the storage.

- create region_allocator.hpp and region_allocator.cpp to pipeline this whole process and test_region_allocator.cpp to test the working of this new file.

- main.cpp runs these scripts and confirms that they work as intended

### More on region_allocator.hpp

- Declares the Public API: Defines the method signatures (Allocate, AllocateString, TotalAllocatedBytes, TotalChunkMemory) that the rest of the engine (like the SkipList) is allowed to call.  
MD

- Defines Memory Constants: Sets architectural defaults, including the contiguous kDefaultChunkSize (2 MB) and the hardware-enforced boundary kDefaultAlignment (8 bytes).  
MD

- Enforces Safety Constraints: Implements the C++ Rule of Five by deleting copy/move constructors and assignment operators to guarantee memory pools cannot be duplicated or invalidated by pointer aliases.

- Encapsulates Internal State: Declares the private structures and state variables (Chunk, alloc_ptr_, bytes_remaining_, chunks_) without exposing their internal memory layouts to consuming modules.

### More on region_allocator.cpp

- Executes Bump-Pointer Allocation: Advances alloc_ptr_ sequentially to hand out memory in $O(1)$ constant time without invoking standard system allocators (malloc/new).  

- Computes Hardware Alignment: Calculates pointer misalignments via modulo arithmetic (current_addr % 8) and inserts padding bytes so every allocated pointer lands precisely on an 8-byte boundary.  

- Manages Dynamic Chunk Spawning: Tracks remaining bytes in bytes_remaining_ and calls AllocateNewChunk() to acquire fresh contiguous memory blocks whenever a chunk is exhausted.  

- Deep-Copies String Payloads: Implements AllocateString() via std::memcpy to move transient key-value data into persistent, region-owned RAM buffers.  

- Bulk Cleanup (Zero Fragmentation): Ensures that when the allocator destructs, all internal chunks stored in chunks_ are reclaimed simultaneously, avoiding the overhead of freeing records individually.

### More on test_region_allocator.cpp

- Validates Alignment Guarantees: Allocates odd-sized byte requests (e.g., 3, 7, and 13 bytes) and asserts that the resulting memory addresses modulo 8 evaluate strictly to 0.  

- Verifies Data Integrity: Tests that string payloads duplicated via AllocateString() retain their exact contents in memory.  

- Guarantees Memory Independence: Validates that allocated strings point to newly carved heap addresses within the allocator rather than referencing the original literal or caller pointers.

- Command used in terminal: `g++ -std=c++20 -Iinclude src/region_allocator.cpp src/main.cpp -o run_test && ./run_test` which basically says to use c++20 version, add /include in the header to resolve import errors, compiles all 3 files, and runs the linux binary called ./run_test

## Update 5: all about skiplist module

- This module is responsible for two things primarily:

1. It makes that the memory required for any new entry into the RAM is allocated from the region_allocator module and does not directly call a malloc or new method.

2. It maintains a skiplist that has levels to it. Similar to binary search but instead of manually running the algo in logN, skiplist quite literally stores Log N levels that makes the search more efficient.

- The node height is decided by random coin flips which makes sure that the overall balance remains fairly accurate and close to middle. 

### More on skiplist.hpp

- Defines the Boundaries: Sets architectural limits like kMaxHeight = 16 (up to 16 express shortcut ribbons) and a 50% branching probability (kBranchingProbability = 0.5f).

- Declares Public APIs: Exposes simple, non-allocating functions for the outside engine: Put(key, value) to insert/update, and Get(key) returning a `std::optional<std::string_view>`.

- The Tail Ribbon Struct (struct Node): Declares a dynamic node layout using a flexible pointer array member (Node* forward[1]) and calculates contiguous byte sizes via AllocationSize(height).

- Integrates the Allocator & Locks: Binds directly to a RegionAllocator& and establishes a mutable std::shared_mutex for concurrent access.

### More on skiplist.cpp

- Direct Region Memory Carving (CreateNode): Asks RegionAllocator for the exact contiguous bytes needed for a node and its express links, initializes the object via placement new, and deep-copies keys/values into region RAM.

- Probabilistic Leveling (GenerateRandomHeight): Simulates coin flips ($p = 0.5$) to randomly assign express-lane heights to new items, avoiding costly self-balancing tree rotations.

- Express Write Traversal (Put): Traverses from the tallest active express lane down to find splice points. If the key exists, it updates the value in place; if new, it links the node across levels and bumps current_height_ when necessary.

- Fast Read Shortcuts (Get): Uses express-lane pointers to drop through shortcuts in $O(\log N)$ steps, bypassing the disk entirely to return values in nanoseconds.Reader-Writer 

- Concurrency: Wraps writes in std::unique_lock (exclusive access) and reads in std::shared_lock (concurrent shared access).

### More on test_skiplist.cpp

- Point Operations: Confirms that inserting multiple records ("Atta_5kg", "Basmati_Rice", "Tata_Salt") and fetching them returns the exact expected values.

- Missing Record Verification: Asserts that querying a nonexistent key ("NonExistent_Item") safely yields std::nullopt instead of crashing.

- In-Place Update Check: Tests that updating an existing key (changing price from "120" to "119") overwrites the data without dangling pointers or duplicate nodes.

- String Lifetime Protection: Creates temporary std::string variables in an isolated scope { ... }, lets them destruct, and confirms the SkipList still reads the values intact—proving data was copied safely into region-owned RAM.

## Update 6: Why we need CMake for this project

Now that the project size has grown a bit, it is a good time to pause and think about running efficiency. Currently we compile each file manually and run the whole executable, typically consisting of every file names, a lot of nuance flags etc. This is not at all scalable and prone to errors. Moreover, as project grows, compiling each file introduces latency. To overcome this, we use a build tool called `CMake`.

- Enforces Standards & Safety Flags: Configures global compiler rules, enforcing modern C++20, strict warnings (-Wall), hardware optimizations (-march=native), and AddressSanitizer (-fsanitize=address) to trap memory corruption.

- Automates Third-Party Dependencies: Uses FetchContent to download, build, and link GoogleTest directly from GitHub without manual system installations.

- Packages Core Libraries: Bundles internal storage engine files (region_allocator.cpp, skiplist.cpp, etc.) into a static library (engine_core) so application runners and tests can share it cleanly.

- Maps Include Paths: Automatically exposes include/ across the project, preventing missing header errors.

- Enables Incremental, Parallel Builds: Generates build recipes for Ninja, which uses all CPU cores to compile only the specific files that changed.

- Wires Automated Testing: Links unit test runners with GoogleTest and registers them with ctest for batch test execution.

#### What is GoogleTest

- GoogleTest (GTest) is Google’s open-source C++ unit testing framework designed to write, organize, and execute automated test suites without manual test scaffolding.

- Instead of relying on crude assert() checks inside an ad-hoc main() function, GoogleTest isolates tests into modular test suites, provides rich failure reporting (showing expected vs. actual values), and continues running remaining checks even if one fails.

## Update 7: The next step, Write ahead log (WAL)

One of the two main architectural pillars or this project, is WAL. While Skiplist helps in maintaining a highly optimized data structure in RAM, WAL ensures that data is written into the disk to ensure data safety. The core distinction is, instead of writing the whole data into the disk directly, we just append a small log entry so that the data can be recovered anytime.

Example: Let's say we have a new entry put("atta_kg"). Instead of finding the atta_5kg in the full collection of database (which takes logN time in worst case) and updating the number of items from x to x-1 (which is extremely slow compared to RAM since it is a disk write), we just log the atta_5kg entry in a log which even though is a disk operation takes less time. 

### More on wal.hpp

- Enforces On-Disk Packing: Defines WalHeader as an exact 16-byte struct via #pragma pack(push, 1) and static_assert(sizeof(WalHeader) == 16), eliminating hidden compiler padding and establishing an immutable binary wire format.

- Defines Mutation Semantics: Declares enum class OpType : uint8_t (kPut = 0x01, kDelete = 0x02), allowing the engine to distinguish between point updates and tombstone deletions using exactly 1 byte on disk.

- Zero-Copy Public API: Exposes AppendRecord() taking std::string_view parameters, enabling callers to stream keys and values into the persistence pipeline without creating heap temporary copies.

- Durability & Metrics Hooks: Exposes Sync() for manual flushing to non-volatile media and FileSize() returning a 64-bit unsigned integer to track physical growth beyond 4 GB boundaries.

### More on wal.cpp

- Low-Level POSIX File Management: Uses ::open() with O_WRONLY | O_CREAT | O_APPEND (mode 0644) to guarantee that all writes stream sequentially to the tail of the log file at the Linux kernel level.

- Rolling Data Integrity (CRC32): Implements an IEEE 802.3 SoftwareCRC32 checksum algorithm that chains OpType, key/value length prefixes, and raw payload bytes into a single 32-bit checksum to detect torn writes or bit rot.

- Contiguous Memory Assembly: Allocates a contiguous staging buffer (sizeof(WalHeader) + key.size() + value.size()), copies the header followed by raw payload bytes via std::memcpy, and dispatches the entire frame in a single atomic ::write() system call.

- Hardware-Level Durability Guarantee: Issues ::fdatasync() immediately after writing, blocking execution until the drive controller physically etches dirty OS page cache pages onto non-volatile flash storage before acknowledging success.

- RAII Lifecycle & Metadata Querying: Flushes and releases the file descriptor in ~WalWriter() and inspects file size directly via kernel inode metadata using ::fstat()

### More on test_wal.cpp 

- Automates Test Isolation: Leverages SetUp() and TearDown() fixtures via POSIX ::unlink() to ensure no residual files corrupt consecutive test runs.

- Validates Physical Disk Framing: Proves that writes are not merely buffered in memory, but are committed in the exact binary wire layout specified in wal.hpp (16-byte fixed header + variable-length payloads).

- Guarantees Correct Deletion Framing: Confirms that deletion tombstones serialize properly with empty values ($0\text{ bytes}$) without corrupting the on-disk record boundary.

- Validates Low-Level OS Integration: Exercises the live POSIX filesystem pipeline (open, write, fdatasync, fstat, and close) under active AddressSanitizer monitoring.

## Pending Works to do

1. Deep dive into the working of wal and CMake to understand a bit better.

2. Design the recovery module

3. Write an overall orchestrator engine to finish up the MVP for now