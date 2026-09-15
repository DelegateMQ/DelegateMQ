#include "DelegateMQ.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <atomic>

#if defined(DMQ_DATABUS)

int DataBusBenchmarkTestMain() {
    std::cout << "Starting DataBusBenchmarkTest..." << std::endl;

    const int iterations = 100000;
    dmq::databus::DataBus::ResetForTesting();

    // 1. Local Throughput (1 subscriber)
    int count = 0;
    auto conn = dmq::databus::DataBus::Subscribe<int>("bench", [&](int val) {
        (void)val;
        count++;
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        dmq::databus::DataBus::Publish<int>("bench", i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double> diff = end - start;
    std::cout << "Local Throughput (1 sub): " << iterations / diff.count() << " msg/sec" << std::endl;
    DMQ_ASSERT_TRUE(count == iterations);

    // 2. Local Throughput (10 subscribers)
    count = 0;
    std::vector<dmq::ScopedConnection> conns;
    for (int i = 0; i < 10; ++i) {
        conns.push_back(dmq::databus::DataBus::Subscribe<int>("bench10", [&](int val) {
            (void)val;
            count++;
        }));
    }

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        dmq::databus::DataBus::Publish<int>("bench10", i);
    }
    end = std::chrono::high_resolution_clock::now();
    
    diff = end - start;
    std::cout << "Local Throughput (10 subs): " << (iterations * 10) / diff.count() << " msg/sec" << std::endl;
    DMQ_ASSERT_TRUE(count == iterations * 10);

    std::cout << "DataBusBenchmarkTest PASSED!" << std::endl;
    return 0;
}

#else

int DataBusBenchmarkTestMain() {
    return 0;
}

#endif
