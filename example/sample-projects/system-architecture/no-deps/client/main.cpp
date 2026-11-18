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
#include "DataMgr.h"
#include "ClientApp.h"
#include <thread>

#ifdef DMQ_LOG
#ifdef _WIN32
// https://github.com/gabime/spdlog
#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>  // MSVC sink
#endif
#endif

static dmq::os::Thread receiveThread("ReceiveThread");

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

// Receive all local and remote data callback
void DataMsgRecv(DataMsg& data)
{
    std::cout << "Actuators: " << data.actuators.size() << std::endl;
    for (const auto& actuator : data.actuators) {
        std::cout << "Actuator ID: " << actuator.id
            << ", Position: " << actuator.position
            << ", Voltage: " << actuator.voltage << std::endl;
    }

    std::cout << "Sensors: " << data.sensors.size() << std::endl;
    for (const auto& sensor : data.sensors) {
        std::cout << "Sensor ID: " << sensor.id
            << ", Reading Voltage: " << sensor.readingV << std::endl;
    }
}

int main(int argc, char* argv[])
{
    int duration = 30;
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

    std::cout << "Client start!" << std::endl;

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    receiveThread.CreateThread();

    int err = NetworkMgr::Instance().Create();
    if (err)
        std::cout << "NetworkMgr::Create() error: " << err << std::endl;
    NetworkMgr::Instance().Start();
    AlarmMgr::Instance();
    DataMgr::Instance();
    ClientApp::Instance();

    // 1. Declare a variable to hold the connection. 
    // It MUST remain in scope for as long as you want to receive messages.
    dmq::ScopedConnection dataConn;

    // 2. Connect and store the result
    dataConn = DataMgr::DataMsgCb.Connect(dmq::MakeDelegate(&DataMsgRecv, receiveThread));

    // Start all data collection
    bool success = ClientApp::Instance().Start();
    if (!success)
        std::cout << "ClientApp::Start() failed!" << std::endl;

    // Let client and server communicate
    std::this_thread::sleep_for(std::chrono::seconds(duration));

    ClientApp::Instance().Stop();
    NetworkMgr::Instance().Stop();
    receiveThread.ExitThread();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}
