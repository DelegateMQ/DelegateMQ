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
#include <cstdio>
#include <csignal>
#include <cmath>
#include <deque>

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

enum class LogFormat { TEXT, CSV, JSON };
LogFormat g_logFormat = LogFormat::TEXT;

// Quote a CSV field (RFC 4180): wrap in quotes, double any embedded quotes.
std::string CsvField(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

// Quote and escape a JSON string.
std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

// One packet as a JSON object: the record format for --log-format json and --echo --json.
std::string JsonRecord(int64_t hostTimeUs, const std::string& senderIp, const dmq::databus::SpyPacket& packet) {
    std::ostringstream ss;
    ss << "{\"host_time_us\":" << hostTimeUs
       << ",\"source_time_us\":" << packet.timestamp_us
       << ",\"sender_ip\":" << JsonString(senderIp)
       << ",\"node_id\":" << JsonString((std::string)packet.nodeId)
       << ",\"topic\":" << JsonString((std::string)packet.topic)
       << ",\"value\":" << JsonString((std::string)packet.value) << "}";
    return ss.str();
}

// Write one received packet to the log file in the selected format.
// CSV and JSON carry both clocks: host_time_us (Spy's wall clock, Unix epoch)
// and source_time_us (the sender's monotonic clock, from the packet).
void LogPacket(const std::string& senderIp, const dmq::databus::SpyPacket& packet) {
    if (!g_fileLogger) return;
    if (g_logFormat == LogFormat::TEXT) {
        g_fileLogger->info("[{}] [{}] [{}] {}", senderIp, packet.nodeId, packet.topic, packet.value);
        return;
    }
    auto hostTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string nodeId = (std::string)packet.nodeId;
    std::string topic = (std::string)packet.topic;
    std::string value = (std::string)packet.value;
    if (g_logFormat == LogFormat::CSV) {
        g_fileLogger->info("{},{},{},{},{},{}", hostTime, packet.timestamp_us,
            CsvField(senderIp), CsvField(nodeId), CsvField(topic), CsvField(value));
    } else {
        g_fileLogger->info("{}", JsonRecord(hostTime, senderIp, packet));
    }
}

void ReceiverThread(UdpSocket& socket) {
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
                LogPacket(senderIp, packet);
                
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

// --- Command-line modes (--echo, --hz, --bw) ---
// Headless alternatives to the TUI: print to stdout, no screen handling, so
// they work over SSH and in shell pipelines. Topics are matched by substring,
// the same as the TUI filter.

enum class CliMode { NONE, ECHO, HZ, BW };

/// Per-(sender, topic) sample window for --hz and --bw.
struct TopicStats {
    std::deque<uint64_t> sourceTimes;                  ///< Sender clock, us (hz)
    std::deque<std::pair<uint64_t, size_t>> arrivals;  ///< Spy clock us, packet bytes (bw)
    uint64_t lastArrival = 0;                          ///< Spy clock us
};

uint64_t SteadyNowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string Fixed(double v, int precision) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    return ss.str();
}

// Rate and interval spread from the sender's own timestamps, so network jitter
// between the sender and Spy doesn't distort the numbers.
void PrintHzReport(const std::map<std::pair<std::string, std::string>, TopicStats>& stats, uint64_t now) {
    std::cout << "\n" << std::left << std::setw(18) << "SENDER" << std::setw(28) << "TOPIC" << std::right
              << std::setw(10) << "RATE(Hz)" << std::setw(10) << "MIN(ms)" << std::setw(10) << "MAX(ms)"
              << std::setw(10) << "STD(ms)" << std::setw(8) << "WINDOW" << std::setw(9) << "AGE(s)" << "\n";
    for (const auto& [key, s] : stats) {
        std::vector<double> intervals;
        for (size_t i = 1; i < s.sourceTimes.size(); ++i) {
            int64_t d = static_cast<int64_t>(s.sourceTimes[i]) - static_cast<int64_t>(s.sourceTimes[i - 1]);
            // Skip out-of-order packets and clock jumps (sender reboot), as the TUI deltas do.
            if (d > 0 && d < 3600000000LL) intervals.push_back(static_cast<double>(d) / 1000.0);
        }
        std::string rate = "-", mn = "-", mx = "-", sd = "-";
        if (!intervals.empty()) {
            double sum = 0, lo = intervals[0], hi = intervals[0];
            for (double v : intervals) { sum += v; lo = (std::min)(lo, v); hi = (std::max)(hi, v); }
            double mean = sum / static_cast<double>(intervals.size());
            double var = 0;
            for (double v : intervals) var += (v - mean) * (v - mean);
            var /= static_cast<double>(intervals.size());
            rate = Fixed(1000.0 / mean, 2);
            mn = Fixed(lo, 3); mx = Fixed(hi, 3); sd = Fixed(std::sqrt(var), 3);
        }
        std::cout << std::left << std::setw(18) << key.first << std::setw(28) << key.second << std::right
                  << std::setw(10) << rate << std::setw(10) << mn << std::setw(10) << mx << std::setw(10) << sd
                  << std::setw(8) << intervals.size()
                  << std::setw(9) << Fixed(static_cast<double>(now - s.lastArrival) / 1e6, 1) << "\n";
    }
    std::cout << std::flush;
}

// Sizes are the Spy feed's own UDP packets (topic, value text, timestamp, node
// ID), not the message as serialized on the application's transport. Good for
// comparing topics against each other; not a link budget.
void PrintBwReport(const std::map<std::pair<std::string, std::string>, TopicStats>& stats, uint64_t now) {
    std::cout << "\n" << std::left << std::setw(18) << "SENDER" << std::setw(28) << "TOPIC" << std::right
              << std::setw(10) << "RATE(Hz)" << std::setw(10) << "B/s" << std::setw(8) << "MEAN(B)"
              << std::setw(8) << "MIN(B)" << std::setw(8) << "MAX(B)" << std::setw(8) << "WINDOW"
              << std::setw(9) << "AGE(s)" << "   (spy-feed bytes, not transport bytes)\n";
    for (const auto& [key, s] : stats) {
        std::string rate = "-", bps = "-", mean = "-", mn = "-", mx = "-";
        if (!s.arrivals.empty()) {
            size_t sum = 0, lo = s.arrivals.front().second, hi = lo;
            for (const auto& a : s.arrivals) { sum += a.second; lo = (std::min)(lo, a.second); hi = (std::max)(hi, a.second); }
            mean = std::to_string(sum / s.arrivals.size());
            mn = std::to_string(lo); mx = std::to_string(hi);
            uint64_t span = s.arrivals.back().first - s.arrivals.front().first;
            if (s.arrivals.size() > 1 && span > 0) {
                // Bytes and messages after the first sample, over the span they arrived in.
                double secs = static_cast<double>(span) / 1e6;
                rate = Fixed(static_cast<double>(s.arrivals.size() - 1) / secs, 2);
                bps = Fixed(static_cast<double>(sum - s.arrivals.front().second) / secs, 0);
            }
        }
        std::cout << std::left << std::setw(18) << key.first << std::setw(28) << key.second << std::right
                  << std::setw(10) << rate << std::setw(10) << bps << std::setw(8) << mean
                  << std::setw(8) << mn << std::setw(8) << mx << std::setw(8) << s.arrivals.size()
                  << std::setw(9) << Fixed(static_cast<double>(now - s.lastArrival) / 1e6, 1) << "\n";
    }
    std::cout << std::flush;
}

void OnSigInt(int) { g_running = false; }

int RunCli(UdpSocket& socket, CliMode mode, const std::string& topicFilter, bool json, size_t window, uint16_t port) {
    socket.SetReceiveTimeout(100);
    std::signal(SIGINT, OnSigInt);

    if (mode != CliMode::ECHO)
        std::cerr << "Listening on port " << port << " for topics matching '" << topicFilter
                  << "'. Reporting every 1s. Ctrl-C to stop." << std::endl;

    std::vector<uint8_t> buffer(16384);
    serialize ms_decoder;
    std::map<std::pair<std::string, std::string>, TopicStats> stats;
    uint64_t nextReport = SteadyNowUs() + 1000000;

    while (g_running) {
        int received = socket.Receive(buffer.data(), (int)buffer.size());
        if (received > 0) {
            std::string senderIp = socket.GetRemoteAddress();
            dmq::databus::SpyPacket packet;
            std::istringstream iss(std::string(reinterpret_cast<char*>(buffer.data()), received), std::ios::binary);
            ms_decoder.read(iss, packet);
            if (iss.good()) {
                LogPacket(senderIp, packet);
                std::string topic = (std::string)packet.topic;
                if (topic.find(topicFilter) != std::string::npos) {
                    std::string sender = (std::string)packet.nodeId;
                    if (sender.empty()) sender = senderIp;

                    if (mode == CliMode::ECHO) {
                        if (json) {
                            auto hostTime = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            std::cout << JsonRecord(hostTime, senderIp, packet) << std::endl;
                        } else {
                            std::cout << Fixed(static_cast<double>(packet.timestamp_us) / 1e6, 6) << " "
                                      << sender << " " << topic << " " << (std::string)packet.value << std::endl;
                        }
                    } else {
                        uint64_t now = SteadyNowUs();
                        TopicStats& s = stats[{sender, topic}];
                        s.lastArrival = now;
                        s.sourceTimes.push_back(packet.timestamp_us);
                        s.arrivals.push_back({now, static_cast<size_t>(received)});
                        // Window counts intervals for hz, so keep one extra timestamp.
                        while (s.sourceTimes.size() > window + 1) s.sourceTimes.pop_front();
                        while (s.arrivals.size() > window) s.arrivals.pop_front();
                    }
                }
            }
        }

        if (mode != CliMode::ECHO) {
            uint64_t now = SteadyNowUs();
            if (now >= nextReport) {
                nextReport = now + 1000000;
                if (stats.empty())
                    std::cout << "no matching messages yet" << std::endl;
                else if (mode == CliMode::HZ)
                    PrintHzReport(stats, now);
                else
                    PrintBwReport(stats, now);
            }
        }
    }
    return 0;
}

void PrintUsage() {
    std::cout <<
        "Usage: dmq-spy [port] [options]\n"
        "\n"
        "With no mode option, opens the interactive TUI.\n"
        "\n"
        "Modes (headless, print to stdout; topics match by substring):\n"
        "  --echo <topic>          Print each matching message\n"
        "  --hz <topic>            Report per-topic publish rate every second\n"
        "  --bw <topic>            Report per-topic bandwidth every second (spy-feed bytes)\n"
        "  --json                  With --echo: print JSON Lines\n"
        "  --window <n>            With --hz/--bw: samples per topic (default 100)\n"
        "\n"
        "Options:\n"
        "  --log <file>            Also log all traffic to a file\n"
        "  --log-format <fmt>      text (default), csv or json\n"
        "  --multicast <group>     Join a multicast group\n"
        "  --interface <addr>      Local interface for multicast\n"
        "  --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    uint16_t port = 9999;
    CliMode cliMode = CliMode::NONE;
    std::string cliTopic;
    bool cliJson = false;
    size_t cliWindow = 100;
    std::string logFile;
    std::string multicastGroup;
    std::string localInterface;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log" && i + 1 < argc) logFile = argv[++i];
        else if (arg == "--log-format" && i + 1 < argc) {
            std::string fmt = argv[++i];
            if (fmt == "text") g_logFormat = LogFormat::TEXT;
            else if (fmt == "csv") g_logFormat = LogFormat::CSV;
            else if (fmt == "json") g_logFormat = LogFormat::JSON;
            else {
                std::cout << "Unknown --log-format '" << fmt << "' (use text, csv or json)" << std::endl;
                return 1;
            }
        }
        else if ((arg == "--echo" || arg == "--hz" || arg == "--bw") && i + 1 < argc) {
            if (cliMode != CliMode::NONE) {
                std::cout << "Use only one of --echo, --hz, --bw" << std::endl;
                return 1;
            }
            cliMode = arg == "--echo" ? CliMode::ECHO : arg == "--hz" ? CliMode::HZ : CliMode::BW;
            cliTopic = argv[++i];
        }
        else if (arg == "--json") cliJson = true;
        else if (arg == "--window" && i + 1 < argc) {
            int n = std::atoi(argv[++i]);
            if (n < 2) {
                std::cout << "--window must be at least 2" << std::endl;
                return 1;
            }
            cliWindow = static_cast<size_t>(n);
        }
        else if (arg == "--multicast" && i + 1 < argc) multicastGroup = argv[++i];
        else if (arg == "--interface" && i + 1 < argc) localInterface = argv[++i];
        else if (arg == "--help" || arg == "-h") { PrintUsage(); return 0; }
        else if (isdigit(static_cast<unsigned char>(arg[0]))) port = static_cast<uint16_t>(std::stoi(arg));
        else {
            std::cout << "Unknown option '" << arg << "'\n\n";
            PrintUsage();
            return 1;
        }
    }
    dmq::util::NetworkContext winsock;
    if (logFile.size()) {
        // Truncate for CSV/JSON so the file holds exactly one header and one run.
        g_fileLogger = spdlog::basic_logger_mt("spy_logger", logFile, g_logFormat != LogFormat::TEXT);
        if (g_logFormat != LogFormat::TEXT) {
            g_fileLogger->set_pattern("%v");    // raw record lines, no spdlog prefix
            if (g_logFormat == LogFormat::CSV)
                g_fileLogger->info("host_time_us,source_time_us,sender_ip,node_id,topic,value");
        }
        // Flush periodically so closing the console window loses at most ~1s of log.
        spdlog::flush_every(std::chrono::seconds(1));
    }

    // Open the socket before any UI starts, so a failure (e.g. another tool
    // already on the port) is reported plainly instead of an empty display.
    UdpSocket socket;
    std::string err = socket.Listen(port, multicastGroup, localInterface);
    if (!err.empty()) {
        std::cerr << err << std::endl;
        return 1;
    }

    if (cliMode != CliMode::NONE) {
        int rc = RunCli(socket, cliMode, cliTopic, cliJson, cliWindow, port);
        spdlog::shutdown();     // flush the log file before exit
        return rc;
    }

    // Initial state: waiting for the first message to define T=0
    g_sessionStart = 0;

    auto screen = ScreenInteractive::Fullscreen();
    std::thread receiver(ReceiverThread, std::ref(socket));
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
