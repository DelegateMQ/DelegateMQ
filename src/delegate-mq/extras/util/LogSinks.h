#ifndef DMQ_LOG_SINKS_H
#define DMQ_LOG_SINKS_H

/// @file LogSinks.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Ready-made dmq::LogHandler sinks (extras/util/Log.h): pass one to
/// dmq::SetLogHandler(). Each is a plain function; write your own the same way
/// (SEGGER RTT, a file, a ring buffer for a crash dump).
///
/// | Sink | Where lines go | Available |
/// |---|---|---|
/// | dmq::log::StdioSink | printf() (stdout; a UART where stdout is retargeted), one line at a time under a dmq::Mutex | every port |
/// | dmq::log::EspLogSink | ESP_LOGE/W/I/D, with ESP-IDF's colours and timestamps | ESP-IDF |
/// | dmq::log::ZephyrSink | printk() | Zephyr |
/// | dmq::log::NuttXSink | syslog() at LOG_ERR/WARNING/INFO/DEBUG | NuttX |
/// | dmq::databus::SpyLogSink | dmq-spy, through a SpyReporter (extras/databus/SpyLogSink.h) | thread ports |
///
/// StdioSink's mutex keeps lines whole when threads log at once. On the
/// FreeRTOS Windows/POSIX simulators it also keeps a task from being suspended
/// while it holds the C runtime's stdout lock, which stalls the next task that
/// prints: on FreeRTOS, dmq::Mutex is a kernel mutex the scheduler can see.
/// Lines that bypass the sink (a plain printf()) aren't covered.

#include "delegate/DelegateOpt.h"
#include <cstdio>
#include <new>

#if defined(ESP_PLATFORM)
    #include "esp_log.h"
#endif
#if defined(__ZEPHYR__)
    #include <zephyr/sys/printk.h>
#endif
#if defined(__NuttX__)
    #include <syslog.h>
#endif

namespace dmq::log {

/// @brief printf() one line: "[INF] Pump: speed 1200 rpm". A dmq::Mutex keeps
/// lines whole across threads (not for use in an interrupt).
inline void StdioSink(dmq::LogLevel level, const char* tag, const char* msg)
{
    // Constructed on first use, not at static initialization (on FreeRTOS a
    // dmq::Mutex allocates from the kernel's heap, set up later by the app),
    // and never destroyed: a static destructor that logs at exit still finds
    // it. Placement new into static storage, no heap.
    alignas(dmq::Mutex) static char storage[sizeof(dmq::Mutex)];
    static dmq::Mutex* lock = new (storage) dmq::Mutex();
    dmq::LockGuard<dmq::Mutex> guard(*lock);
    printf("[%s] %s: %s\n", dmq::LogLevelName(level), tag ? tag : "", msg ? msg : "");
    fflush(stdout);
}

#if defined(ESP_PLATFORM)
/// @brief ESP-IDF's log, at the matching level, so esp_log_level_set() and
/// idf.py monitor's colours apply.
inline void EspLogSink(dmq::LogLevel level, const char* tag, const char* msg)
{
    tag = tag ? tag : "";
    msg = msg ? msg : "";
    switch (level) {
    case dmq::LogLevel::ERR: ESP_LOGE(tag, "%s", msg); break;
    case dmq::LogLevel::WRN: ESP_LOGW(tag, "%s", msg); break;
    case dmq::LogLevel::INF: ESP_LOGI(tag, "%s", msg); break;
    case dmq::LogLevel::DBG: ESP_LOGD(tag, "%s", msg); break;
    }
}
#endif

#if defined(__ZEPHYR__)
/// @brief Zephyr's printk() (the console), one line per call.
inline void ZephyrSink(dmq::LogLevel level, const char* tag, const char* msg)
{
    tag = tag ? tag : "";
    msg = msg ? msg : "";
    printk("[%s] %s: %s\n", dmq::LogLevelName(level), tag, msg);
}
#endif

#if defined(__NuttX__)
/// @brief NuttX's syslog() at the matching priority.
inline void NuttXSink(dmq::LogLevel level, const char* tag, const char* msg)
{
    tag = tag ? tag : "";
    msg = msg ? msg : "";
    int priority = LOG_INFO;
    switch (level) {
    case dmq::LogLevel::ERR: priority = LOG_ERR; break;
    case dmq::LogLevel::WRN: priority = LOG_WARNING; break;
    case dmq::LogLevel::INF: priority = LOG_INFO; break;
    case dmq::LogLevel::DBG: priority = LOG_DEBUG; break;
    }
    syslog(priority, "%s: %s\n", tag, msg);
}
#endif

} // namespace dmq::log

#endif // DMQ_LOG_SINKS_H
