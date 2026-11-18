/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Windows UDP and msg_serialize with remote delegates examples. 
/// 
/// ClientApp and ServerApp communicate using UDP transport, msg_serialize 
/// serialization, and DelegateMQ dispatching.
/// 
/// The ServerApp collects data locally and remotely from sensors and 
/// actuators. Start both applications to run sample. 

#include "NetworkMgr.h"
#include "AlarmMgr.h"
#include "ServerApp.h"
#include <thread>

#ifdef DMQ_LOG
#ifdef _WIN32
// https://github.com/gabime/spdlog
#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>  // MSVC sink
#endif
#endif

std::atomic<bool> processTimerExit = false;
static void ProcessTimers()
{
    while (!processTimerExit.load())
    {
        // Process all delegate-based timers
        dmq::util::Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

int main(int argc, char* argv[])
{
    int duration = 45;
    if (argc > 1) duration = atoi(argv[1]);

#ifdef DMQ_LOG
#ifdef _WIN32
    // Create the MSVC sink (multi-threaded)
    auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    // Optional: Set the log level for the sink
    msvc_sink->set_level(spdlog::level::debug);

    // Create a logger using the MSVC sink
    auto logger = std::make_shared<spdlog::logger>("msvc_logger", msvc_sink);

    // Set as default logger (optional)
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug); // Show debug and above
#endif
#endif

    std::cout << "Server start!" << std::endl;

#ifdef _WIN32
    dmq::util::NetworkContext wsContext;
#endif

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    int err = NetworkMgr::Instance().Create();
    if (err)
        std::cout << "NetworkMgr::Create() error: " << err << std::endl;
    NetworkMgr::Instance().Start();
    AlarmMgr::Instance();
    ServerApp::Instance();

    // Let client and server communicate
    std::this_thread::sleep_for(std::chrono::seconds(duration));

    NetworkMgr::Instance().Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}
