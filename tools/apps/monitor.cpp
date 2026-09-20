// monitor_main.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Main entry point for the DelegateMQ Node Monitor Console TUI application.
//
// Listens for NodeInfoPacket heartbeats broadcast by NodeBridge instances
// and displays a live table of all active nodes on the DataBus network.

#define NOMINMAX  // Prevent Windows.h from defining min/max macros

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "UdpSocket.h"
#include "NodeInfoPacket.h"
#include "port/serialize/serialize/msg_serialize.h"
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

using namespace ftxui;

// --- Node status thresholds ---
static constexpr int ONLINE_THRESHOLD_S  = 3;
static constexpr int OFFLINE_THRESHOLD_S = 10;

enum class NodeStatus { ONLINE, STALE, OFFLINE };

struct NodeRecord {
    dmq::NodeInfoPacket              packet;
    std::chrono::steady_clock::time_point lastSeen;
};

// --- Global state ---
static std::map<std::string, NodeRecord> g_nodes;
static std::mutex                        g_nodesMutex;
static std::atomic<bool>                 g_running{true};
static std::atomic<uint32_t>             g_packetCount{0};
static std::atomic<uint32_t>             g_errorCount{0};
static std::string                       g_statusMessage = "Waiting for heartbeats...";
static std::string                       g_selectedNodeId;

// --- Helpers ---

// Unique map key: nodeId + IP so two machines with the same node name get separate rows.
static std::string MakeNodeKey(const dmq::NodeInfoPacket& p) {
    return p.nodeId + "@" + p.ipAddress;
}

static NodeStatus GetStatus(const NodeRecord& rec) {
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - rec.lastSeen).count();
    if (age < ONLINE_THRESHOLD_S)  return NodeStatus::ONLINE;
    if (age < OFFLINE_THRESHOLD_S) return NodeStatus::STALE;
    return NodeStatus::OFFLINE;
}

static std::string FormatUptime(uint64_t ms) {
    uint64_t s = ms / 1000;
    uint64_t m = s / 60;
    uint64_t h = m / 60;
    if (h > 0) {
        return std::to_string(h) + "h " + std::to_string(m % 60) + "m";
    }
    if (m > 0) {
        return std::to_string(m) + "m " + std::to_string(s % 60) + "s";
    }
    return std::to_string(s) + "s";
}

static std::string FormatAge(const NodeRecord& rec) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - rec.lastSeen).count();
    if (ms < 1000) return "<1s";
    auto s = ms / 1000;
    if (s < 60) return std::to_string(s) + "s";
    return std::to_string(s / 60) + "m" + std::to_string(s % 60) + "s";
}

// --- Receiver thread ---

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
        g_statusMessage = "Joined Group: " + multicastGroup + " Port: " + std::to_string(port);
    } else {
        g_statusMessage = "Listening on Port: " + std::to_string(port);
    }

    socket.SetReceiveTimeout(100);

    std::vector<uint8_t> buffer(16384);
    serialize ms_decoder;

    while (g_running) {
        int received = socket.Receive(buffer.data(), (int)buffer.size());
        if (received > 0) {
            g_packetCount++;

            if (received < 2 ||
                static_cast<dmq::PacketType>(buffer[0]) != dmq::PacketType::NodeInfo) {
                continue;
            }

            dmq::NodeInfoPacket packet;
            std::string data(reinterpret_cast<char*>(buffer.data() + 1), received - 1);
            std::istringstream iss(data, std::ios::binary);
            ms_decoder.read(iss, packet);

            if (iss.good() && !packet.nodeId.empty()) {
                std::string key = MakeNodeKey(packet);
                std::lock_guard<std::mutex> lock(g_nodesMutex);
                NodeRecord rec;
                rec.packet = std::move(packet);
                rec.lastSeen = std::chrono::steady_clock::now();
                g_nodes[key] = std::move(rec);
            } else {
                g_errorCount++;
                g_statusMessage = "Deserialize error (bytes=" + std::to_string(received) + ")";
            }
        }
    }
}

// --- Usage ---

static void PrintUsage() {
    std::cout << "Usage: dmq-monitor [port] [--multicast <group_addr>] [--interface <addr>]" << std::endl;
    std::cout << "  port: The UDP port to listen on (default: 9998)" << std::endl;
    std::cout << "  --multicast <addr>: Join a multicast group (e.g. 239.1.1.1)" << std::endl;
    std::cout << "  --interface <addr>: The local interface IP to use (default: auto-detect)" << std::endl;
}

