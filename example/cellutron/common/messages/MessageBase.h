#ifndef MESSAGE_BASE_H
#define MESSAGE_BASE_H

#include "DelegateMQ.h"
#include <atomic>

namespace cellutron {

/// @brief Base class for all Cellutron messages.
/// @details Stamps every message with a process-local monotonic uint32_t sequence
///          number at construction time. Receivers hold a MessageGuard and call
///          guard.IsNewer(msg.seq) to silently discard stale or reordered arrivals.
///
///          The primary scenario this guards against is the DataBus LVC rewind:
///          when a subscriber connects to a Last Value Cache topic, it may receive
///          a fresh live value followed by an older cached value delivered slightly
///          late. IsNewer() detects and drops the stale cached value automatically.
///
/// @note The counter is per-process and not synchronized across CPUs, so it is
///       only meaningful for single-publisher-per-topic scenarios. If a publisher
///       process restarts, receivers must call MessageGuard::Reset() to accept the
///       restarted publisher's sequence numbers.
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

} // namespace cellutron

#endif
