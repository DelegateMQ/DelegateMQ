#ifndef SENSOR_MESSAGE_BASE_H
#define SENSOR_MESSAGE_BASE_H

/// @file MessageBase.h
/// @brief Base class for every sensor-node message (the same pattern as
/// Cellutron's and Pumptron's MessageBase).

#include "DelegateMQ.h"
#include "extras/util/MonotonicGuard.h"

#include <atomic>
#include <cstdint>

namespace sensornode {

/// @brief Stamps every message with a process-local, increasing sequence
/// number at construction, and serializes it ahead of the message's fields.
/// @details A copy keeps its seq, so a retry (RetryMonitor re-sends the same
/// bytes) or a second Publish() of the same object repeats it. A receiver holds
/// a dmq::util::MonotonicGuard per topic and drops anything not newer than the
/// last it accepted: a retried duplicate, or an old retry that arrives after a
/// newer command.
///
/// @note One counter per process, shared by all its messages, so a topic's seq
/// increases but skips values. A restarted sender counts from 1 again: its
/// receivers must reset their guards (the node does when a Start brings a new
/// client session).
struct MessageBase : public ::serialize::I
{
    XALLOCATOR
    uint32_t seq = 0;

    MessageBase() : seq(NextSeq()) {}

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        return ms.write(os, seq);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        return ms.read(is, seq);
    }

private:
    static uint32_t NextSeq() {
        static std::atomic<uint32_t> s_seq{1};
        return s_seq.fetch_add(1, std::memory_order_relaxed);
    }
};

/// A receiver's guard for one topic: IsNewer(msg.seq) accepts only a newer
/// seq. Not thread safe: use it on the thread that handles the topic.
using MessageGuard = dmq::util::MonotonicGuard<uint32_t>;

} // namespace sensornode

#endif // SENSOR_MESSAGE_BASE_H
