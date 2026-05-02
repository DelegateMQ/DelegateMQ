#ifndef _LOGS_H
#define _LOGS_H

#include "DelegateMQ.h"
#include <string>
#include <fstream>

namespace cellutron {
namespace util {

/// @brief Singleton class for system-wide data logging to a file.
class Logs {
public:
    static Logs& GetInstance() {
        static Logs instance;
        return instance;
    }

    /// Initialize logging and start the logging thread.
    void Initialize();

    /// Shutdown logging.
    void Shutdown();

private:
    Logs() = default;
    ~Logs();

    Logs(const Logs&) = delete;
    Logs& operator=(const Logs&) = delete;

    void WriteToFile(const std::string& msg);
    std::string GetTimestamp();
    void LogHeartbeat();

    std::ofstream m_file;
    size_t m_writeCount = 0;
    
    // Use standardized thread name for Active Object subsystem
    dmq::os::Thread m_thread{"LogsThread", 100, dmq::os::FullPolicy::DROP, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI"};

    std::unique_ptr<dmq::util::Timer> m_heartbeatTimer;
    dmq::ScopedConnection m_heartbeatConn;

    // Scoped connections for DataBus
    dmq::ScopedConnection m_startConn;
    dmq::ScopedConnection m_stopConn;
    dmq::ScopedConnection m_runConn;
    dmq::ScopedConnection m_speedConn;
    dmq::ScopedConnection m_faultConn;
    dmq::ScopedConnection m_actuatorConn;
    dmq::ScopedConnection m_sensorConn;
};

} // namespace util
} // namespace cellutron

#endif
