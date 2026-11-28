#ifndef DMQ_LOG_H
#define DMQ_LOG_H

/// @file Log.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief A portable log: library and application code log through one
/// handler, and the application decides where lines go (a UART, ESP_LOG,
/// printk, syslog, dmq-spy, ...). Portable code then needs no printf(),
/// ESP_LOGI() or printk() of its own.
///
/// @code
/// dmq::SetLogHandler(&dmq::log::StdioSink);             // extras/util/LogSinks.h
/// DMQ_LOG_INF("Pump", "speed %d rpm", rpm);              // printf-style
/// DMQ_LOG_ERR("Pump", "sensor %u failed", id);
/// @endcode
///
/// - Until the application sets a handler (dmq::SetLogHandler()), each line
///   goes to printf() as "[ERR] tag: text", as the library's messages always
///   have; with DMQ_LOG (spdlog, desktop), to spdlog. SetLogHandler(nullptr)
///   silences the log: a line with no handler isn't even formatted.
/// - DMQ_LOG_LEVEL (default 3) compiles out the levels above it: their lines
///   cost no code or flash.
/// - A line is formatted on the calling thread's stack, DMQ_LOG_LINE_SIZE
///   bytes (default 128), no heap. A longer line is truncated and counted
///   (dmq::GetLogTruncatedCount()).
/// - The handler runs on the calling thread, so a slow sink (a blocking UART)
///   slows the caller; extras/databus/SpyLogSink.h only queues the line.
/// - Not from an interrupt on an RTOS: sinks take OS mutexes or queue to a
///   thread. Defer with an IsrDelegate. The library logs only outside its
///   critical sections; on bare metal with Timer::ProcessTimers() in an
///   interrupt, the timer's "falling behind" warning comes from that interrupt.
///
/// Include DelegateOpt.h (or DelegateMQ.h), not this header: DelegateOpt.h
/// includes it after the constants it uses.

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#ifdef DMQ_LOG
    #include <spdlog/spdlog.h>
#endif

namespace dmq {

/// @brief Log level, most severe first. Zephyr's short names: ERROR and DEBUG
/// are macros in Windows headers and many builds.
enum class LogLevel : uint8_t
{
    ERR = 1,
    WRN = 2,
    INF = 3,
    DBG = 4
};

/// @brief Receives each formatted line: level, tag (the component, never
/// nullptr) and the message, NUL-terminated, without a trailing newline.
/// Both strings are valid only during the call.
using LogHandler = void (*)(LogLevel level, const char* tag, const char* msg);

/// @brief "ERR", "WRN", "INF" or "DBG".
inline const char* LogLevelName(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::ERR: return "ERR";
    case LogLevel::WRN: return "WRN";
    case LogLevel::INF: return "INF";
    case LogLevel::DBG: return "DBG";
    }
    return "???";
}

namespace detail {

#ifdef DMQ_LOG
    /// The handler a DMQ_LOG (spdlog) build starts with
    inline void SpdlogHandler(LogLevel level, const char* tag, const char* msg)
    {
        switch (level) {
        case LogLevel::ERR: spdlog::error("[{}] {}", tag, msg); break;
        case LogLevel::WRN: spdlog::warn("[{}] {}", tag, msg); break;
        case LogLevel::INF: spdlog::info("[{}] {}", tag, msg); break;
        case LogLevel::DBG: spdlog::debug("[{}] {}", tag, msg); break;
        }
    }
    inline std::atomic<LogHandler> g_logHandler{ &SpdlogHandler };
#else
    /// The handler every other build starts with: one printf() per line
    inline void PrintfHandler(LogLevel level, const char* tag, const char* msg)
    {
        printf("[%s] %s: %s\n", LogLevelName(level), tag, msg);
    }
    inline std::atomic<LogHandler> g_logHandler{ &PrintfHandler };
#endif
    inline std::atomic<uint32_t> g_logTruncated{ 0 };

} // namespace detail

