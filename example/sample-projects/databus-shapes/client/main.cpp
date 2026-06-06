// main.cpp
// DataBus Shapes Demo Client (Subscriber).
//
// Receives shape positions via Multicast and renders them in a TUI using FTXUI.
// DeadlineSubscription monitors the Square topic — if no data arrives within
// 1 second the status line turns red; it recovers automatically when data resumes.

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>

#include "DelegateMQ.h"
#include "SystemMessages.h"
#include "SystemIds.h"
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>

#include "extras/util/NetworkConnect.h"

#ifdef DMQ_DATABUS_TOOLS
#include "NodeBridge.h"
#endif

using namespace ftxui;

// State shared between DataBus callbacks and the UI thread.
struct GlobalState {
    std::mutex mutex;
    std::map<std::string, ShapeMsg> shapes;
    std::atomic<bool> serverOnline{false};
    ScreenInteractive* screen = nullptr;
} g_state;

// Holds all network and DataBus infrastructure for the client.
struct ClientState {
    dmq::transport::MulticastTransport transport;
    std::shared_ptr<dmq::databus::Participant> group;
    dmq::serialization::serializer::Serializer<void(ShapeMsg)> serializer;
};

// Create the multicast SUB transport.
static bool SetupTransport(ClientState& s, const std::string& localIP) {
    if (s.transport.Create(dmq::transport::MulticastTransport::Type::SUB, "239.1.1.1", 8000, localIP.c_str()) != 0) {
        std::cerr << "Failed to create Multicast transport" << std::endl;
        return false;
    }
    return true;
}

// Register incoming multicast topics so they flow through the local DataBus.
// Subscribers and DeadlineSubscriptions are set up in main() after this call.
static void SetupDataBus(ClientState& s) {
    s.group = std::make_shared<dmq::databus::Participant>(s.transport);
    dmq::databus::DataBus::AddIncomingTopic<ShapeMsg>(SystemTopic::Square,   SystemTopic::SquareId,   *s.group, s.serializer);
    dmq::databus::DataBus::AddIncomingTopic<ShapeMsg>(SystemTopic::Circle,   SystemTopic::CircleId,   *s.group, s.serializer);
    dmq::databus::DataBus::AddIncomingTopic<ShapeMsg>(SystemTopic::Triangle, SystemTopic::TriangleId, *s.group, s.serializer);
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif
    std::string localIP = dmq::util::NetworkContext::GetLocalAddress();

#ifdef DMQ_DATABUS_TOOLS
    NodeBridge::StartMulticast("ShapesClient", "239.1.1.1", 9998, localIP);
#endif

    ClientState s;
    if (!SetupTransport(s, localIP)) return -1;

    auto screen = ScreenInteractive::Fullscreen();
    g_state.screen = &screen;

    SetupDataBus(s);

    // Subscribe to each shape topic: update render state and mark server online.
    auto onShape = [](const char* topic, const ShapeMsg& msg) {
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            g_state.shapes[topic] = msg;
        }
        g_state.serverOnline = true;
        if (g_state.screen)
            g_state.screen->PostEvent(Event::Custom);
    };

    auto connSquare   = dmq::databus::DataBus::Subscribe<ShapeMsg>(SystemTopic::Square,
        [onShape](const ShapeMsg& m) { onShape(SystemTopic::Square,   m); });
    auto connCircle   = dmq::databus::DataBus::Subscribe<ShapeMsg>(SystemTopic::Circle,
        [onShape](const ShapeMsg& m) { onShape(SystemTopic::Circle,   m); });
    auto connTriangle = dmq::databus::DataBus::Subscribe<ShapeMsg>(SystemTopic::Triangle,
        [onShape](const ShapeMsg& m) { onShape(SystemTopic::Triangle, m); });

    // Deadline monitoring: if Square stops arriving for more than 1 second,
    // mark the server offline and force a UI redraw. Recovers automatically
    // when deliveries resume — no extra wiring needed.
    auto onServerSilent = []() {
        g_state.serverOnline = false;
        if (g_state.screen)
            g_state.screen->PostEvent(Event::Custom);
    };

    dmq::databus::DeadlineSubscription<ShapeMsg> deadlineSquare{
        SystemTopic::Square,
        std::chrono::milliseconds(1000),
        dmq::MakeDelegate([](const ShapeMsg&) {}),  // data handled by connSquare above
        dmq::MakeDelegate(onServerSilent)
    };

    // Background thread: receive multicast data and drive the deadline timer.
    std::atomic<bool> running{true};
    std::thread netThread([&]() {
        while (running) {
            s.group->ProcessIncoming();
            dmq::util::Timer::ProcessTimers();  // drives DeadlineSubscription timer
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // FTXUI render loop
    auto renderer = Renderer([&] {
        auto c = Canvas(200, 100);

        bool online = g_state.serverOnline.load();

        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            for (auto const& [topic, shape] : g_state.shapes) {
                if (topic == SystemTopic::Square) {
                    for (int x = 0; x < 10; ++x)
                        for (int y = 0; y < 10; ++y)
                            c.DrawBlock(shape.x * 2 + x, shape.y * 2 + y, true, Color::Blue);
                } else if (topic == SystemTopic::Circle) {
                    c.DrawBlockCircle(shape.x * 2, shape.y * 2, 10, Color::Red);
                }
            }
        }

        auto statusText = online
            ? text("  \u25cf  Server Online  ") | color(Color::Green) | bold
            : text("  \u25cf  Server Offline \u2014 no data for >1s  ") | color(Color::Red) | bold;

        return vbox({
            text("DelegateMQ DataBus - Multicast Shapes Demo") | bold | color(Color::Green) | center,
            text("Interface: " + localIP + " | Group: 239.1.1.1:8000") | dim | center,
            hbox({ filler(), statusText, filler() }),
            separator(),
            canvas(std::move(c)) | flex | border,
            hbox({
                filler(),
                text("Data-Driven Rendering | 'q' or Ctrl+C to quit") | dim,
                filler()
            })
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('\x03')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    if (duration > 0) {
        std::thread([&screen, duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            screen.Exit();
        }).detach();
    }

    screen.Loop(component);

    running = false;
    netThread.join();
    s.transport.Close();

#ifdef DMQ_DATABUS_TOOLS
    NodeBridge::Stop();
#endif

    std::cout << "\r" << std::flush;

    return 0;
}
