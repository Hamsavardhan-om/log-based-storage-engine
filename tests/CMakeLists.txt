include(GoogleTest)

# Compile unit test runner for the allocator
add_executable(test_region_allocator test_region_allocator.cpp)
add_executable(test_wal test_wal.cpp)

# Link the core engine and GoogleTest libraries
target_link_libraries(test_region_allocator PRIVATE engine_core GTest::gtest_main)
target_link_libraries(test_wal PRIVATE engine_core GTest::gtest_main)

# Register with CTest
gtest_discover_tests(test_region_allocator)
gtest_discover_tests(test_wal)