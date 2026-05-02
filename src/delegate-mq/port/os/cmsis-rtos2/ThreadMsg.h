#ifndef _THREAD_MSG_H
#define _THREAD_MSG_H

#include "delegate/DelegateOpt.h"
#include <memory>

// Message IDs
#define MSG_DISPATCH_DELEGATE   1
#define MSG_EXIT_THREAD         2

namespace dmq::os {

class ThreadMsg
{
public:
    // Constructor for generic messages
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

    XALLOCATOR
};

} // namespace dmq::os

#endif