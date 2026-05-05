#ifndef _TRANSPORT_MONITOR_HH
#define _TRANSPORT_MONITOR_HH

#include "delegate/DelegateOpt.h"
#include "delegate/Signal.h"
#include "../../port/transport/ITransportMonitor.h"
#include <map>
#include <cstdint>
#include <chrono>
#include <vector>
#include <iostream>

namespace dmq::util {

/// @brief A thread-safe monitor for tracking outgoing remote messages and detecting timeouts.
/// 
/// @details 
/// The TransportMonitor implements the reliability layer for remote delegate invocations. 
/// It tracks "in-flight" messages by their sequence number and timestamps them upon sending.
///
/// **Key Responsibilities:**
/// * **Timeout Detection:** Identifies messages that have not been acknowledged within the 
///   configured `TRANSPORT_TIMEOUT` duration.
/// * **Status Reporting:** Invokes the `SendStatusCb` delegate with `Status::SUCCESS` (upon ACK) 
///   or `Status::TIMEOUT` (upon expiration) to notify the application.
/// * **Thread Safety:** Internal state is protected by a recursive mutex, allowing safe access 
///   from multiple threads (e.g., sending thread vs. ACK receiving thread).
///
/// **Usage Note:**
/// This class relies on a cooperative polling model. The `Process()` method must be called 
/// periodically (typically by a background timer or the network thread loop) to scan for 
/// and handle expired messages.
class TransportMonitor : public dmq::transport::ITransportMonitor
{
public:
    enum class Status
    {
        SUCCESS,  // Message received by remote
        TIMEOUT   // Message timeout
    };

    /// Signal emitted when a message status is determined.
    /// Subscribers receive: (remoteId, seqNum, status)
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t, Status)> OnSendStatus;

    TransportMonitor(const dmq::Duration timeout = std::chrono::seconds(2)) : TRANSPORT_TIMEOUT(timeout)
    {
    }

    ~TransportMonitor()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
        m_pending.clear();
    }

    /// Add a sequence number
    /// param[in] seqNum - the delegate message sequence number
    /// param[in] remoteId - the remote ID
    virtual void Add(uint16_t seqNum, dmq::DelegateRemoteId remoteId) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
        TimeoutData d;
        d.timeStamp = dmq::Clock::now();
        d.remoteId = remoteId;
        m_pending[seqNum] = d;
    }

    /// Remove a sequence number. Invokes SendStatusCb callback to notify 
    /// registered client of removal.
    /// param[in] seqNum - the delegate message sequence number
    virtual void Remove(uint16_t seqNum) override
    {
        bool found = false;
        TimeoutData d;
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            auto it = m_pending.find(seqNum);
            if (it != m_pending.end())
            {
                d = it->second;
                m_pending.erase(it);
                found = true;
            }
        }

        if (found)
        {
            OnSendStatus(d.remoteId, seqNum, Status::SUCCESS);
        }
    }

    /// Call periodically to process message timeouts
    void Process()
    {
        // 1. Collect expired items into a local list
        struct ExpiredItem { uint16_t seq; TimeoutData data; };
        std::vector<ExpiredItem> expiredItems;

        {
            // Lock ONLY while reading/modifying the map
            const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);

            if (m_pending.empty())
                return;

            auto now = dmq::Clock::now();
            auto it = m_pending.begin();

            // Optimization: Since we are using a map and potentially high volume,
            // we want to limit the time spent holding this lock if there are many items.
            // However, we must ensure all expired items are caught.
            while (it != m_pending.end())
            {
                // Ensure consistent duration types
                auto elapsed = std::chrono::duration_cast<dmq::Duration>(now - (*it).second.timeStamp);

                if (elapsed > TRANSPORT_TIMEOUT)
                {
                    expiredItems.push_back({ (*it).first, (*it).second });
                    it = m_pending.erase(it);
                }
                else
                {
                    // If items are added in chronological order, we could break here,
                    // but map is sorted by seqNum, not time. So we must continue.
                    ++it;
                }
            }
        } // Lock is RELEASED here

        // 2. Fire callbacks without holding the lock
        for (const auto& item : expiredItems)
        {
            OnSendStatus(item.data.remoteId, item.seq, Status::TIMEOUT);
        }
    }

private:
    struct TimeoutData
    {
        dmq::DelegateRemoteId remoteId = 0;
        dmq::TimePoint timeStamp;
    };

    std::map<uint16_t, TimeoutData> m_pending;
    const dmq::Duration TRANSPORT_TIMEOUT;
    dmq::RecursiveMutex m_lock;
};

} // namespace dmq::util


#endif
