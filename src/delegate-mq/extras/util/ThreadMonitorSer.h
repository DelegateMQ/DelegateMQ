#ifndef THREAD_MONITOR_SER_H
#define THREAD_MONITOR_SER_H

#include "ThreadMonitor.h"
#include "port/serialize/serialize/msg_serialize.h"
#include <cstdio>

namespace dmq::util {

/// @brief Serializer for ThreadStatsPacket.
class ThreadStatsPacketSerializer : public dmq::ISerializer<void(ThreadStatsPacket)> {
public:
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const ThreadStatsPacket& data) override {
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

    virtual dmq::ByteReader& Read(dmq::ByteReader& is, ThreadStatsPacket& data) override {
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

/// @brief Stringifier for ThreadStatsPacket (for DataSpy). snprintf, not a
/// stringstream: no iostreams in the library.
inline dmq::xstring ThreadStatsPacketToString(const ThreadStatsPacket& p) {
    char text[192];
    snprintf(text, sizeof(text), "CPU:%s Thread:%s Q:%u/%u/%u Latency(ms):%.2f/%.2f Invoke(ms):%.2f/%.2f",
             p.cpu_name.c_str(), p.thread_name.c_str(),
             static_cast<unsigned>(p.queue_depth), static_cast<unsigned>(p.queue_depth_max_window),
             static_cast<unsigned>(p.queue_depth_max_all),
             static_cast<double>(p.latency_avg_ms), static_cast<double>(p.latency_max_window_ms),
             static_cast<double>(p.invoke_avg_ms), static_cast<double>(p.invoke_max_window_ms));
    return text;
}

} // namespace dmq::util

#endif
