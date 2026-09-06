# Build core engine modules into a reusable static library
add_library(engine_core
    region_allocator.cpp
    skiplist.cpp
)

# Export include/ path to any target linking against engine_core
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR}/include)