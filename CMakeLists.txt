cmake_minimum_required(VERSION 3.25)
project(StorageEngine VERSION 0.1.0 LANGUAGES CXX)

# Enforce Modern C++20 across all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings, hardware tuning, and return diagnostics
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wconversion -Wshadow
    -Werror=return-type
    -march=native
)

# Default to Debug build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# AddressSanitizer to catch buffer overruns and pointer mistakes
option(ENABLE_ASAN "Enable AddressSanitizer" ON)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Make include/ visible to all subdirectories
include_directories(include)

# Download GoogleTest automatically from GitHub
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Direct CMake to subdirectories
add_subdirectory(src)
add_subdirectory(tests)