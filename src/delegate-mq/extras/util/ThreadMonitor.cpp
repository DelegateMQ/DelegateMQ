#include "ThreadMonitor.h"
#include "DelegateMQ.h"

#if defined(DMQ_DATABUS)

#include <chrono>

namespace dmq::util {
// ... rest of namespace dmq::util content ...

ThreadMonitor::~ThreadMonitor() {
    Disable();
}

void ThreadMonitor::Register(dmq::os::Thread* thread) {
    if (!thread) return;
    auto& instance = GetInstance();
    dmq::LockGuard<dmq::Mutex> lock(instance.m_mutex);
    for (size_t i = 0; i < instance.m_threadCount; ++i) {
        if (instance.m_threads[i] == thread) return;
    }
    if (instance.m_threadCount < dmq::MAX_WATCHDOG_THREADS)
        instance.m_threads[instance.m_threadCount++] = thread;
    else
        ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
}

void ThreadMonitor::Deregister(dmq::os::Thread* thread) {
    if (!thread) return;
    auto& instance = GetInstance();
    dmq::LockGuard<dmq::Mutex> lock(instance.m_mutex);
    for (size_t i = 0; i < instance.m_threadCount; ++i) {
        if (instance.m_threads[i] == thread) {
            instance.m_threads[i] = instance.m_threads[--instance.m_threadCount];
            instance.m_threads[instance.m_threadCount] = nullptr;
            return;
        }
    }
}

void ThreadMonitor::Enable(const dmq::xstring& topic) {
    auto& instance = GetInstance();
    if (instance.m_enabled.exchange(true)) return;

    instance.m_topic = topic;
    instance.m_monitorThread = std::make_unique<dmq::os::Thread>("ThreadMonitor", 10);
    instance.m_monitorThread->CreateThread();
    
    (void)dmq::MakeDelegate(&instance, &ThreadMonitor::MonitorLoop, *instance.m_monitorThread).AsyncInvoke();
}

void ThreadMonitor::Disable() {
    auto& instance = GetInstance();
    if (!instance.m_enabled.exchange(false)) return;

    if (instance.m_monitorThread) {
        instance.m_monitorThread->ExitThread();
        instance.m_monitorThread.reset();
    }
}

void ThreadMonitor::MonitorLoop() {
    if (!m_enabled) return;

    std::array<dmq::os::Thread::ThreadStats, dmq::MAX_WATCHDOG_THREADS> snapshots;
    size_t snapshotCount = 0;
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        for (size_t i = 0; i < m_threadCount; ++i)
            snapshots[snapshotCount++] = m_threads[i]->SnapshotStats();
    }

    for (size_t i = 0; i < snapshotCount; ++i) {
        const auto& s = snapshots[i];
        ThreadStatsPacket packet;
        packet.cpu_name = s.cpu_name;
        packet.thread_name = s.thread_name;
        packet.queue_depth = (uint32_t)s.queue_depth;
        packet.queue_depth_max_window = (uint32_t)s.queue_depth_max_window;
        packet.queue_depth_max_all = (uint32_t)s.queue_depth_max_all;
        packet.queue_size_limit = (uint32_t)s.queue_size_limit;
        packet.latency_avg_ms = s.latency_avg_ms;
        packet.latency_max_window_ms = s.latency_max_window_ms;
        packet.latency_max_all_ms = s.latency_max_all_ms;
        packet.invoke_avg_ms = s.invoke_avg_ms;
        packet.invoke_max_window_ms = s.invoke_max_window_ms;
        packet.invoke_max_all_ms = s.invoke_max_all_ms;
        packet.dispatch_count = s.dispatch_count;

        dmq::databus::DataBus::Publish(m_topic, packet);
    }

    if (m_enabled) {
        dmq::os::Thread::Sleep(std::chrono::seconds(2));
        (void)dmq::MakeDelegate(this, &ThreadMonitor::MonitorLoop, *m_monitorThread).AsyncInvoke();
    }
}

} // namespace dmq::util

#endif // DMQ_DATABUS
