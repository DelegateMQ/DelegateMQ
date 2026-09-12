#ifndef NUTTX_DELEGATE_QUEUE_H
#define NUTTX_DELEGATE_QUEUE_H

/// @file NuttXDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around a POSIX mqueue used by dmq::os::Thread
/// to carry pending ThreadMsg pointers, with priority support.
///
/// @details
/// Unlike Zephyr's k_msgq (strict FIFO, no native priority -- see
/// ZephyrDelegateQueue.h's two-k_msgq workaround), POSIX mq_send() takes a
/// msg_prio argument directly: a higher-priority message is dequeued ahead
/// of lower-priority ones already queued, FIFO within the same priority.
/// A single mqd_t therefore handles both dmq::Priority lanes, the same way
/// CmsisRtos2DelegateQueue.h uses osMessageQueuePut's native msg_prio --
/// no second queue needed here. This class owns no policy decisions of its
/// own (FullPolicy, timeout computation, and stats stay in Thread.cpp) --
/// it only knows how to move ThreadMsg* pointers through a named POSIX
/// message queue.
///
/// Each instance opens its own uniquely-named queue (process id + a
/// monotonic counter) since POSIX message queues live in a global,
/// filesystem-like namespace -- two dmq::os::Thread instances must never
/// collide on the same name. Destroy() closes and unlinks it.
///
/// @warning UNVERIFIED: written against the documented NuttX/POSIX mqueue
/// API but never compiled or run against a real NuttX toolchain (none is
/// available in this development environment). Review this file with extra
/// care and exercise it on real hardware or `nuttx/boards/sim` before
/// relying on it in production.

#include "port/os/common/ThreadMsg.h"
#include "delegate/DelegateOpt.h"
#include <mqueue.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <atomic>

namespace dmq::os {

class NuttXDelegateQueue
{
public:
    /// Sentinel meaning "block forever" for Send()/Receive()'s timeout
    /// parameter -- dmq::Duration has no dedicated "infinite" value of its
    /// own, so this mirrors the K_FOREVER/osWaitForever sentinel every
    /// other dmq::os::Thread port already needs.
    static constexpr dmq::Duration WAIT_FOREVER = dmq::Duration::max();

    NuttXDelegateQueue() = default;
    ~NuttXDelegateQueue() { Destroy(); }

    NuttXDelegateQueue(const NuttXDelegateQueue&) = delete;
    NuttXDelegateQueue& operator=(const NuttXDelegateQueue&) = delete;

    /// Create the underlying queue with room for 'capacity' pointers.
    /// @return true on success, false if mq_open() failed.
    bool Create(size_t capacity)
    {
        if (m_mqd != INVALID_MQD)
            return true;

        static std::atomic<uint32_t> s_counter{0};
        std::snprintf(m_name, sizeof(m_name), "/dmq_q_%u_%u",
                      static_cast<unsigned>(getpid()), s_counter.fetch_add(1));

        struct mq_attr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.mq_maxmsg = static_cast<long>(capacity);
        attr.mq_msgsize = static_cast<long>(sizeof(ThreadMsg*));

        m_mqd = mq_open(m_name, O_CREAT | O_RDWR, 0600, &attr);
        if (m_mqd != INVALID_MQD) {
            mq_unlink(m_name);
        }
        return m_mqd != INVALID_MQD;
    }

    bool IsCreated() const { return m_mqd != INVALID_MQD; }

    /// Send a message pointer. 'highPriority' is passed as mq_send()'s
    /// native msg_prio argument -- a higher-priority message is delivered
    /// ahead of lower-priority ones already queued, no second queue needed.
    /// 'timeout' is Duration::zero() for non-blocking, WAIT_FOREVER to block
    /// forever, or a finite duration for a bounded wait.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, dmq::Duration timeout)
    {
        unsigned prio = highPriority ? 1u : 0u;
        const char* data = reinterpret_cast<const char*>(&msg);

        if (timeout == WAIT_FOREVER)
            return mq_send(m_mqd, data, sizeof(msg), prio) == 0;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        AddDuration(ts, timeout);
        return mq_timedsend(m_mqd, data, sizeof(msg), prio, &ts) == 0;
    }

    /// Receive a message pointer, blocking up to 'timeout'. The
    /// high-priority lane is always delivered first since it is the
    /// kernel's own mqueue ordering, not something this wrapper implements.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(dmq::Duration timeout)
    {
        char buf[sizeof(ThreadMsg*)];
        unsigned prio = 0;
        ssize_t n;

        if (timeout == WAIT_FOREVER) {
            n = mq_receive(m_mqd, buf, sizeof(buf), &prio);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            AddDuration(ts, timeout);
            n = mq_timedreceive(m_mqd, buf, sizeof(buf), &prio, &ts);
        }

        if (n == static_cast<ssize_t>(sizeof(ThreadMsg*))) {
            ThreadMsg* msg = nullptr;
            std::memcpy(&msg, buf, sizeof(msg));
            return msg;
        }
        return nullptr;
    }

    /// Current number of messages waiting in the queue.
    size_t Size() const
    {
        if (m_mqd == INVALID_MQD)
            return 0;
        struct mq_attr attr;
        if (mq_getattr(m_mqd, &attr) == 0)
            return static_cast<size_t>(attr.mq_curmsgs);
        return 0;
    }

    /// Remove and delete every pending message without blocking. Used during
    /// shutdown once the owning thread has stopped consuming the queue.
    void DrainAndDelete()
    {
        while (true) {
            ThreadMsg* msg = Receive(dmq::Duration::zero());
            if (!msg)
                break;
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_mqd != INVALID_MQD) {
            mq_close(m_mqd);
            mq_unlink(m_name);
            m_mqd = INVALID_MQD;
        }
    }

private:
    static constexpr mqd_t INVALID_MQD = static_cast<mqd_t>(-1);

    /// Add 'timeout' (clamped to zero for a non-positive/immediate wait) to
    /// 'ts' in place, carrying seconds/nanoseconds correctly.
    static void AddDuration(struct timespec& ts, dmq::Duration timeout)
    {
        if (timeout < dmq::Duration::zero())
            timeout = dmq::Duration::zero();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
        ts.tv_sec += static_cast<time_t>(ns / 1000000000LL);
        ts.tv_nsec += static_cast<long>(ns % 1000000000LL);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_nsec -= 1000000000L;
            ts.tv_sec += 1;
        }
    }

    mqd_t m_mqd = INVALID_MQD;
    char m_name[32] = {0};
};

} // namespace dmq::os

#endif // NUTTX_DELEGATE_QUEUE_H
