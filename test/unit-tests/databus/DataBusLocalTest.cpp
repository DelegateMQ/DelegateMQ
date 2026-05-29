#include "DelegateMQ.h"
#include <iostream>
#include <string>
#include <atomic>
#include <thread>

#if defined(DMQ_DATABUS)

using namespace dmq;
using namespace dmq::os;

int DataBusLocalTestMain() {
    std::cout << "Starting DataBusLocalTest..." << std::endl;
    dmq::databus::DataBus::ResetForTesting();

    // 1. Simple Synchronous Subscription
    {
        bool received = false;
        float receivedValue = 0.0f;
        auto conn = dmq::databus::DataBus::Subscribe<float>("sensor/temp", [&](float value) {
            received = true;
            receivedValue = value;
            std::cout << "Received temp: " << value << std::endl;
        });

        dmq::databus::DataBus::Publish<float>("sensor/temp", 25.5f);
        ASSERT_TRUE(received == true);
        ASSERT_TRUE(receivedValue == 25.5f);
    }

    // 2. Verify ScopedConnection auto-disconnect
    {
        bool received = false;
        {
            auto conn = dmq::databus::DataBus::Subscribe<float>("sensor/temp", [&](float value) {
                (void)value;
                received = true;
            });
        }
        dmq::databus::DataBus::Publish<float>("sensor/temp", 30.0f);
        ASSERT_TRUE(received == false);
    }

    // 3. Multiple subscribers
    {
        int sub1Count = 0;
        int sub2Count = 0;
        auto conn1 = dmq::databus::DataBus::Subscribe<int>("count", [&](int c) { (void)c; sub1Count++; });
        auto conn2 = dmq::databus::DataBus::Subscribe<int>("count", [&](int c) { (void)c; sub2Count++; });

        dmq::databus::DataBus::Publish<int>("count", 1);
        dmq::databus::DataBus::Publish<int>("count", 2);

        ASSERT_TRUE(sub1Count == 2);
        ASSERT_TRUE(sub2Count == 2);
    }

    // 4. Asynchronous Thread Dispatch
    {
        std::cout << "Testing Async Thread Dispatch..." << std::endl;
        Thread workerThread("WorkerThread");
        workerThread.CreateThread();

        std::atomic<bool> asyncReceived{false};
        std::atomic<int> asyncValue{0};
        auto mainThreadId = Thread::GetCurrentThreadId();

        auto conn = dmq::databus::DataBus::Subscribe<int>("async/data", [&](int val) {
            std::cout << "Async callback on thread: " << Thread::GetCurrentThreadId() << std::endl;
            ASSERT_TRUE(Thread::GetCurrentThreadId() != mainThreadId);
            ASSERT_TRUE(Thread::GetCurrentThreadId() == workerThread.GetThreadId());
            asyncValue = val;
            asyncReceived = true;
        }, &workerThread);

        dmq::databus::DataBus::Publish<int>("async/data", 100);

        // Wait for async callback
        int retry = 0;
        const int maxRetries = 100;
        while (!asyncReceived && retry++ < maxRetries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!asyncReceived) {
            std::cerr << "FAILED: Async callback not received within 1 second for topic 'async/data'" << std::endl;
            ASSERT_TRUE(false);
        }
        ASSERT_TRUE(asyncValue == 100);

        workerThread.ExitThread();
    }

    // 5. Different types on different topics
    {
        bool stringReceived = false;
        auto conn = dmq::databus::DataBus::Subscribe<dmq::xstring>("name", [&](dmq::xstring name) {
            stringReceived = (name == "DelegateMQ");
        });
        dmq::databus::DataBus::Publish<dmq::xstring>("name", "DelegateMQ");
        ASSERT_TRUE(stringReceived == true);
    }

    // 6. SubscribeUnhandled — fires when no subscriber exists for a topic
    {
        dmq::databus::DataBus::ResetForTesting();
        dmq::xstring unhandledTopic;
        auto unhandledConn = dmq::databus::DataBus::SubscribeUnhandled([&](const dmq::xstring& topic) {
            unhandledTopic = topic;
        });

        // No subscriber: unhandled fires synchronously
        dmq::databus::DataBus::Publish<int>("ghost/topic", 42);
        ASSERT_TRUE(unhandledTopic == "ghost/topic");

        // With subscriber present: unhandled must NOT fire
        unhandledTopic = "";
        int received = 0;
        auto sub = dmq::databus::DataBus::Subscribe<int>("ghost/topic", [&](int v) { received = v; });
        dmq::databus::DataBus::Publish<int>("ghost/topic", 99);
        ASSERT_TRUE(received == 99);
        ASSERT_TRUE(unhandledTopic.empty());

        // Different unhandled topic alongside a subscribed topic: only the unhandled one fires
        unhandledTopic = "";
        dmq::databus::DataBus::Publish<float>("another/ghost", 1.0f);
        ASSERT_TRUE(unhandledTopic == "another/ghost");
    }

    std::cout << "DataBusLocalTest PASSED!" << std::endl;
    return 0;
}

#else

int DataBusLocalTestMain() {
    return 0;
}

#endif