// --- Main ---

int main(int argc, char* argv[]) {
    uint16_t    port           = 9998;
    std::string multicastGroup;
    std::string localInterface;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--multicast" && i + 1 < argc) {
            multicastGroup = argv[++i];
        } else if (arg == "--interface" && i + 1 < argc) {
            localInterface = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        } else if (isdigit(arg[0])) {
            port = static_cast<uint16_t>(std::stoi(arg));
        }
    }

    dmq::util::NetworkContext winsock;
    if (localInterface.empty() && !multicastGroup.empty()) {
        localInterface = dmq::util::NetworkContext::GetLocalAddress();
    }

    auto screen = ScreenInteractive::Fullscreen();

    std::thread receiver(ReceiverThread, port, multicastGroup, localInterface);

    // Shared snapshot — written by renderer, read by event handler (both on FTXUI thread).
    std::vector<std::pair<std::string, NodeRecord>> sharedNodeList;

    auto renderer = Renderer([&] {
        // Snapshot node list under lock
        std::vector<std::pair<std::string, NodeRecord>> nodeList;
        {
            std::lock_guard<std::mutex> lock(g_nodesMutex);
            nodeList.assign(g_nodes.begin(), g_nodes.end());
        }
        sharedNodeList = nodeList;

        // Resolve selected index from nodeId so new nodes inserted alphabetically
        // before the current selection don't silently shift focus.
        int selectedIdx = 0;
        bool found = false;
        for (int i = 0; i < (int)nodeList.size(); i++) {
            if (nodeList[i].first == g_selectedNodeId) {
                selectedIdx = i;
                found = true;
                break;
            }
        }
        if (!found && !nodeList.empty())
            g_selectedNodeId = nodeList[0].first;

        // Count statuses
        int nOnline = 0, nStale = 0, nOffline = 0;
        for (auto& [id, rec] : nodeList) {
            switch (GetStatus(rec)) {
                case NodeStatus::ONLINE:  nOnline++;  break;
                case NodeStatus::STALE:   nStale++;   break;
                case NodeStatus::OFFLINE: nOffline++; break;
            }
        }

        // --- Header ---
        std::string title = "DelegateMQ Node Monitor (Port: " + std::to_string(port);
        if (!multicastGroup.empty()) title += ", Group: " + multicastGroup;
        title += ")";

        auto header = vbox({
            text(title) | bold | color(Color::Green) | center,
        });

        std::string summary = " Nodes:  Online: " + std::to_string(nOnline) +
                              "  Stale: "  + std::to_string(nStale) +
                              "  Offline: " + std::to_string(nOffline) +
                              "  |  Heartbeats: " + std::to_string(g_packetCount);

        auto statusColor = g_statusMessage.find("ERROR") != std::string::npos
                               ? Color::Red : Color::Blue;
        auto statusArea = vbox({
            text(" Status: " + g_statusMessage) | color(statusColor),
            text(summary) | color(Color::GrayDark),
        }) | border;

        // --- Column header row ---
        auto colHeader = hbox({
            text(" Node ID")            | bold | size(WIDTH, EQUAL, 22),
            separator(),
            text(" IP Address")         | bold | size(WIDTH, EQUAL, 17),
            separator(),
            text(" Hostname")           | bold | size(WIDTH, EQUAL, 17),
            separator(),
            text(" Status ")            | bold | size(WIDTH, EQUAL, 9),
            separator(),
            text("   Msgs")             | bold | size(WIDTH, EQUAL, 8),
            separator(),
            text(" Uptime ")            | bold | size(WIDTH, EQUAL, 10),
            separator(),
            text(" Last")               | bold | size(WIDTH, EQUAL, 6),
        }) | color(Color::White);

        // --- Node rows ---
        Elements rows;
        rows.push_back(colHeader);
        rows.push_back(separator());

        if (nodeList.empty()) {
            rows.push_back(
                text("  No nodes discovered yet. Is NodeBridge::Start() called in your app?")
                | color(Color::GrayDark)
            );
        }

        for (int i = 0; i < (int)nodeList.size(); i++) {
            auto& [id, rec] = nodeList[i];
            bool     selected = (i == selectedIdx);
            NodeStatus status = GetStatus(rec);

            std::string statusStr;
            Color       statusColor2;
            switch (status) {
                case NodeStatus::ONLINE:  statusStr = " ONLINE";  statusColor2 = Color::Green;  break;
                case NodeStatus::STALE:   statusStr = " STALE";   statusColor2 = Color::Yellow; break;
                case NodeStatus::OFFLINE: statusStr = " OFFLINE"; statusColor2 = Color::Red;    break;
            }

            std::string prefix = selected ? " > " : "   ";
            std::string msgs   = std::to_string(rec.packet.totalMsgCount);

            std::string padding = (msgs.size() < 7) ? std::string(7 - msgs.size(), ' ') : "";

            auto row = hbox({
                text(prefix + rec.packet.nodeId)   | size(WIDTH, EQUAL, 22),
                separator(),
                text(" " + rec.packet.ipAddress)   | size(WIDTH, EQUAL, 17),
                separator(),
                text(" " + rec.packet.hostname)    | size(WIDTH, EQUAL, 17),
                separator(),
                text(statusStr)                    | color(statusColor2) | size(WIDTH, EQUAL, 9),
                separator(),
                text(padding + msgs)               | size(WIDTH, EQUAL, 8),
                separator(),
                text(" " + FormatUptime(rec.packet.uptimeMs)) | size(WIDTH, EQUAL, 10),
                separator(),
                text(" " + FormatAge(rec))         | size(WIDTH, EQUAL, 6),
            });

            if (selected) row = row | inverted;
            rows.push_back(row);
        }

        auto table = vbox(std::move(rows)) | border;

        // --- Topics panel for selected node ---
        Element topicsPanel = text(" No node selected") | color(Color::GrayDark);
        if (!nodeList.empty() && selectedIdx < (int)nodeList.size()) {
            auto& [id, rec] = nodeList[selectedIdx];
            int topicCount = 0;
            std::string display;
            if (rec.packet.topicsStr.empty()) {
                display = "(no topics seen yet)";
            } else {
                // Count semicolons to get topic count, then replace for display
                for (char c : rec.packet.topicsStr) if (c == ';') topicCount++;
                topicCount++;  // one more than the number of separators
                display = rec.packet.topicsStr;
                std::replace(display.begin(), display.end(), ';', ' ');
            }
            std::string label = " Topics for \"" + rec.packet.nodeId + "\" (" +
                                std::to_string(topicCount) + "):";
            topicsPanel = vbox({
                text(label) | color(Color::Yellow),
                text("  " + display) | color(Color::White),
            });
        }
        auto topicsArea = topicsPanel | border;

        // --- Footer ---
        auto footer = hbox({
            text(" [↑↓] Select   [c] Clear Offline   [q] Quit"),
            filler(),
        }) | dim;

        return vbox({
            header,
            statusArea,
            table | flex,
            topicsArea,
            footer,
        }) | flex;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            g_running = false;
            screen.Exit();
            return true;
        }
        if (event == Event::ArrowUp) {
            for (int i = 0; i < (int)sharedNodeList.size(); i++) {
                if (sharedNodeList[i].first == g_selectedNodeId && i > 0) {
                    g_selectedNodeId = sharedNodeList[i - 1].first;
                    break;
                }
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            for (int i = 0; i < (int)sharedNodeList.size(); i++) {
                if (sharedNodeList[i].first == g_selectedNodeId &&
                    i + 1 < (int)sharedNodeList.size()) {
                    g_selectedNodeId = sharedNodeList[i + 1].first;
                    break;
                }
            }
            return true;
        }
        if (event == Event::Character('c')) {
            // Clear offline nodes
            std::lock_guard<std::mutex> lock(g_nodesMutex);
            for (auto it = g_nodes.begin(); it != g_nodes.end(); ) {
                if (GetStatus(it->second) == NodeStatus::OFFLINE) {
                    it = g_nodes.erase(it);
                } else {
                    ++it;
                }
            }
            g_selectedNodeId.clear();
            return true;
        }
        return false;
    });

    // Refresh the TUI periodically
    std::thread refresher([&] {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(component);

    g_running = false;
    if (receiver.joinable()) receiver.join();
    if (refresher.joinable()) refresher.join();

    std::cout << "\r" << std::flush;
    return 0;
}
