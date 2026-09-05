#include "engine/region_allocator.hpp"
#include "engine/skiplist.hpp"

#include <iostream>
#include <cassert>
#include <string>

int main()
{
    engine::RegionAllocator allocator;
    engine::SkipList list(allocator);

    // 1. Point inserts
    list.Put("Atta_5kg", "120");
    list.Put("Basmati_Rice", "250");
    list.Put("Tata_Salt", "30");

    // 2. Fetch point records
    auto val1 = list.Get("Atta_5kg");
    auto val2 = list.Get("Basmati_Rice");
    auto val3 = list.Get("Tata_Salt");
    auto missing = list.Get("NonExistent_Item");

    assert(val1.has_value() && *val1 == "120");
    assert(val2.has_value() && *val2 == "250");
    assert(val3.has_value() && *val3 == "30");
    assert(!missing.has_value());

    // 3. In-place update test
    list.Put("Atta_5kg", "119");
    auto updated_val = list.Get("Atta_5kg");
    assert(updated_val.has_value() && *updated_val == "119");

    // 4. Memory ownership test: pass temporary out-of-scope strings
    {
        std::string temp_key = "Transient_Milk";
        std::string temp_val = "60";
        list.Put(temp_key, temp_val);
    } // temp_key and temp_val fall out of scope and are destroyed here

    auto persistent_val = list.Get("Transient_Milk");
    assert(persistent_val.has_value() && *persistent_val == "60");

    std::cout << "\n>>> ALL SKIPLIST IN-MEMORY TESTS PASSED! <<<\n\n";
    return 0;
}