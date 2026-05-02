#ifndef THREAD_MONITOR_H
#define THREAD_MONITOR_H

#include "DelegateMQ.h"

#if defined(DMQ_DATABUS)

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

namespace dmq::util {
// ... rest of namespace dmq::util content ...

/// @brief Packet published to DataBus for thread monitoring.
struct ThreadStatsPacket {
    std::string cpu_name;
    std::string thread_name;
    uint32_t    queue_depth;
    uint32_t    queue_depth_max_window;
    uint32_t    queue_depth_max_all;
    uint32_t    queue_size_limit;
    float       latency_avg_ms;
    float       latency_max_window_ms;
    float       latency_max_all_ms;
    uint64_t    dispatch_count;
};

/// @brief Central monitor that polls registered threads and publishes stats.
class ThreadMonitor {
public:
    /// Register a thread for monitoring.
    static void Register(dmq::os::Thread* thread);

    /// Deregister a thread.
    static void Deregister(dmq::os::Thread* thread);

    /// Enable the monitor (starts the 1Hz polling thread).
    static void Enable(const std::string& topic = "ThreadStats");

    /// Disable the monitor.
    static void Disable();

private:
    ThreadMonitor() = default;
    ~ThreadMonitor();

    static ThreadMonitor& GetInstance() {
        static ThreadMonitor instance;
        return instance;
    }

    void MonitorLoop();

    std::vector<dmq::os::Thread*> m_threads;
    dmq::Mutex m_mutex;
    std::unique_ptr<dmq::os::Thread> m_monitorThread;
    std::atomic<bool> m_enabled{false};
    std::string m_topic;
};

} // namespace dmq::util

#endif // DMQ_DATABUS

#endif
