// main.cpp
// DataBus Shapes Demo Server (Publisher).
//
// Simulates moving shapes and publishes their positions via Multicast.

#include "DelegateMQ.h"
#include "SystemMessages.h"
#include "SystemIds.h"
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <cmath>
#include <atomic>
#include <csignal>

#ifdef DMQ_DATABUS_TOOLS
#include "SpyBridge.h"
#include "NodeBridge.h"
#include <sstream>
#endif

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

// Holds all network and DataBus infrastructure for the server.
struct ServerState {
    dmq::transport::MulticastTransport transport;
    std::shared_ptr<dmq::databus::Participant> group;
    dmq::serialization::serializer::Serializer<void(ShapeMsg)> serializer;
};

// Create the multicast PUB transport.
static bool SetupTransport(ServerState& s, const std::string& localIP) {
    if (s.transport.Create(dmq::transport::MulticastTransport::Type::PUB, "239.1.1.1", 8000, localIP.c_str()) != 0) {
        std::cerr << "Failed to create Multicast transport" << std::endl;
        return false;
    }
    return true;
}

// Register participant and serializers for all shape topics.
static void SetupDataBus(ServerState& s) {
    s.group = std::make_shared<dmq::databus::Participant>(s.transport);
    s.group->AddRemoteTopic(SystemTopic::Square,   SystemTopic::SquareId);
    s.group->AddRemoteTopic(SystemTopic::Circle,   SystemTopic::CircleId);
    s.group->AddRemoteTopic(SystemTopic::Triangle, SystemTopic::TriangleId);
    dmq::databus::DataBus::AddParticipant(s.group);
    dmq::databus::DataBus::RegisterSerializer<ShapeMsg>(SystemTopic::Square,   s.serializer);
    dmq::databus::DataBus::RegisterSerializer<ShapeMsg>(SystemTopic::Circle,   s.serializer);
    dmq::databus::DataBus::RegisterSerializer<ShapeMsg>(SystemTopic::Triangle, s.serializer);
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Shapes Demo SERVER (Publisher)..." << std::endl;

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif
    std::string localIP = dmq::util::NetworkContext::GetLocalAddress();
    std::cout << "Local Interface: " << localIP << std::endl;

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::StartMulticast("239.1.1.1", 9999, localIP);
    NodeBridge::StartMulticast("ShapesServer", "239.1.1.1", 9998, localIP);
    dmq::databus::DataBus::RegisterStringifier<ShapeMsg>(SystemTopic::Square, dmq::MakeDelegate([](const ShapeMsg& m) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << "Square X=" << m.x << " Y=" << m.y;
        return ss.str();
    }));
    dmq::databus::DataBus::RegisterStringifier<ShapeMsg>(SystemTopic::Circle, dmq::MakeDelegate([](const ShapeMsg& m) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << "Circle X=" << m.x << " Y=" << m.y;
        return ss.str();
    }));
#endif

    ServerState s;
    if (!SetupTransport(s, localIP)) return -1;
    SetupDataBus(s);

    if (duration > 0) {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    } else {
        std::cout << "Press Ctrl+C to quit." << std::endl;
    }

    // Animation loop: publish shape positions at ~30fps
    float angle = 0;
    while (g_running) {
        ShapeMsg square;
        square.x = 20 + (int)(20 * std::cos(angle));
        square.y = 20 + (int)(10 * std::sin(angle));
        square.color = 0; // Blue
        dmq::databus::DataBus::Publish<ShapeMsg>(SystemTopic::Square, square);

        ShapeMsg circle;
        circle.x = 50 + (int)(30 * std::sin(angle * 0.5f));
        circle.y = 25 + (int)(15 * std::cos(angle * 0.5f));
        circle.color = 1; // Red
        dmq::databus::DataBus::Publish<ShapeMsg>(SystemTopic::Circle, circle);

        angle += 0.1f;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    std::cout << "\nShutting down..." << std::endl;
    s.transport.Close();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
    NodeBridge::Stop();
#endif

    return 0;
}
