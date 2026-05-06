#ifndef MESSAGE_BASE_H
#define MESSAGE_BASE_H

#include "DelegateMQ.h"
#include <atomic>

namespace cellutron {

/// @brief Base class for all Cellutron messages.
/// @details Stamps every message with a process-local monotonic sequence number
///          at construction time. Receivers use MessageGuard::IsNewer(msg.seq)
///          to discard stale or reordered messages.
///
/// @note The counter is per-process, not synchronized across CPUs. MonotonicGuard
///       is therefore only valid for single-publisher-per-topic scenarios. If a
///       publisher process restarts, receivers must also call MessageGuard::Reset()
///       to accept the restarted publisher's messages.
struct MessageBase : public serialize::I
{
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
