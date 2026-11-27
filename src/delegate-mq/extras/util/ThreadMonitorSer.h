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
        s.write(os, data.stack_size);
        s.write(os, data.stack_free_min);
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
        s.read(is, data.stack_size);
        s.read(is, data.stack_free_min);
        return is;
    }
};

/// @brief Stringifier for ThreadStatsPacket (for DataSpy). snprintf, not a
/// stringstream: no iostreams in the library.
inline dmq::xstring ThreadStatsPacketToString(const ThreadStatsPacket& p) {
    // Stack: least free / size in bytes, "n/a" for a value the port doesn't know
    char freeText[12] = "n/a";
    char sizeText[12] = "n/a";
    if (p.stack_free_min != dmq::STACK_UNKNOWN)
        snprintf(freeText, sizeof(freeText), "%lu", static_cast<unsigned long>(p.stack_free_min));
    if (p.stack_size != dmq::STACK_UNKNOWN)
        snprintf(sizeText, sizeof(sizeText), "%lu", static_cast<unsigned long>(p.stack_size));

    char text[224];
    snprintf(text, sizeof(text), "CPU:%s Thread:%s Q:%u/%u/%u Latency(ms):%.2f/%.2f Invoke(ms):%.2f/%.2f Stack free/size:%s/%s",
             p.cpu_name.c_str(), p.thread_name.c_str(),
             static_cast<unsigned>(p.queue_depth), static_cast<unsigned>(p.queue_depth_max_window),
             static_cast<unsigned>(p.queue_depth_max_all),
             static_cast<double>(p.latency_avg_ms), static_cast<double>(p.latency_max_window_ms),
             static_cast<double>(p.invoke_avg_ms), static_cast<double>(p.invoke_max_window_ms),
             freeText, sizeText);
    return text;
}

} // namespace dmq::util

#endif
