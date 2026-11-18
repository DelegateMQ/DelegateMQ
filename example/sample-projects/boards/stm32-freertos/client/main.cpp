/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// ClientApp and ServerApp communicate using Win32 UDP socket transport, msg_serialize
/// serialization, and DelegateMQ dispatching. Application runs on Windows. Uses the 
/// libserialport library (https://github.com/sigrokproject/libserialport).
/// See Examples Setup in DETAILS.md for more information about external library setup.
/// 
/// The ServerApp collects data locally and remotely from sensors and 
/// actuators. Start both applications to run sample. 

#include "NetworkMgr.h"
#include "AlarmMgr.h"
#include "DataMgr.h"
#include "ClientApp.h"
#include "extras/util/NetworkConnect.h"
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
    std::cout << "--- Data Received ---" << std::endl;

    std::cout << "Actuators: " << data.actuators.size() << std::endl;
    for (const auto& actuator : data.actuators) {
        // Determine source based on ID convention
        std::string source = (actuator.id <= 2) ? "[STM32]" : "[Local]";

        std::cout << "  " << source
            << " ID: " << actuator.id
            << ", Pos: " << actuator.position
            << ", Volt: " << actuator.voltage << std::endl;
    }

    std::cout << "Sensors: " << data.sensors.size() << std::endl;
    for (const auto& sensor : data.sensors) {
        // Determine source based on ID convention
        std::string source = (sensor.id <= 2) ? "[STM32]" : "[Local]";

        std::cout << "  " << source
            << " ID: " << sensor.id
            << ", Supply: " << sensor.supplyV
            << ", Reading: " << sensor.readingV << std::endl;
    }
}

int main()
{
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

#ifdef _WIN32
    // Starts Winsock now; automatically cleans up when main exits.
    dmq::util::NetworkContext wsContext;
#endif

    std::cout << "Client start!" << std::endl;

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
    dataConn = DataMgr::DataMsgCb.Connect(MakeDelegate(&DataMsgRecv, receiveThread));

    // Start all data collection
    bool success = ClientApp::Instance().Start();
    if (!success)
        std::cout << "ClientApp::Start() failed!" << std::endl;

    // Let client and server communicate
    std::this_thread::sleep_for(std::chrono::seconds(30));

    ClientApp::Instance().Stop();
    NetworkMgr::Instance().Stop();
    receiveThread.ExitThread();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}


