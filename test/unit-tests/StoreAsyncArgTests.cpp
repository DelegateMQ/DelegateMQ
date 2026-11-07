#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>

using namespace dmq;

// store_async_arg<Param>() stores one argument of a non-blocking async call. It
// decides by the declared parameter type Param: by-value parameters are stored
// as-is (no heap copy), reference and pointer parameters get a heap copy that
// heapArgs frees later.
void StoreAsyncArgTests()
{
    LOG_INFO("StoreAsyncArgTests Begin");

    // Test 1: By-value parameter, r-value argument: no heap allocation
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 42;
        int stored = store_async_arg<int>(heapArgs, std::move(val));

        DMQ_ASSERT_TRUE(heapArgs.size() == 0);
        DMQ_ASSERT_TRUE(stored == 42);
    }

    // Test 2: By-value parameter, l-value argument: still no heap allocation
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 100;
        int stored = store_async_arg<int>(heapArgs, val);

        DMQ_ASSERT_TRUE(heapArgs.size() == 0);
        DMQ_ASSERT_TRUE(stored == 100);
    }

    // Test 3: Pointer parameter: heap copy of the pointee
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 42;
        int* pVal = &val;
        int* stored = store_async_arg<int*>(heapArgs, pVal);

        DMQ_ASSERT_TRUE(heapArgs.size() == 1);
        DMQ_ASSERT_TRUE(*stored == 42);
        DMQ_ASSERT_TRUE(stored != pVal); // copied to the heap
    }

    // Test 4: Reference parameter: heap copy, reference to it
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 42;
        int& stored = store_async_arg<int&>(heapArgs, val);

        DMQ_ASSERT_TRUE(heapArgs.size() == 1);
        DMQ_ASSERT_TRUE(stored == 42);
        DMQ_ASSERT_TRUE(&stored != &val); // copied to the heap
    }

    // Test 5: Const reference parameter: heap copy, reference to it
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        const double dVal = 3.14;
        const double& stored = store_async_arg<const double&>(heapArgs, dVal);

        DMQ_ASSERT_TRUE(heapArgs.size() == 1);
        DMQ_ASSERT_TRUE(stored == 3.14);
        DMQ_ASSERT_TRUE(&stored != &dVal);
    }

    // Test 6: Pointer-to-pointer parameter: heap copies of both levels
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 7;
        int* pVal = &val;
        int** ppVal = &pVal;
        int** stored = store_async_arg<int**>(heapArgs, ppVal);

        DMQ_ASSERT_TRUE(heapArgs.size() == 1);
        DMQ_ASSERT_TRUE(**stored == 7);
        DMQ_ASSERT_TRUE(stored != ppVal && *stored != pVal);
    }

    // Test 7: A mixed signature, as DelegateAsyncMsg builds it: only the pointer
    // and reference arguments allocate
    {
        xlist<std::shared_ptr<heap_arg_deleter_base>> heapArgs;
        int val = 42;
        int* pVal = &val;
        double dVal = 3.14;

        std::tuple<int, int*, const int&, double> tup{
            store_async_arg<int>(heapArgs, 42),
            store_async_arg<int*>(heapArgs, pVal),
            store_async_arg<const int&>(heapArgs, val),
            store_async_arg<double>(heapArgs, std::move(dVal)) };

        DMQ_ASSERT_TRUE(heapArgs.size() == 2);
        DMQ_ASSERT_TRUE(std::get<0>(tup) == 42);
        DMQ_ASSERT_TRUE(*std::get<1>(tup) == 42 && std::get<1>(tup) != pVal);
        DMQ_ASSERT_TRUE(std::get<2>(tup) == 42 && &std::get<2>(tup) != &val);
        DMQ_ASSERT_TRUE(std::get<3>(tup) == 3.14);
    }

    std::cout << "StoreAsyncArgTests() complete!" << std::endl;
    LOG_INFO("StoreAsyncArgTests End");
}
