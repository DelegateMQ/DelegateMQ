/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
/// 
/// @brief C++ server for DelegateMQ cross-language interop demonstration.
/// 
/// @details This application sends sensor data to and receives commands 
/// from C# and Python clients using the DelegateMQ DataBus.

#include "extras/databus/DataBus.h"
#include "port/serialize/msgpack/Serializer.h"
#include "extras/util/NetworkConnect.h"
#include <iostream>
#include <thread>
#include <chrono>

// SensorData is published over multicast so the C# and Python clients can both
// receive it concurrently; Command stays unicast since the server is its only recipient.
#ifdef _WIN32
#include "port/transport/win32-udp/Win32UdpTransport.h"
#include "port/transport/win32-udp/MulticastTransport.h"
using CommandTransportType = dmq::transport::Win32UdpTransport;
#else
#include "port/transport/linux-udp/LinuxUdpTransport.h"
#include "port/transport/linux-udp/MulticastTransport.h"
using CommandTransportType = dmq::transport::LinuxUdpTransport;
#endif
using SensorDataTransportType = dmq::transport::MulticastTransport;

// Must match the C# and Python clients.
static const char* MULTICAST_GROUP = "239.1.1.50";

using namespace dmq::databus;
using namespace dmq::transport;

// 1. Define Data Structures (Must match C# and Python)
struct SensorData {
    int id;
    float value;
    MSGPACK_DEFINE(id, value);
};

struct Command {
    int pollingRateMs;
    MSGPACK_DEFINE(pollingRateMs);
};

// 2. Define Remote IDs (Must match C# and Python)
enum RemoteId : dmq::DelegateRemoteId {
    SensorDataId = 100,
    CommandId = 101
};

int main() {
    dmq::util::NetworkContext netContext;
    std::cout << "Starting C++ Interop Server..." << std::endl;

    std::string localIP = dmq::util::NetworkContext::GetLocalAddress();

    // 3. Setup Transport (multicast PUB on 8000, unicast SUB on 8001)
    auto pubTransport = std::make_unique<SensorDataTransportType>();
    auto subTransport = std::make_unique<CommandTransportType>();

    if (pubTransport->Create(SensorDataTransportType::Type::PUB, MULTICAST_GROUP, 8000, localIP.c_str()) != 0 ||
        subTransport->Create(CommandTransportType::Type::SUB, "", 8001) != 0) {
        std::cerr << "Failed to create transports" << std::endl;
        return -1;
    }

    // Participants in DelegateMQ currently wrap a single ITransport. 
    // We create one for outgoing (Publisher) and one for incoming (Subscriber).
    auto pubParticipant = std::make_shared<Participant>(*pubTransport);
    auto subParticipant = std::make_shared<Participant>(*subTransport);

    DataBus::AddParticipant(pubParticipant);
    // DataBus::AddParticipant(subParticipant); // Only needed if subParticipant had outgoing topics

    // 4. Register Serializers
    static dmq::serialization::msgpack::Serializer<void(SensorData)> sensorSer;
    static dmq::serialization::msgpack::Serializer<void(Command)> cmdSer;

    // Outgoing SensorTopic -> SensorDataId (100) on pubParticipant
    DataBus::RegisterSerializer<SensorData>("SensorTopic", sensorSer);
    pubParticipant->AddRemoteTopic("SensorTopic", SensorDataId);

    // 5. Handle Incoming Commands from C#/Python
    static std::atomic<int> pollingRateMs{ 1000 };
    DataBus::AddIncomingTopic<Command>("CommandTopic", CommandId, *subParticipant, cmdSer);
    
    auto cmdConn = DataBus::Subscribe<Command>("CommandTopic", [](Command cmd) {
        std::cout << "[RECV] Command: pollingRateMs=" << cmd.pollingRateMs << std::endl;
        pollingRateMs = cmd.pollingRateMs;
    });

    // Start a thread to process incoming messages from subTransport
    std::thread recvThread([&subParticipant]() {
        while (true) {
            subParticipant->ProcessIncoming();
        }
    });
    recvThread.detach();

    // 6. Main Loop: Publish sensor data
    int id = 0;
    while (true) {
        SensorData data{ ++id, 25.5f + (float)(id % 10) };
        std::cout << "[SEND] SensorData id=" << data.id << " val=" << data.value << " (Rate: " << pollingRateMs << "ms)" << std::endl;
        DataBus::Publish<SensorData>("SensorTopic", data);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingRateMs.load()));
    }

    return 0;
}
