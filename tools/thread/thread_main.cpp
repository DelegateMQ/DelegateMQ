// thread_main.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Main entry point for the DelegateMQ Thread Monitor Console TUI application.

#define NOMINMAX

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "UdpSocket.h"
#include "extras/util/ThreadMonitorSer.h"
#include "extras/util/NetworkConnect.h"

#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <iomanip>

using namespace ftxui;

struct ThreadRecord {
    dmq::util::ThreadStatsPacket packet;
    std::chrono::steady_clock::time_point lastSeen;
    std::string ip;
};

// Global state
static std::map<std::string, ThreadRecord> g_threads;
static std::mutex g_threadsMutex;
static std::atomic<bool> g_running{true};
static std::atomic<uint32_t> g_packetCount{0};
static std::string g_statusMessage = "Waiting for thread stats...";

// Unique key: IP + CPU + Thread
static std::string MakeKey(const dmq::util::ThreadStatsPacket& p, const std::string& ip) {
    return ip + ":" + p.cpu_name + ":" + p.thread_name;
}

static void ReceiverThread(uint16_t port, std::string multicastGroup, std::string localInterface) {
    UdpSocket socket;
    if (!socket.Create()) {
        g_statusMessage = "ERROR: Failed to create socket";
        return;
    }

    if (!socket.Bind(port, "0.0.0.0")) {
        g_statusMessage = "ERROR: Could not bind to port " + std::to_string(port);
        return;
    }

    if (!multicastGroup.empty()) {
        if (!socket.JoinGroup(multicastGroup, localInterface.empty() ? "0.0.0.0" : localInterface)) {
            g_statusMessage = "ERROR: Could not join group " + multicastGroup;
            return;
        }
    }

    socket.SetReceiveTimeout(100);
    std::vector<uint8_t> buffer(16384);
    dmq::util::ThreadStatsPacketSerializer serializer;

    while (g_running) {
        int received = socket.Receive(buffer.data(), (int)buffer.size());
        if (received > 0) {
            std::string senderIp = socket.GetRemoteAddress();
            g_packetCount++;
            dmq::util::ThreadStatsPacket packet;
            std::string data(reinterpret_cast<char*>(buffer.data()), received);
            std::istringstream iss(data, std::ios::binary);
            
            serializer.Read(iss, packet);

            if (!packet.thread_name.empty()) {
                std::string key = MakeKey(packet, senderIp);
                std::lock_guard<std::mutex> lock(g_threadsMutex);
                ThreadRecord& rec = g_threads[key];
                rec.packet = std::move(packet);
                rec.lastSeen = std::chrono::steady_clock::now();
                rec.ip = senderIp;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 9998; // Default DataBus UDP port
    std::string multicastGroup;
    std::string localInterface;

    // Argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--multicast" && i + 1 < argc) {
            multicastGroup = argv[++i];
        } else if (arg == "--interface" && i + 1 < argc) {
            localInterface = argv[++i];
        } else if (std::isdigit(arg[0])) {
            port = (uint16_t)std::stoi(arg);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cout << "Usage: dmq-thread [port] [--multicast <group_addr>] [--interface <addr>]" << std::endl;
            return 1;
        }
    }

    dmq::util::NetworkContext winsock;
    auto screen = ScreenInteractive::Fullscreen();
    std::thread receiver(ReceiverThread, port, multicastGroup, localInterface);

    auto renderer = Renderer([&] {
        std::vector<ThreadRecord> records;
        {
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            for (auto& pair : g_threads) records.push_back(pair.second);
        }

        // Sort by CPU then Thread
        std::sort(records.begin(), records.end(), [](const ThreadRecord& a, const ThreadRecord& b) {
            if (a.packet.cpu_name != b.packet.cpu_name) return a.packet.cpu_name < b.packet.cpu_name;
            return a.packet.thread_name < b.packet.thread_name;
        });

        Elements rows;
        // Header
        rows.push_back(hbox({
            text(" CPU")           | bold | size(WIDTH, EQUAL, 12),
            separator(),
            text(" Thread")        | bold | size(WIDTH, EQUAL, 20),
            separator(),
            text(" IP")            | bold | size(WIDTH, EQUAL, 15),
            separator(),
            text(" QCur")          | bold | size(WIDTH, EQUAL, 6),
            separator(),
            text(" QWin")          | bold | size(WIDTH, EQUAL, 6),
            separator(),
            text(" QMax")          | bold | size(WIDTH, EQUAL, 6),
            separator(),
            text(" LAvg(ms)")      | bold | size(WIDTH, EQUAL, 10),
            separator(),
            text(" LWin(ms)")      | bold | size(WIDTH, EQUAL, 10),
            separator(),
            text(" LMax(ms)")      | bold | size(WIDTH, EQUAL, 10),
        }) | color(Color::White));
        rows.push_back(separator());

        for (const auto& rec : records) {
            auto& p = rec.packet;
            
            auto fmtFloat = [](float f) {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << f;
                return ss.str();
            };

            rows.push_back(hbox({
                text(" " + p.cpu_name)    | size(WIDTH, EQUAL, 12),
                separator(),
                text(" " + p.thread_name) | size(WIDTH, EQUAL, 20),
                separator(),
                text(" " + rec.ip)        | size(WIDTH, EQUAL, 15),
                separator(),
                text(" " + std::to_string(p.queue_depth)) | size(WIDTH, EQUAL, 6),
                separator(),
                text(" " + std::to_string(p.queue_depth_max_window)) | size(WIDTH, EQUAL, 6),
                separator(),
                text(" " + std::to_string(p.queue_depth_max_all)) | size(WIDTH, EQUAL, 6),
                separator(),
                text(" " + fmtFloat(p.latency_avg_ms)) | size(WIDTH, EQUAL, 10),
                separator(),
                text(" " + fmtFloat(p.latency_max_window_ms)) | size(WIDTH, EQUAL, 10),
                separator(),
                text(" " + fmtFloat(p.latency_max_all_ms)) | size(WIDTH, EQUAL, 10),
            }));
        }

        return vbox({
            text("DelegateMQ Thread Monitor") | bold | color(Color::Cyan) | center,
            text(" Packets: " + std::to_string(g_packetCount)) | dim,
            vbox(std::move(rows)) | border,
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    std::thread refresher([&] {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(component);
    g_running = false;
    receiver.join();
    refresher.join();

    return 0;
}
