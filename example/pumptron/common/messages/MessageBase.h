#ifndef PUMPTRON_MESSAGE_BASE_H
#define PUMPTRON_MESSAGE_BASE_H

#include "DelegateMQ.h"
#include <atomic>
#include <cstdint>

namespace pumptron {

/// @brief Base class for all Pumptron messages.
/// @details Stamps every message with a process-local monotonic sequence number
///          at construction time. Receivers can hold a dmq::util::MonotonicGuard
///          and discard stale or reordered arrivals (e.g. a late RELIABLE retry
///          that lands after a newer value).
///
/// @note The counter is per-process, so it is only meaningful for
///       single-publisher-per-topic scenarios, which is how Pumptron is wired:
///       the controller owns every status/telemetry topic and the GUI owns the
///       command topic.
struct MessageBase : public serialize::I
{
    XALLOCATOR
    uint32_t seq = 0;

    MessageBase() : seq(NextSeq()) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        return ms.read(is, seq);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        return ms.write(os, seq);
    }

private:
    static uint32_t NextSeq() {
        static std::atomic<uint32_t> s_seq{1};
        return s_seq.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace pumptron

#endif
