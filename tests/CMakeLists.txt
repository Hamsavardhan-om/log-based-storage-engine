include(GoogleTest)

# 1. Allocator test suite
add_executable(test_region_allocator test_region_allocator.cpp)
target_link_libraries(test_region_allocator PRIVATE engine_core GTest::gtest_main)
gtest_discover_tests(test_region_allocator)

# 2. SkipList test suite
add_executable(test_skiplist test_skiplist.cpp)
target_link_libraries(test_skiplist PRIVATE engine_core GTest::gtest_main)
gtest_discover_tests(test_skiplist)

# 3. WAL test suite
add_executable(test_wal test_wal.cpp)
target_link_libraries(test_wal PRIVATE engine_core GTest::gtest_main)
gtest_discover_tests(test_wal)

# 4. Recovery engine test suite
add_executable(test_recovery test_recovery.cpp)
target_link_libraries(test_recovery PRIVATE engine_core GTest::gtest_main)
gtest_discover_tests(test_recovery)

# 5. Storage_engine test suite
add_executable(test_storage_engine test_storage_engine.cpp)
target_link_libraries(test_storage_engine PRIVATE engine_core GTest::gtest_main)
gtest_discover_tests(test_storage_engine)