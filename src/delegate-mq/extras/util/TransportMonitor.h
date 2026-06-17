#ifndef _TRANSPORT_MONITOR_HH
#define _TRANSPORT_MONITOR_HH

#include "delegate/DelegateOpt.h"
#include "delegate/Signal.h"
#include "../../port/transport/ITransportMonitor.h"
#include <cstdint>
#include <array>
#include <chrono>

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
    XALLOCATOR
public:
    enum class Status
    {
        SUCCESS,  // Message received by remote
        TIMEOUT   // Message timeout
    };

    /// Signal emitted when a message status is determined.
    /// Subscribers receive: (remoteId, seqNum, status)
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t, Status)> OnSendStatus;

    /// Signal emitted when the pending map hits MAX_TRANSPORT_MONITOR_PENDING.
    /// Subscribers receive: (currentSize) — the number of unacknowledged messages at the time of rejection.
    /// Fired outside the internal lock so subscribers may call back into TransportMonitor safely.
    dmq::Signal<void(size_t)> OnCapExceeded;

    /// Signal emitted when Process() fills its batch buffer before emptying m_pending.
    /// Subscribers receive: (remaining) — number of entries still in m_pending after the current pass.
    /// Indicates that expired messages are accumulating faster than Process() is being called.
    /// Fired outside the internal lock so subscribers may call back into TransportMonitor safely.
    dmq::Signal<void(size_t)> OnPendingExceeded;

    TransportMonitor(const dmq::Duration timeout = std::chrono::seconds(2)) : TRANSPORT_TIMEOUT(timeout) {}

    ~TransportMonitor()
    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_lock);
        m_pending.clear();
    }

    /// Add a sequence number
    /// param[in] seqNum - the delegate message sequence number
    /// param[in] remoteId - the remote ID
    /// @return true if added; false otherwise.
    virtual bool Add(uint16_t seqNum, dmq::DelegateRemoteId remoteId) override
    {
        size_t capSize = 0;
        uint32_t key = (static_cast<uint32_t>(remoteId) << 16) | seqNum;
        {
            const dmq::LockGuard<dmq::RecursiveMutex> lock(m_lock);

            if (m_pending.size() >= dmq::MAX_TRANSPORT_MONITOR_PENDING) {
                capSize = m_pending.size();
            } else {
                TimeoutData d;
                d.timeStamp = dmq::Clock::now();
                d.remoteId = remoteId;
                d.seqNum = seqNum;
                m_pending[key] = d;
            }
        }

        if (capSize > 0) {
            LOG_ERROR("TransportMonitor: Pending map full ({} entries). Check ACK logic or comm link.", capSize);
            OnCapExceeded(capSize);
            return false;
        }

        return true;
    }

    /// Remove a sequence number. Invokes SendStatusCb callback to notify 
    /// registered client of removal.
    /// param[in] seqNum - the delegate message sequence number
    /// param[in] remoteId - the remote ID (default 0 for backwards compatibility, but strongly recommended)
    virtual void Remove(uint16_t seqNum, dmq::DelegateRemoteId remoteId = 0)
    {
        bool found = false;
        TimeoutData d;
        uint32_t key = (static_cast<uint32_t>(remoteId) << 16) | seqNum;
        
        {
            const dmq::LockGuard<dmq::RecursiveMutex> lock(m_lock);
            
            if (remoteId != 0) {
                // Exact composite key lookup
                auto it = m_pending.find(key);
                if (it != m_pending.end()) {
                    d = it->second;
                    m_pending.erase(it);
                    found = true;
                }
            } else {
                // Backwards compatibility: linear scan for seqNum if remoteId is unknown
                for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                    if (it->second.seqNum == seqNum) {
                        d = it->second;
                        m_pending.erase(it);
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found)
        {
            OnSendStatus(d.remoteId, seqNum, Status::SUCCESS);
        }
    }

    // Standard ITransportMonitor override (uses default remoteId 0)
    virtual void Remove(uint16_t seqNum) override { Remove(seqNum, 0); }

    /// Call periodically to process message timeouts.
    /// Drains all expired entries across multiple passes so a single call always
    /// fully clears the backlog, regardless of how many entries have timed out.
    void Process()
    {
        size_t expiredCount = 0;

        do {
            expiredCount = 0;
            size_t remaining = 0;

            {
                // Lock ONLY while reading/modifying the map
                const dmq::LockGuard<dmq::RecursiveMutex> lock(m_lock);

                if (m_pending.empty())
                    return;

                auto now = dmq::Clock::now();
                auto it = m_pending.begin();

                while (it != m_pending.end())
                {
                    auto elapsed = std::chrono::duration_cast<dmq::Duration>(now - (*it).second.timeStamp);

                    if (elapsed > TRANSPORT_TIMEOUT)
                    {
                        if (expiredCount >= dmq::MAX_TIMER_EXPIRED) {
                            remaining = m_pending.size();
                            break;
                        }
                        m_expiredItems[expiredCount++] = { (*it).second.seqNum, (*it).second };
                        it = m_pending.erase(it);
                    }
                    else
                    {
                        // map is sorted by key, not time — must scan all entries
                        ++it;
                    }
                }
            } // Lock is RELEASED here

            // Fire timeout callbacks without holding the lock
            for (size_t i = 0; i < expiredCount; ++i)
            {
                OnSendStatus(m_expiredItems[i].data.remoteId, m_expiredItems[i].seq, Status::TIMEOUT);
            }

            // Notify app if a full pass still left entries; loop to drain them
            if (remaining > 0) {
                LOG_ERROR("TransportMonitor: Cleanup bottleneck — {} entries remain after pass. Continuing drain.", remaining);
                OnPendingExceeded(remaining);
            }

        } while (expiredCount == dmq::MAX_TIMER_EXPIRED);
    }

private:
    struct TimeoutData
    {
        dmq::DelegateRemoteId remoteId = 0;
        uint16_t seqNum = 0;
        dmq::TimePoint timeStamp;
    };

    struct ExpiredItem { uint16_t seq; TimeoutData data; };

    // Key is (remoteId << 16) | seqNum
    dmq::xmap<uint32_t, TimeoutData> m_pending;
    std::array<ExpiredItem, dmq::MAX_TIMER_EXPIRED> m_expiredItems{};
    const dmq::Duration TRANSPORT_TIMEOUT;
    dmq::RecursiveMutex m_lock;
};

} // namespace dmq::util


#endif
