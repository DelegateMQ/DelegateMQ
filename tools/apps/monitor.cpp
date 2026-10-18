// monitor_main.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Main entry point for the DelegateMQ Node Monitor Console TUI application.
//
// Listens for NodeInfoPacket heartbeats and NodeHealthPacket health counters
// broadcast by NodeBridge instances and displays a live table of all active
// nodes on the DataBus network.

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
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <optional>
#include <tuple>

using namespace ftxui;
using Clock = std::chrono::steady_clock;

// --- Node status thresholds ---
static constexpr int ONLINE_THRESHOLD_S  = 3;
static constexpr int OFFLINE_THRESHOLD_S = 10;

// --- Health: link problems in the last HEALTH_WINDOW_S seconds set WARN/FAIL ---
static constexpr int HEALTH_WINDOW_S = 10;

enum class NodeStatus { ONLINE, STALE, OFFLINE };
enum class HealthLevel { NONE, OK, WARN, FAIL };

struct NodeRecord {
    dmq::NodeInfoPacket packet;
    Clock::time_point   lastSeen;
};

/// The health counters as one value, so totals, baselines and window deltas
/// can be subtracted.
struct HealthCounts {
    uint32_t dataBusErrors = 0;
    uint32_t sendTimeouts = 0;
    uint32_t deliveryFailures = 0;
    uint32_t capExceeded = 0;
    uint32_t pendingExceeded = 0;

    static HealthCounts From(const dmq::NodeHealthPacket& p) {
        return { p.dataBusErrors, p.sendTimeouts, p.deliveryFailures, p.capExceeded, p.pendingExceeded };
    }
    HealthCounts operator-(const HealthCounts& o) const {
        return { dataBusErrors - o.dataBusErrors, sendTimeouts - o.sendTimeouts,
                 deliveryFailures - o.deliveryFailures, capExceeded - o.capExceeded,
                 pendingExceeded - o.pendingExceeded };
    }
    bool Any() const {
        return dataBusErrors || sendTimeouts || deliveryFailures || capExceeded || pendingExceeded;
    }
    bool AnyBelow(const HealthCounts& o) const {
        return dataBusErrors < o.dataBusErrors || sendTimeouts < o.sendTimeouts ||
               deliveryFailures < o.deliveryFailures || capExceeded < o.capExceeded ||
               pendingExceeded < o.pendingExceeded;
    }
};

struct HealthRecord {
    dmq::NodeHealthPacket latest;
    HealthCounts baseline;   ///< Totals at the last [r] reset (or node restart)
    std::deque<std::pair<Clock::time_point, HealthCounts>> samples;  ///< Recent totals, for the window
};

// --- Global state ---
static std::map<std::string, NodeRecord>   g_nodes;
static std::map<std::string, HealthRecord> g_health;     // same keys as g_nodes; guarded by g_nodesMutex
static std::mutex                          g_nodesMutex;
static std::atomic<bool>                   g_running{true};
static std::atomic<uint32_t>               g_packetCount{0};
static std::atomic<uint32_t>               g_errorCount{0};
static std::string                         g_statusMessage = "Waiting for heartbeats...";  // guarded by g_nodesMutex
static std::string                         g_selectedNodeId;

// --- Helpers ---

// The receiver thread writes the status line and the UI thread reads it.
static void SetStatusMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_nodesMutex);
    g_statusMessage = msg;
}

static std::string GetStatusMessage() {
    std::lock_guard<std::mutex> lock(g_nodesMutex);
    return g_statusMessage;
}

// Unique map key: nodeId + IP so two machines with the same node name get separate rows.
static std::string MakeNodeKey(const std::string& nodeId, const std::string& ipAddress) {
    return nodeId + "@" + ipAddress;
}

