# Compile unit test runner for the allocator
add_executable(test_region_allocator test_region_allocator.cpp)

# Link the core engine and GoogleTest libraries
target_link_libraries(test_region_allocator PRIVATE engine_core GTest::gtest_main)

# Register with CTest
include(GoogleTest)
gtest_discover_tests(test_region_allocator)