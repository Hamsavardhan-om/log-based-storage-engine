#include "engine/region_allocator.hpp"
#include <iostream>
#include <cassert>

int main()
{
    engine::RegionAllocator allocator(1024 * 1024);

    // 1. Test 8-byte alignment
    void* p1 = allocator.Allocate(3);
    void* p2 = allocator.Allocate(7);
    void* p3 = allocator.Allocate(13);

    assert(reinterpret_cast<uintptr_t>(p1) % 8 == 0);
    assert(reinterpret_cast<uintptr_t>(p2) % 8 == 0);
    assert(reinterpret_cast<uintptr_t>(p3) % 8 == 0);

    // 2. Test string persistence
    std::string_view key = allocator.AllocateString("Atta_5kg");
    std::string_view val = allocator.AllocateString("99");

    assert(key == "Atta_5kg");
    assert(val == "99");

    std::cout << "\n>>> ALL REGION ALLOCATOR TESTS PASSED! <<<\n\n";
    return 0;
}