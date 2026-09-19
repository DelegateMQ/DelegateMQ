// main.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Main entry point for the DelegateMQ Spy Console TUI application.

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <map>

#include "UdpSocket.h"
#include "extras/databus/SpyPacket.h"
#include "port/serialize/serialize/msg_serialize.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "extras/util/NetworkConnect.h"

using namespace ftxui;

struct LogEntry {
    uint64_t source_timestamp;
    uint64_t arrival_timestamp;
    std::string sender_id;
    std::string topic;
    std::string value;
};

// Global state
std::vector<LogEntry> g_messages;
std::atomic<uint64_t> g_sessionStart{0};
std::mutex g_msgMutex;
std::atomic<bool> g_running{true};
std::atomic<bool> g_paused{false};
std::string g_statusMessage = "Waiting for data...";
std::atomic<uint32_t> g_packetCount{0};
std::string g_filter;
std::shared_ptr<spdlog::logger> g_fileLogger;

void ReceiverThread(uint16_t port, std::string multicastGroup, std::string localInterface) {
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
    serialize ms_decoder;

    while (g_running) {
        int received = socket.Receive(buffer.data(), (int)buffer.size());
        if (received > 0) {
            std::string senderIp = socket.GetRemoteAddress();
            g_packetCount++;
            dmq::databus::SpyPacket packet;
            std::istringstream iss(std::string(reinterpret_cast<char*>(buffer.data()), received), std::ios::binary);
            ms_decoder.read(iss, packet);
            if (iss.good()) {
                if (g_fileLogger) g_fileLogger->info("[{}] [{}] [{}] {}", senderIp, packet.nodeId, packet.topic, packet.value);
                
                auto arrival = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                
                std::lock_guard<std::mutex> lock(g_msgMutex);
                // If this is the very first message of the session, use its arrival time as T=0
                if (g_sessionStart == 0) {
                    g_sessionStart = arrival;
                }

                std::string senderId = (std::string)packet.nodeId;
                if (senderId.empty()) senderId = senderIp;

                g_messages.push_back({packet.timestamp_us, static_cast<uint64_t>(arrival), senderId, (std::string)packet.topic, (std::string)packet.value});
                if (g_messages.size() > 2000) g_messages.erase(g_messages.begin());
            }
        }
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 9999;
    std::string logFile;
    std::string multicastGroup;
    std::string localInterface;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log" && i + 1 < argc) logFile = argv[++i];
        else if (arg == "--multicast" && i + 1 < argc) multicastGroup = argv[++i];
        else if (arg == "--interface" && i + 1 < argc) localInterface = argv[++i];
        else if (isdigit(arg[0])) port = static_cast<uint16_t>(std::stoi(arg));
    }
    dmq::util::NetworkContext winsock;
    if (logFile.size()) g_fileLogger = spdlog::basic_logger_mt("spy_logger", logFile);

    // Initial state: waiting for the first message to define T=0
    g_sessionStart = 0;

    auto screen = ScreenInteractive::Fullscreen();
    std::thread receiver(ReceiverThread, port, multicastGroup, localInterface);
    Component input_filter = Input(&g_filter, "topic filter...");

    static std::vector<LogEntry> display_cache;
    int scroll_offset = 0;
    auto renderer = Renderer(input_filter, [&] {
        Elements rows;
        
        // Header
        rows.push_back(hbox({
            text(" Session Time") | bold | size(WIDTH, EQUAL, 16), separator(),
            text(" T-Delta(ms)")  | bold | size(WIDTH, EQUAL, 12), separator(),
            text(" B-Delta(ms)")  | bold | size(WIDTH, EQUAL, 12), separator(),
            text(" Sender")       | bold | size(WIDTH, EQUAL, 16), separator(),
            text(" Topic")        | bold | size(WIDTH, EQUAL, 26), separator(),
            text(" Value")        | bold | flex
        }) | color(Color::White));
        rows.push_back(separator());

        {
            std::lock_guard<std::mutex> lock(g_msgMutex);
            if (!g_paused) {
                display_cache = g_messages;
                // Sort by local arrival time to ensure a perfectly monotonic timeline
                std::stable_sort(display_cache.begin(), display_cache.end(), [](const LogEntry& a, const LogEntry& b) {
                    return a.arrival_timestamp < b.arrival_timestamp;
                });
            }
        }

        if (!display_cache.empty()) {
            std::map<std::string, uint64_t> lastBus;
            std::map<std::pair<std::string, std::string>, uint64_t> lastTopic;

            std::vector<Element> data_rows;
            for (size_t i = 0; i < display_cache.size(); ++i) {
                auto& msg = display_cache[i];

                // Filter first so deltas and session time are calculated for visible rows
                if (!g_filter.empty() && msg.topic.find(g_filter) == std::string::npos) continue;
                
                // Deltas use HIGH-PRECISION source timestamps for jitter analysis
                int64_t bDelta = 0;
                if (lastBus.count(msg.sender_id)) {
                    bDelta = static_cast<int64_t>(msg.source_timestamp) - static_cast<int64_t>(lastBus[msg.sender_id]);
                    // Cap at 1 hour; if larger, it's likely a boot-time jump or first message
                    if (std::abs(bDelta) > 3600000000LL) bDelta = 0;
                }
                lastBus[msg.sender_id] = msg.source_timestamp;

                int64_t tDelta = 0;
                auto tKey = std::make_pair(msg.sender_id, msg.topic);
                if (lastTopic.count(tKey)) {
                    tDelta = static_cast<int64_t>(msg.source_timestamp) - static_cast<int64_t>(lastTopic[tKey]);
                    // Cap at 1 hour; if larger, it's likely a boot-time jump or first message
                    if (std::abs(tDelta) > 3600000000LL) tDelta = 0;
                }
                lastTopic[tKey] = msg.source_timestamp;

                // Session time uses LOCAL arrival time for absolute stability
                int64_t relTimeUs = static_cast<int64_t>(msg.arrival_timestamp) - static_cast<int64_t>(g_sessionStart);
                double relTime = static_cast<double>(relTimeUs < 0 ? 0 : relTimeUs) / 1000000.0;

                auto fmtDelta = [](int64_t d) {
                    if (d == 0) return std::string("0.000");
                    std::stringstream ss; ss << std::fixed << std::setprecision(3) << (static_cast<double>(d) / 1000.0);
                    return ss.str();
                };

                auto value_color = Color::White;
                std::string lv = msg.value; std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
                if (lv.find("ok") != std::string::npos || lv.find("run") != std::string::npos || lv.find("true") != std::string::npos) value_color = Color::Green;
                else if (lv.find("err") != std::string::npos || lv.find("fault") != std::string::npos || lv.find("fail") != std::string::npos) value_color = Color::Red;
                else if (lv.find("warn") != std::string::npos) value_color = Color::Yellow;

                std::stringstream ssRel; ssRel << std::fixed << std::setprecision(6) << relTime;

                data_rows.push_back(hbox({
                    text(" " + ssRel.str()) | color(Color::GrayDark) | size(WIDTH, EQUAL, 16), separator(),
                    text(" " + fmtDelta(tDelta)) | color(Color::Cyan) | size(WIDTH, EQUAL, 12), separator(),
                    text(" " + fmtDelta(bDelta)) | color(Color::Blue) | size(WIDTH, EQUAL, 12), separator(),
                    text(" " + msg.sender_id) | color(Color::Green) | size(WIDTH, EQUAL, 16), separator(),
                    text(" " + msg.topic) | color(Color::Yellow) | size(WIDTH, EQUAL, 26), separator(),
                    text(" " + msg.value) | color(value_color) | flex
                }));
            }
            
            size_t skip = g_paused ? static_cast<size_t>(scroll_offset) : 0;
            skip = (std::min)(skip, data_rows.size());
            for (auto it = data_rows.rbegin() + static_cast<std::ptrdiff_t>(skip); it != data_rows.rend(); ++it) {
                rows.push_back(*it);
            }
        }

        return vbox({
            text("DelegateMQ Spy Console (Port: " + std::to_string(port) + ")") | bold | color(Color::Green) | center,
            hbox(text(" Filter: "), input_filter->Render()) | border,
            vbox(std::move(rows)) | yframe | flex | border,
            hbox({ 
                text(" Messages: " + std::to_string(g_messages.size()) + (g_paused ? (" [PAUSED" + (scroll_offset > 0 ? " +" + std::to_string(scroll_offset) : "") + "]") : "")) | color(Color::BlueLight),
                filler(),
                text(" Ctrl-P pause | Ctrl-C clear | Ctrl-Q quit | wheel scrolls when paused ") | dim
            }) | size(HEIGHT, EQUAL, 1)
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('\x11')) { g_running = false; screen.Exit(); return true; }
        if (event == Event::Character(static_cast<char>(16))) {
            g_paused = !g_paused;
            if (!g_paused) scroll_offset = 0;
            return true;
        }
        if (event == Event::Character('\x03')) {
            std::lock_guard<std::mutex> lock(g_msgMutex);
            g_messages.clear();
            g_sessionStart = 0;
            scroll_offset = 0;
            return true;
        }
        if (g_paused && event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelDown) { ++scroll_offset; return true; }
            if (event.mouse().button == Mouse::WheelUp) { if (scroll_offset > 0) --scroll_offset; return true; }
        }
        if (g_paused) {
            int page = (std::max)(1, Terminal::Size().dimy - 7);
            if (event == Event::PageDown) { scroll_offset += page; return true; }
            if (event == Event::PageUp)   { scroll_offset = (std::max)(0, scroll_offset - page); return true; }
        }
        return false;
    });

    std::thread refresher([&] { while (g_running) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); screen.PostEvent(Event::Custom); } });
    screen.Loop(component);
    g_running = false; receiver.join(); refresher.join();
    return 0;
}
