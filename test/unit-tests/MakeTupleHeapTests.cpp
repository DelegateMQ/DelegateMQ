#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>

using namespace dmq;

void MakeTupleHeapTests()
{
    LOG_INFO("MakeTupleHeapTests Begin");

    // Test 1: By-value argument (should NOT allocate on heap)
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        std::tuple<> empty_tup;
        int val = 42;
        auto tup = make_tuple_heap(heapArgs, empty_tup, std::move(val));
        
        ASSERT_TRUE(heapArgs.size() == 1); // Heap allocation restored for by-value/r-value
        ASSERT_TRUE(std::get<0>(tup) == 42);
    }

    // Test 2: R-value literal argument (should NOT allocate on heap)
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        std::tuple<> empty_tup;
        auto tup = make_tuple_heap(heapArgs, empty_tup, 100);
        
        ASSERT_TRUE(heapArgs.size() == 1); // Heap allocation restored for r-value
        ASSERT_TRUE(std::get<0>(tup) == 100);
    }

    // Test 3: Pointer argument (SHOULD allocate on heap)
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        std::tuple<> empty_tup;
        int val = 42;
        int* pVal = &val;
        auto tup = make_tuple_heap(heapArgs, empty_tup, pVal);
        
        ASSERT_TRUE(heapArgs.size() == 1); // Heap allocation for pointer
        ASSERT_TRUE(*(std::get<0>(tup)) == 42);
        ASSERT_TRUE(std::get<0>(tup) != pVal); // Address should be different (it was copied to heap)
    }

    // Test 4: Reference argument (SHOULD allocate on heap)
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        std::tuple<> empty_tup;
        int val = 42;
        int& rVal = val;
        auto tup = make_tuple_heap(heapArgs, empty_tup, rVal);
        
        ASSERT_TRUE(heapArgs.size() == 1); // Heap allocation for reference
        ASSERT_TRUE(std::get<0>(tup) == 42);
        ASSERT_TRUE(&(std::get<0>(tup)) != &val); // Address should be different
    }

    // Test 5: Mixed arguments
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        std::tuple<> empty_tup;
        int val = 42;
        int* pVal = &val;
        int& rVal = val;
        double dVal = 3.14;

        auto tup = make_tuple_heap(heapArgs, empty_tup, std::move(val), pVal, rVal, std::move(dVal));

        // val -> 1 allocation
        // pVal -> 1 allocation
        // rVal -> 1 allocation
        // dVal -> 1 allocation
        ASSERT_TRUE(heapArgs.size() == 4); 
        ASSERT_TRUE(std::get<0>(tup) == 42);
        ASSERT_TRUE(*(std::get<1>(tup)) == 42);
        ASSERT_TRUE(std::get<2>(tup) == 42);
        ASSERT_TRUE(std::get<3>(tup) == 3.14);
    }

    std::cout << "MakeTupleHeapTests() complete!" << std::endl;
    LOG_INFO("MakeTupleHeapTests End");
}