static NodeStatus GetStatus(const NodeRecord& rec) {
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now() - rec.lastSeen).count();
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
        Clock::now() - rec.lastSeen).count();
    if (ms < 1000) return "<1s";
    auto s = ms / 1000;
    if (s < 60) return std::to_string(s) + "s";
    return std::to_string(s / 60) + "m" + std::to_string(s % 60) + "s";
}

/// Drop samples older than the window, keeping one at or before its start so
/// the delta covers the whole window. Once packets stop, every sample ages out
/// except the newest, and the recent counts fall to zero.
static void PruneSamples(HealthRecord& h, Clock::time_point now) {
    const auto windowStart = now - std::chrono::seconds(HEALTH_WINDOW_S);
    while (h.samples.size() > 1 && h.samples[1].first <= windowStart)
        h.samples.pop_front();
}

/// Counter increases over the last HEALTH_WINDOW_S seconds.
static HealthCounts RecentCounts(const HealthRecord& h) {
    if (h.samples.empty()) return {};
    return HealthCounts::From(h.latest) - h.samples.front().second;
}

/// FAIL: messages lost or rejected recently. WARN: retries or a growing backlog
/// recently, or any DataBus error since the last reset (DataBus errors are
/// usually reported once per topic and error, so they stay visible). OK: none.
static HealthLevel GetHealth(const HealthRecord* h) {
    if (h == nullptr) return HealthLevel::NONE;
    HealthCounts recent = RecentCounts(*h);
    HealthCounts sinceReset = HealthCounts::From(h->latest) - h->baseline;
    if (recent.deliveryFailures > 0 || recent.capExceeded > 0) return HealthLevel::FAIL;
    if (recent.sendTimeouts > 0 || recent.pendingExceeded > 0 || sinceReset.dataBusErrors > 0)
        return HealthLevel::WARN;
    return HealthLevel::OK;
}

static void HealthText(HealthLevel level, std::string& str, Color& col) {
    switch (level) {
        case HealthLevel::NONE: str = " -";    col = Color::GrayDark; break;
        case HealthLevel::OK:   str = " OK";   col = Color::Green;    break;
        case HealthLevel::WARN: str = " WARN"; col = Color::Yellow;   break;
        case HealthLevel::FAIL: str = " FAIL"; col = Color::Red;      break;
    }
}

/// Reset every node's health baseline to its current totals ([r] key).
static void ResetHealthBaselines() {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_nodesMutex);
    for (auto& [key, h] : g_health) {
        h.baseline = HealthCounts::From(h.latest);
        // Restart the window too, so pre-reset events don't count as recent.
        h.samples.clear();
        h.samples.emplace_back(now, h.baseline);
    }
}

// --- Receiver thread ---

template <typename Packet>
static bool Decode(const std::vector<uint8_t>& buffer, int received, Packet& packet) {
    serialize ms;
    std::string data(reinterpret_cast<const char*>(buffer.data() + 1), received - 1);
    std::istringstream iss(data, std::ios::binary);
    ms.read(iss, packet);
    return iss.good() && !packet.nodeId.empty();
}

static void ReceiverThread(UdpSocket& socket) {
    socket.SetReceiveTimeout(100);

    std::vector<uint8_t> buffer(16384);

    while (g_running) {
        int received = socket.Receive(buffer.data(), (int)buffer.size());
        if (received < 2) continue;

        const auto type = static_cast<dmq::PacketType>(buffer[0]);
        if (type == dmq::PacketType::NodeInfo) {
            g_packetCount++;
            dmq::NodeInfoPacket packet;
            if (Decode(buffer, received, packet)) {
                std::string key = MakeNodeKey(packet.nodeId, packet.ipAddress);
                std::lock_guard<std::mutex> lock(g_nodesMutex);
                NodeRecord& rec = g_nodes[key];
                rec.packet = std::move(packet);
                rec.lastSeen = Clock::now();
            } else {
                g_errorCount++;
                SetStatusMessage("ERROR: cannot decode heartbeat (bytes=" + std::to_string(received) + ")");
            }
        } else if (type == dmq::PacketType::Health) {
            dmq::NodeHealthPacket packet;
            if (Decode(buffer, received, packet)) {
                std::string key = MakeNodeKey(packet.nodeId, packet.ipAddress);
                const auto now = Clock::now();
                const HealthCounts totals = HealthCounts::From(packet);
                std::lock_guard<std::mutex> lock(g_nodesMutex);
                HealthRecord& h = g_health[key];
                // A counter going down means the node restarted: start over.
                if (!h.samples.empty() && totals.AnyBelow(HealthCounts::From(h.latest))) {
                    h = HealthRecord{};
                }
                h.latest = std::move(packet);
                h.samples.emplace_back(now, totals);
                PruneSamples(h, now);
            } else {
                g_errorCount++;
                SetStatusMessage("ERROR: cannot decode health packet (bytes=" + std::to_string(received) + ")");
            }
        }
    }
}

