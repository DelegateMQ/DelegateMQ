// dmq::Log(), DMQ_LOG_* and dmq::SetLogHandler() (extras/util/Log.h), and the
// portable sinks (extras/util/LogSinks.h). SpyLogSink is tested in
// databus/DataBusSpyTest.cpp, next to SpyReporter.

#include "DelegateMQ.h"
#include "extras/util/LogSinks.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Captured {
    dmq::LogLevel level = dmq::LogLevel::INF;
    std::string tag;
    std::string msg;
    int calls = 0;
};

Captured& Last() { static Captured c; return c; }

void Capture(dmq::LogLevel level, const char* tag, const char* msg)
{
    Last().level = level;
    Last().tag = tag;
    Last().msg = msg;
    Last().calls++;
}

} // namespace

void LogTests()
{
    const dmq::LogHandler saved = dmq::GetLogHandler();

    // 1. No handler: nothing happens (and nothing is formatted)
    dmq::SetLogHandler(nullptr);
    Last() = {};
    DMQ_LOG_ERR("Test", "dropped %d", 1);
    DMQ_ASSERT_TRUE(Last().calls == 0);

    // 2. A handler gets the level, the tag and the formatted text
    dmq::SetLogHandler(&Capture);
    DMQ_ASSERT_TRUE(dmq::GetLogHandler() == &Capture);
    DMQ_LOG_ERR("Pump", "sensor %d at %s", 7, "inlet");
    DMQ_ASSERT_TRUE(Last().calls == 1);
    DMQ_ASSERT_TRUE(Last().level == dmq::LogLevel::ERR);
    DMQ_ASSERT_TRUE(Last().tag == "Pump");
    DMQ_ASSERT_TRUE(Last().msg == "sensor 7 at inlet");
    DMQ_LOG_WRN("Valve", "plain text");
    DMQ_ASSERT_TRUE(Last().level == dmq::LogLevel::WRN && Last().msg == "plain text");
    DMQ_LOG_INF("Net", "up");
    DMQ_ASSERT_TRUE(Last().level == dmq::LogLevel::INF && Last().calls == 3);

    // 3. DBG is above the default DMQ_LOG_LEVEL (3): compiled out
#if DMQ_LOG_LEVEL < 4
    DMQ_LOG_DBG("Net", "not compiled in");
    DMQ_ASSERT_TRUE(Last().calls == 3);
#endif

    // 4. A nullptr tag becomes ""; a nullptr format logs nothing (called
    //    through a pointer: the compiler's format check rejects nullptr)
    dmq::Log(dmq::LogLevel::INF, nullptr, "x");
    DMQ_ASSERT_TRUE(Last().tag.empty() && Last().msg == "x");
    {
        void (*logFn)(dmq::LogLevel, const char*, const char*, ...) = &dmq::Log;
        const int callsBefore = Last().calls;
        logFn(dmq::LogLevel::INF, "T", nullptr);
        DMQ_ASSERT_TRUE(Last().calls == callsBefore);
    }

    // 5. A line longer than DMQ_LOG_LINE_SIZE is cut there and counted
    const uint32_t truncatedBefore = dmq::GetLogTruncatedCount();
    const std::string longText(dmq::LOG_LINE_SIZE * 2, 'a');
    DMQ_LOG_INF("Long", "%s", longText.c_str());
    DMQ_ASSERT_TRUE(Last().msg.size() == dmq::LOG_LINE_SIZE - 1);
    DMQ_ASSERT_TRUE(dmq::GetLogTruncatedCount() == truncatedBefore + 1);
    const std::string fits(dmq::LOG_LINE_SIZE - 1, 'b');
    DMQ_LOG_INF("Long", "%s", fits.c_str());
    DMQ_ASSERT_TRUE(Last().msg == fits);
    DMQ_ASSERT_TRUE(dmq::GetLogTruncatedCount() == truncatedBefore + 1);

    // 6. Level names
    DMQ_ASSERT_TRUE(std::strcmp(dmq::LogLevelName(dmq::LogLevel::ERR), "ERR") == 0);
    DMQ_ASSERT_TRUE(std::strcmp(dmq::LogLevelName(dmq::LogLevel::WRN), "WRN") == 0);
    DMQ_ASSERT_TRUE(std::strcmp(dmq::LogLevelName(dmq::LogLevel::INF), "INF") == 0);
    DMQ_ASSERT_TRUE(std::strcmp(dmq::LogLevelName(dmq::LogLevel::DBG), "DBG") == 0);

    // 7. StdioSink from several threads at once: whole lines, no fault
    dmq::SetLogHandler(&dmq::log::StdioSink);
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; t++) {
            threads.emplace_back([t] {
                for (int i = 0; i < 3; i++)
                    DMQ_LOG_INF("LogTests", "StdioSink thread %d line %d", t, i);
            });
        }
        for (auto& th : threads)
            th.join();
    }

    dmq::SetLogHandler(saved);
    std::cout << "LogTests PASSED!" << std::endl;
}
