#ifndef DMQ_SPY_LOG_SINK_H
#define DMQ_SPY_LOG_SINK_H

/// @file SpyLogSink.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief A dmq::LogHandler (extras/util/Log.h) that sends each log line to
/// dmq-spy through a SpyReporter, so an embedded node's log shows up next to
/// its DataBus traffic, in time order, and `dmq-spy --log` captures it to disk.
///
/// @code
/// static dmq::databus::SpyReporter spy;
/// spy.Start(spyTransport, "Controller");
/// dmq::databus::SpyLogSink::Attach(spy, &dmq::log::StdioSink);  // dmq-spy and the console
/// DMQ_LOG_WRN("Pump", "pressure %d kPa", kpa);   // dmq-spy: topic "log/WRN/Pump"
/// @endcode
///
/// - A line becomes a message on topic "log/<level>/<tag>" (e.g.
///   "log/ERR/RetryMonitor") with the text as its value, stamped with
///   dmq::Clock like DataBus traffic. dmq-spy's filter selects by level or tag
///   ("log/ERR", "RetryMonitor"), and --log, --echo and the CSV/JSON output
///   take log lines like any message.
/// - Only queues the line to the SpyReporter's thread: the logging thread
///   never waits on the network. A full queue drops the line, counted by the
///   SpyReporter (GetDropped(), OnDropped).
/// - Attach() with a next handler (a console sink) sends every line there too.
/// - A line logged while this sink is already handing one to the SpyReporter
///   (a log call from inside the dispatch, or from an OnDropped handler) goes
///   only to the next handler, so logging can't loop. On a multi-core CPU two
///   threads logging at the same instant can hit this too; such lines are
///   counted (GetSkipped()).
/// - Not from an interrupt (it dispatches to a thread).

#include "delegate/DelegateOpt.h"
#include "extras/databus/SpyReporter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dmq::databus {

class SpyLogSink {
public:
    /// @brief Send log lines to spy, and to next if given, from now on: sets
    ///        dmq::SetLogHandler(&SpyLogSink::Handler).
    /// @param spy a SpyReporter, started or not (lines before Start() are
    ///        dropped and counted); must outlive the sink, or Detach() first
    /// @param next another handler each line also goes to (e.g.
    ///        dmq::log::StdioSink), or nullptr. Passing this sink's own handler
    ///        (dmq::GetLogHandler() while attached) keeps the current next.
    static void Attach(SpyReporter& spy, dmq::LogHandler next = nullptr)
    {
        if (next == &SpyLogSink::Handler)
            next = Next().load();       // chaining to itself would recurse forever
        Next().store(next);
        Spy().store(&spy);
        dmq::SetLogHandler(&SpyLogSink::Handler);
    }

    /// @brief Stop sending to dmq-spy: the log handler becomes the next
    ///        handler given to Attach() (nullptr: none). Returns once no
    ///        thread is still handing a line to the SpyReporter, so the
    ///        SpyReporter may be destroyed after it.
    /// @note Not from a log handler, an OnDropped handler or anything else
    ///       that runs inside a log call: it would wait for itself.
    static void Detach()
    {
        dmq::SetLogHandler(Next().load());
        Spy().store(nullptr);
        while (InFlight().load() != 0)
            dmq::ThisThread::yield();
    }

    /// @brief Lines not sent to dmq-spy because the sink was busy with
    ///        another (see the file comment).
    static uint32_t GetSkipped() { return Skipped().load(); }

    /// @brief The dmq::LogHandler itself.
    static void Handler(dmq::LogLevel level, const char* tag, const char* msg)
    {
        tag = tag ? tag : "";
        msg = msg ? msg : "";
        if (const dmq::LogHandler next = Next().load())
            next(level, tag, msg);

        // Counted before reading Spy(), so Detach() waits for this call
        const Counted inFlight(InFlight());
        SpyReporter* spy = Spy().load();
        if (spy == nullptr)
            return;
        if (Busy().exchange(true)) {
            Skipped().fetch_add(1);
            return;
        }
        const ClearOnExit notBusy(Busy());     // also if Report() throws
        char topic[48];
        snprintf(topic, sizeof(topic), "log/%s/%s", dmq::LogLevelName(level), tag);
        const uint64_t nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            dmq::Clock::now().time_since_epoch()).count());
        spy->Report(topic, msg, nowUs);
    }

private:
    struct Counted {
        explicit Counted(std::atomic<uint32_t>& n) : m_n(n) { m_n.fetch_add(1); }
        ~Counted() { m_n.fetch_sub(1); }
        std::atomic<uint32_t>& m_n;
    };
    struct ClearOnExit {
        explicit ClearOnExit(std::atomic<bool>& flag) : m_flag(flag) {}
        ~ClearOnExit() { m_flag.store(false); }
        std::atomic<bool>& m_flag;
    };

    static std::atomic<SpyReporter*>& Spy() { static std::atomic<SpyReporter*> s{ nullptr }; return s; }
    static std::atomic<dmq::LogHandler>& Next() { static std::atomic<dmq::LogHandler> n{ nullptr }; return n; }
    static std::atomic<bool>& Busy() { static std::atomic<bool> b{ false }; return b; }
    static std::atomic<uint32_t>& Skipped() { static std::atomic<uint32_t> n{ 0 }; return n; }
    static std::atomic<uint32_t>& InFlight() { static std::atomic<uint32_t> n{ 0 }; return n; }
};

} // namespace dmq::databus

#endif // DMQ_SPY_LOG_SINK_H
