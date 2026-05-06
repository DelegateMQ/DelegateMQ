#ifndef THREAD_MONITOR_SER_H
#define THREAD_MONITOR_SER_H

#include "ThreadMonitor.h"
#include "port/serialize/serialize/msg_serialize.h"
#include <sstream>
#include <iomanip>

namespace dmq::util {

/// @brief Serializer for ThreadStatsPacket.
class ThreadStatsPacketSerializer : public dmq::ISerializer<void(ThreadStatsPacket)> {
public:
    virtual std::ostream& Write(std::ostream& os, const ThreadStatsPacket& data) override {
        serialize s;
        s.write(os, data.cpu_name);
        s.write(os, data.thread_name);
        s.write(os, data.queue_depth);
        s.write(os, data.queue_depth_max_window);
        s.write(os, data.queue_depth_max_all);
        s.write(os, data.queue_size_limit);
        s.write(os, data.latency_avg_ms);
        s.write(os, data.latency_max_window_ms);
        s.write(os, data.latency_max_all_ms);
        s.write(os, data.invoke_avg_ms);
        s.write(os, data.invoke_max_window_ms);
        s.write(os, data.invoke_max_all_ms);
        s.write(os, data.dispatch_count);
        return os;
    }

    virtual std::istream& Read(std::istream& is, ThreadStatsPacket& data) override {
        serialize s;
        s.read(is, data.cpu_name);
        s.read(is, data.thread_name);
        s.read(is, data.queue_depth);
        s.read(is, data.queue_depth_max_window);
        s.read(is, data.queue_depth_max_all);
        s.read(is, data.queue_size_limit);
        s.read(is, data.latency_avg_ms);
        s.read(is, data.latency_max_window_ms);
        s.read(is, data.latency_max_all_ms);
        s.read(is, data.invoke_avg_ms);
        s.read(is, data.invoke_max_window_ms);
        s.read(is, data.invoke_max_all_ms);
        s.read(is, data.dispatch_count);
        return is;
    }
};

/// @brief Stringifier for ThreadStatsPacket (for DataSpy).
inline std::string ThreadStatsPacketToString(const ThreadStatsPacket& p) {
    std::stringstream ss;
    ss << "CPU:" << p.cpu_name << " Thread:" << p.thread_name 
       << " Q:" << p.queue_depth << "/" << p.queue_depth_max_window << "/" << p.queue_depth_max_all
       << " Latency(ms):" << std::fixed << std::setprecision(2) << p.latency_avg_ms << "/" << p.latency_max_window_ms
       << " Invoke(ms):" << std::fixed << std::setprecision(2) << p.invoke_avg_ms << "/" << p.invoke_max_window_ms;
    return ss.str();
}

} // namespace dmq::util

#endif