// --- View ---

using NodeList = std::vector<std::tuple<std::string, NodeRecord, std::optional<HealthRecord>>>;

static NodeList SnapshotNodes() {
    NodeList list;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_nodesMutex);
    for (auto& [key, rec] : g_nodes) {
        auto it = g_health.find(key);
        std::optional<HealthRecord> health;
        if (it != g_health.end()) {
            health = it->second;
            PruneSamples(*health, now);   // the window moves on even when packets stop
        }
        list.emplace_back(key, rec, std::move(health));
    }
    return list;
}

static Element HealthPanel(const NodeRecord& rec, const std::optional<HealthRecord>& health) {
    std::string label = " Health for \"" + rec.packet.nodeId + "\": ";
    if (!health) {
        return vbox({
            text(label + "not reported") | color(Color::Yellow),
            text("  This node's NodeBridge predates health reporting.") | color(Color::GrayDark),
        });
    }

    const auto& h = *health;
    std::string levelStr;
    Color levelCol;
    HealthText(GetHealth(&h), levelStr, levelCol);

    const HealthCounts total  = HealthCounts::From(h.latest) - h.baseline;
    const HealthCounts recent = RecentCounts(h);
    auto counter = [](const std::string& name, uint32_t t, uint32_t r) {
        std::string s = name + " " + std::to_string(t);
        if (r > 0) s += " (+" + std::to_string(r) + ")";
        return s;
    };

    const bool linkWatched = (h.latest.sources & (dmq::NodeHealthPacket::SRC_TRANSPORT |
                                                  dmq::NodeHealthPacket::SRC_RETRY)) != 0;
    Elements lines;
    lines.push_back(hbox({ text(label) | color(Color::Yellow), text(levelStr) | bold | color(levelCol) }));
    std::string counts = "  " + counter("DataBus errors", total.dataBusErrors, recent.dataBusErrors);
    if (linkWatched) {
        counts += "   " + counter("ACK timeouts", total.sendTimeouts, recent.sendTimeouts) +
                  "   " + counter("Delivery failed", total.deliveryFailures, recent.deliveryFailures) +
                  "   " + counter("Rejected", total.capExceeded, recent.capExceeded) +
                  "   " + counter("Backlog", total.pendingExceeded, recent.pendingExceeded);
    }
    lines.push_back(text(counts) | color(Color::White));
    if (!linkWatched) {
        lines.push_back(text("  Link health not watched: call NodeBridge::Watch() with the node's "
                             "NetworkNode or TransportMonitor/RetryMonitor.") | color(Color::GrayDark));
    }
    const std::string& last = h.latest.lastEvent;
    if (!last.empty() && total.Any()) {
        lines.push_back(text("  Last: " + last) | color(Color::GrayLight));
    }
    lines.push_back(text("  (+n) = in the last " + std::to_string(HEALTH_WINDOW_S) +
                         " s; totals since start or [r] reset") | color(Color::GrayDark));
    return vbox(std::move(lines));
}

