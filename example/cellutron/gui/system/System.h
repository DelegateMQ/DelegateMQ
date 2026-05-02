#ifndef _GUI_SYSTEM_H
#define _GUI_SYSTEM_H

#include "DelegateMQ.h"
#include "util/Heartbeat.h"
#include <thread>
#include <atomic>

namespace cellutron {

/// @brief Top-level system coordinator for the GUI node.
class System {
public:
    static System& GetInstance() {
        static System instance;
        return instance;
    }

    void Initialize();
    void Shutdown();
    void Tick(uint32_t ms);

    dmq::os::Thread& GetThread() { return m_thread; }

private:
    System();
    ~System();

    System(const System&) = delete;
    System& operator=(const System&) = delete;

    void SetupNetwork();
    void SetupWatchdog();
    void StartTimerThread();
    void TimerTick();

    dmq::os::Thread m_thread;
    
    std::atomic<bool> m_timerRunning{false};
    dmq::os::Thread m_backgroundTimer;

    util::Heartbeat m_heartbeat;
};

} // namespace cellutron

#endif // _GUI_SYSTEM_H
