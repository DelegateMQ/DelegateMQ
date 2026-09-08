#ifndef DMQ_OS_COMMON_THREAD_MSG_H
#define DMQ_OS_COMMON_THREAD_MSG_H

/// @file ThreadMsg.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief A class to hold a platform-specific thread message that will be passed
/// through a dmq::os::Thread port's OS message queue.
///
/// @details
/// Identical across every dmq::os::Thread port (FreeRTOS, ThreadX, Zephyr,
/// CMSIS-RTOS2, stdlib, Win32) except Qt, which dispatches via Qt's own
/// signal/slot queued-connection mechanism instead of a ThreadMsg wrapper --
/// kept as a single shared file here instead of one copy per port directory.

#include "delegate/DelegateOpt.h"
#include <memory>

namespace dmq::os {

// Message IDs
#define MSG_DISPATCH_DELEGATE   1
#define MSG_EXIT_THREAD         2

class ThreadMsg
{
public:
    /// Constructor
    /// @param[in] id - a unique identifier for the thread message
    /// @param[in] data - a pointer to the message data to be typecast
    ///     by the receiving task based on the id value.
    ThreadMsg(int id, std::shared_ptr<dmq::DelegateMsg> data = nullptr)
        : m_id(id), m_data(data) {
    }

    virtual ~ThreadMsg() = default;

    int GetId() const { return m_id; }
    std::shared_ptr<dmq::DelegateMsg> GetData() const { return m_data; }

    /// Get the message priority
    dmq::Priority GetPriority() const {
        return m_data ? m_data->GetPriority() : dmq::Priority::NORMAL;
    }

#if defined(DMQ_DATABUS_TOOLS)
    void SetEnqueueTime(dmq::TimePoint time) { m_enqueueTime = time; }
    dmq::TimePoint GetEnqueueTime() const { return m_enqueueTime; }
#endif

private:
    int m_id;
    std::shared_ptr<dmq::DelegateMsg> m_data;
#if defined(DMQ_DATABUS_TOOLS)
    dmq::TimePoint m_enqueueTime;
#endif

    // Use fixed-block memory allocator if DMQ_ALLOCATOR set
    XALLOCATOR
};

} // namespace dmq::os

#endif // DMQ_OS_COMMON_THREAD_MSG_H