/// @brief Send every log line to handler from now on; nullptr: none (lines
/// are then not formatted at all). Any thread.
inline void SetLogHandler(LogHandler handler) noexcept { detail::g_logHandler.store(handler); }

/// @brief The current handler, e.g. to chain to it from a new one.
inline LogHandler GetLogHandler() noexcept { return detail::g_logHandler.load(); }

/// @brief Lines cut short to DMQ_LOG_LINE_SIZE so far. Two threads that
/// truncate at the same instant may count once: it's counted with a load and
/// a store, not a read-modify-write, which ARMv6-M (Cortex-M0) can't link.
inline uint32_t GetLogTruncatedCount() noexcept { return detail::g_logTruncated.load(); }

#if defined(__GNUC__) || defined(__clang__)
inline void LogV(LogLevel level, const char* tag, const char* format, va_list args)
    __attribute__((format(__printf__, 3, 0)));
#endif

/// @brief Format a line (vsnprintf) and pass it to the handler. Use the
/// DMQ_LOG_* macros, which compile out levels above DMQ_LOG_LEVEL.
inline void LogV(LogLevel level, const char* tag, const char* format, va_list args)
{
    const LogHandler handler = detail::g_logHandler.load();
    if (handler == nullptr || format == nullptr)
        return;
    char line[dmq::LOG_LINE_SIZE];
    const int n = vsnprintf(line, sizeof(line), format, args);
    if (n < 0)
        line[0] = '\0';
    else if (static_cast<size_t>(n) >= sizeof(line))
        detail::g_logTruncated.store(detail::g_logTruncated.load() + 1);   // see GetLogTruncatedCount()
    handler(level, tag ? tag : "", line);
}

#if defined(__GNUC__) || defined(__clang__)
inline void Log(LogLevel level, const char* tag, const char* format, ...)
    __attribute__((format(__printf__, 3, 4)));
#endif

/// @brief Format a line, printf-style, and pass it to the handler.
inline void Log(LogLevel level, const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, tag, format, args);
    va_end(args);
}

} // namespace dmq

// A level above DMQ_LOG_LEVEL compiles to a call that never runs, so its
// arguments still count as used and the optimizer drops it with its string.
// An unoptimized (-O0) build may still link dmq::Log() and vsnprintf() with
// DMQ_LOG_LEVEL 0, unless the linker removes unused sections (--gc-sections).
#if DMQ_LOG_LEVEL >= 1
    #define DMQ_LOG_ERR(tag, ...) ::dmq::Log(::dmq::LogLevel::ERR, tag, __VA_ARGS__)
#else
    #define DMQ_LOG_ERR(tag, ...) do { if constexpr (false) ::dmq::Log(::dmq::LogLevel::ERR, tag, __VA_ARGS__); } while (0)
#endif
#if DMQ_LOG_LEVEL >= 2
    #define DMQ_LOG_WRN(tag, ...) ::dmq::Log(::dmq::LogLevel::WRN, tag, __VA_ARGS__)
#else
    #define DMQ_LOG_WRN(tag, ...) do { if constexpr (false) ::dmq::Log(::dmq::LogLevel::WRN, tag, __VA_ARGS__); } while (0)
#endif
#if DMQ_LOG_LEVEL >= 3
    #define DMQ_LOG_INF(tag, ...) ::dmq::Log(::dmq::LogLevel::INF, tag, __VA_ARGS__)
#else
    #define DMQ_LOG_INF(tag, ...) do { if constexpr (false) ::dmq::Log(::dmq::LogLevel::INF, tag, __VA_ARGS__); } while (0)
#endif
#if DMQ_LOG_LEVEL >= 4
    #define DMQ_LOG_DBG(tag, ...) ::dmq::Log(::dmq::LogLevel::DBG, tag, __VA_ARGS__)
#else
    #define DMQ_LOG_DBG(tag, ...) do { if constexpr (false) ::dmq::Log(::dmq::LogLevel::DBG, tag, __VA_ARGS__); } while (0)
#endif

#endif // DMQ_LOG_H
