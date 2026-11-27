#ifndef DMQ_STACK_WATERMARK_H
#define DMQ_STACK_WATERMARK_H

/// @file StackWatermark.h
/// @brief A thread's stack high-water mark, readable from any thread, also
/// after the thread has exited.
///
/// The RTOS ports measure free stack with a kernel call on the thread's control
/// block or a scan of its stack. Both must not run once the kernel or the port
/// frees that memory, which can happen while another thread (ThreadMonitor) is
/// asking. StackWatermark closes that window without a kernel object:
/// - Start() when the thread is created: queries go to the live thread.
/// - Get() counts itself in while it queries, and returns the final value
///   once the thread has finished.
/// - Finish() records the final value, stops new live queries, then waits for
///   any query already running. Call it before the stack or control block can
///   go away: from the thread itself just before it exits (FreeRTOS,
///   CMSIS-RTOS2, whose kernels free those on self-delete), or from the
///   cleanup in ExitThread() before the memory is freed (ThreadX, Zephyr).
///
/// Ports include this header directly, as they do ThreadMsg.h.

#include "delegate/DelegateOpt.h"
#include <atomic>
#include <cstdint>

namespace dmq::os::detail {

class StackWatermark
{
public:
    /// The thread is running: queries go to it.
    void Start()
    {
        m_final.store(dmq::STACK_UNKNOWN);
        m_live.store(true);
    }

    /// The least free stack seen, in bytes: from query() while the thread is
    /// live, then the final value; dmq::STACK_UNKNOWN before the thread starts.
    /// @param query a callable returning the live thread's free stack in bytes
    template <typename Query>
    uint32_t Get(Query query)
    {
        m_queries.fetch_add(1);
        uint32_t free = m_live.load() ? query() : m_final.load();
        m_queries.fetch_sub(1);
        return free;
    }

    /// Record the final value and stop live queries. Returns once no query is
    /// running, after which the stack and control block may be freed.
    /// @param finalFree the thread's free stack, measured now
    /// @param pause called while a query is still running (a short sleep)
    template <typename Pause>
    void Finish(uint32_t finalFree, Pause pause)
    {
        m_final.store(finalFree);
        m_live.store(false);
        while (m_queries.load() != 0)
            pause();
    }

    bool IsLive() const { return m_live.load(); }

private:
    std::atomic<bool> m_live{false};
    std::atomic<uint32_t> m_queries{0};
    std::atomic<uint32_t> m_final{dmq::STACK_UNKNOWN};
};

} // namespace dmq::os::detail

#endif // DMQ_STACK_WATERMARK_H