/// Topics and health for one node.
static Element DetailPanel(const NodeRecord& rec, const std::optional<HealthRecord>& health) {
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
    return vbox({
        text(label) | color(Color::Yellow),
        paragraph("  " + display) | color(Color::White),
        separator(),
        HealthPanel(rec, health),
    }) | border;
}

/// The whole screen except the footer. Shared by the interactive UI and --once.
/// allDetails: show every node's detail panel (--once) instead of the selected one's.
static Element RenderView(const NodeList& nodeList, int selectedIdx, uint16_t port,
                          const std::string& multicastGroup, bool allDetails = false) {
    // Count statuses
    int nOnline = 0, nStale = 0, nOffline = 0;
    int nWarn = 0, nFail = 0;
    for (auto& [id, rec, health] : nodeList) {
        switch (GetStatus(rec)) {
            case NodeStatus::ONLINE:  nOnline++;  break;
            case NodeStatus::STALE:   nStale++;   break;
            case NodeStatus::OFFLINE: nOffline++; break;
        }
        HealthLevel level = GetHealth(health ? &*health : nullptr);
        if (level == HealthLevel::WARN) nWarn++;
        if (level == HealthLevel::FAIL) nFail++;
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
                          "  |  Health:  Warn: " + std::to_string(nWarn) +
                          "  Fail: " + std::to_string(nFail) +
                          "  |  Heartbeats: " + std::to_string(g_packetCount);

    const std::string statusMessage = GetStatusMessage();
    auto statusColor = statusMessage.find("ERROR") != std::string::npos
                           ? Color::Red : Color::Blue;
    auto statusArea = vbox({
        text(" Status: " + statusMessage) | color(statusColor),
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
        text(" Health")             | bold | size(WIDTH, EQUAL, 7),
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
        auto& [id, rec, health] = nodeList[i];
        bool     selected = (i == selectedIdx);
        NodeStatus status = GetStatus(rec);

        std::string statusStr;
        Color       statusColor2;
        switch (status) {
            case NodeStatus::ONLINE:  statusStr = " ONLINE";  statusColor2 = Color::Green;  break;
            case NodeStatus::STALE:   statusStr = " STALE";   statusColor2 = Color::Yellow; break;
            case NodeStatus::OFFLINE: statusStr = " OFFLINE"; statusColor2 = Color::Red;    break;
        }

        std::string healthStr;
        Color       healthColor;
        HealthText(GetHealth(health ? &*health : nullptr), healthStr, healthColor);

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
            text(healthStr)                    | color(healthColor) | size(WIDTH, EQUAL, 7),
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

    // --- Topics and health panel for the selected node (or all nodes) ---
    Elements details;
    if (allDetails) {
        for (auto& [id, rec, health] : nodeList)
            details.push_back(DetailPanel(rec, health));
    } else if (selectedIdx >= 0 && selectedIdx < (int)nodeList.size()) {
        auto& [id, rec, health] = nodeList[selectedIdx];
        details.push_back(DetailPanel(rec, health));
    } else {
        details.push_back(text(" No node selected") | color(Color::GrayDark) | border);
    }

    return vbox({
        header,
        statusArea,
        allDetails ? table : table | flex,
        vbox(std::move(details)),
    });
}

// --- Usage ---

static void PrintUsage() {
    std::cout << "Usage: dmq-monitor [port] [--multicast <group_addr>] [--interface <addr>] [--once <sec>]" << std::endl;
    std::cout << "  port: The UDP port to listen on (default: 9998)" << std::endl;
    std::cout << "  --multicast <addr>: Join a multicast group (e.g. 239.1.1.1)" << std::endl;
    std::cout << "  --interface <addr>: The local interface IP to use (default: auto-detect)" << std::endl;
    std::cout << "  --once <sec>: Listen for <sec> seconds, print the table with every node's" << std::endl;
    std::cout << "                health details as plain text, and exit (no interactive UI)" << std::endl;
}

// --- Main ---

int main(int argc, char* argv[]) {
    uint16_t    port           = 9998;
    std::string multicastGroup;
    std::string localInterface;
    int         onceSeconds    = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--multicast" && i + 1 < argc) {
            multicastGroup = argv[++i];
        } else if (arg == "--interface" && i + 1 < argc) {
            localInterface = argv[++i];
        } else if (arg == "--once" && i + 1 < argc) {
            onceSeconds = std::max(1, std::atoi(argv[++i]));
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

    // Open the socket before the UI starts, so a failure (e.g. another tool
    // already on the port) is reported plainly instead of an empty display.
    UdpSocket socket;
    std::string err = socket.Listen(port, multicastGroup, localInterface);
    if (!err.empty()) {
        std::cerr << err << std::endl;
        return 1;
    }
    SetStatusMessage(multicastGroup.empty()
        ? "Listening on Port: " + std::to_string(port)
        : "Joined Group: " + multicastGroup + " Port: " + std::to_string(port));

    std::thread receiver(ReceiverThread, std::ref(socket));

    // --- Non-interactive: collect, print once (every node's details), exit ---
    if (onceSeconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(onceSeconds));
        NodeList nodeList = SnapshotNodes();
        g_running = false;
        receiver.join();

        auto doc = RenderView(nodeList, -1, port, multicastGroup, /*allDetails=*/true);
        // Render into a tall screen and drop the unused rows at the bottom.
        // (Dimension::Fit() caps the height at the terminal's.)
        auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(1000));
        Render(screen, doc);
        int lastUsed = 0;
        for (int y = 0; y < screen.dimy(); ++y)
            for (int x = 0; x < screen.dimx(); ++x)
                if (screen.PixelAt(x, y).character != " " && !screen.PixelAt(x, y).character.empty())
                    lastUsed = y;
        auto out = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(lastUsed + 1));
        Render(out, doc);
        std::cout << out.ToString() << std::endl;
        return 0;
    }

    auto screen = ScreenInteractive::Fullscreen();

    // Shared snapshot — written by renderer, read by event handler (both on FTXUI thread).
    NodeList sharedNodeList;

    auto renderer = Renderer([&] {
        NodeList nodeList = SnapshotNodes();
        sharedNodeList = nodeList;

        // Resolve selected index from nodeId so new nodes inserted alphabetically
        // before the current selection don't silently shift focus.
        int selectedIdx = 0;
        bool found = false;
        for (int i = 0; i < (int)nodeList.size(); i++) {
            if (std::get<0>(nodeList[i]) == g_selectedNodeId) {
                selectedIdx = i;
                found = true;
                break;
            }
        }
        if (!found && !nodeList.empty())
            g_selectedNodeId = std::get<0>(nodeList[0]);

        // --- Footer ---
        auto footer = hbox({
            text(" [↑↓] Select   [c] Clear Offline   [r] Reset Health Counts   [q] Quit"),
            filler(),
        }) | dim;

        return vbox({
            RenderView(nodeList, selectedIdx, port, multicastGroup) | flex,
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
                if (std::get<0>(sharedNodeList[i]) == g_selectedNodeId && i > 0) {
                    g_selectedNodeId = std::get<0>(sharedNodeList[i - 1]);
                    break;
                }
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            for (int i = 0; i < (int)sharedNodeList.size(); i++) {
                if (std::get<0>(sharedNodeList[i]) == g_selectedNodeId &&
                    i + 1 < (int)sharedNodeList.size()) {
                    g_selectedNodeId = std::get<0>(sharedNodeList[i + 1]);
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
                    g_health.erase(it->first);
                    it = g_nodes.erase(it);
                } else {
                    ++it;
                }
            }
            g_selectedNodeId.clear();
            return true;
        }
        if (event == Event::Character('r')) {
            ResetHealthBaselines();
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
