#ifndef _RETRY_MONITOR_H
#define _RETRY_MONITOR_H

#include "delegate/DelegateOpt.h"
#include "delegate/DelegateRemote.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "TransportMonitor.h"
#include <mutex>
#include <cstdint>
#include <vector>
#include <string>

namespace dmq::util {

/// @file RetryMonitor.h
/// @brief Automatic retransmission manager for DelegateMQ remote calls.
/// 
/// @details
/// The RetryMonitor acts as a reliability decorator for any ITransport implementation.
/// It bridges the gap between detection (TransportMonitor) and recovery (Physical Transport).
///
/// ### Core Responsibilities
/// 1. **Data Persistence**: Stores the fully serialized binary payload of every outgoing 
///    remote delegate call, indexed by its unique Sequence Number.
/// 2. **Timeout Handling**: Subscribes to the `TransportMonitor::OnSendStatus` signal.
/// 3. **Automatic Recovery**: If a TIMEOUT status is received, it decrements the retry 
///    counter and re-submits the exact binary packet to the transport.
/// 4. **Cleanup**: Discards stored packets upon SUCCESS (ACK received) or when 
///    `maxRetries` is exhausted.
///
/// ### Sequence Number Integrity
/// Retries use the **original** sequence number. This allows the Receiver to perform 
/// idempotency checks (filtering out duplicate calls if an ACK was lost but the 
/// function was already executed).
///
/// ### Reentrancy Note
/// This class calls `ITransport::Send()`. If the underlying transport (e.g., SerialTransport)
/// routes its high-level `Send()` back into this monitor, the transport MUST implement 
/// a reentrancy guard to prevent infinite recursion.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
class RetryMonitor 
{
    XALLOCATOR
public:
    /// @brief Storage for a message that might need retransmission.
    struct RetryEntry {
        dmq::xstring packetData;    ///< The raw serialized arguments
        dmq::transport::DmqHeader header;           ///< Original metadata (ID, SeqNum, etc.)
        int attemptsRemaining;      ///< Counter for retry budget
    };

    RetryMonitor() = default;

    /// @brief Constructor
    /// @param transport The underlying transport to use for re-sending.
    /// @param monitor The monitor that detects the timeouts.
    /// @param maxRetries Number of retries before giving up (default 3).
    RetryMonitor(dmq::transport::ITransport& transport, TransportMonitor& monitor, int maxRetries = 3)
    {
        Init(transport, monitor, maxRetries);
    }

    void Init(dmq::transport::ITransport& transport, TransportMonitor& monitor, int maxRetries = 3)
    {
        m_transport  = &transport;
        m_monitor    = &monitor;
        m_maxRetries = maxRetries;
        m_connection = m_monitor->OnSendStatus.Connect(dmq::MakeDelegate(this, &RetryMonitor::OnStatusChanged));
    }

    ~RetryMonitor() {
        m_connection.Disconnect();

        const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
        m_retryStore.clear();
    }

    /// @brief Sends a message and tracks it for potential retries.
    /// @return 0 on success, -1 on immediate transport failure.
    int SendWithRetry(dmq::xostringstream& os, const dmq::transport::DmqHeader& header)
    {
        ASSERT_TRUE(m_transport != nullptr);
        // Critical Section: Store the packet for retry before sending.
        // If Send() fails we remove the entry immediately so it doesn't leak.
        uint32_t key = (static_cast<uint32_t>(header.GetId()) << 16) | header.GetSeqNum();
        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            RetryEntry entry;
            entry.attemptsRemaining = m_maxRetries;
            entry.header = header;
            entry.packetData = os.str(); // Copy data
            m_retryStore[key] = entry;
        }

        // Non-Critical Section: Send via Transport.
        // We must NOT hold m_lock while calling Send().
        // Send() calls TransportMonitor::Add(), which takes its own lock.
        bool added = true;
        if (m_monitor)
            added = m_monitor->Add(header.GetSeqNum(), header.GetId());
        
        if (!added) {
            std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            m_retryStore.erase(key);
            return -1;
        }

        int result = m_transport->Send(os, header);

        // If the send failed, TransportMonitor::Add() was never called so
        // OnStatusChanged() will never fire for this seqNum. Remove the entry
        // now to prevent it from leaking in m_retryStore indefinitely.
        if (result != 0)
        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            m_retryStore.erase(key);
        }

        return result;
    }

private:
    void OnStatusChanged(dmq::DelegateRemoteId id, uint16_t seqNum, TransportMonitor::Status status)
    {
        // Variables to hold data for the retry OUTSIDE the lock
        bool shouldRetry = false;
        dmq::xstring retryPayload;
        dmq::transport::DmqHeader retryHeader;
        uint32_t key = (static_cast<uint32_t>(id) << 16) | seqNum;

        {
            // 1. Critical Section: Read/Modify Map ONLY
            const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);

            auto it = m_retryStore.find(key);
            if (it == m_retryStore.end()) return;

            if (status == TransportMonitor::Status::SUCCESS)
            {
                // Message arrived safely, we can forget about the data now
                m_retryStore.erase(it);
                return; // Done
            }
            else if (status == TransportMonitor::Status::TIMEOUT)
            {
                if (it->second.attemptsRemaining > 0)
                {
                    // Decrement counter
                    it->second.attemptsRemaining--;

                    // COPY data to local variables so we can use them after unlocking
                    retryPayload = it->second.packetData;
                    retryHeader = it->second.header;
                    shouldRetry = true;
                }
                else
                {
                    // Max retries exceeded. Clean up.
                    // LOG_ERROR("RetryMonitor: Max retries exceeded for seq {}", seqNum);
                    m_retryStore.erase(it);
                }
            }
        } // <--- LOCK IS RELEASED HERE

        // 2. Non-Critical Section: Perform blocking network operations
        if (shouldRetry && m_transport)
        {
            dmq::xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            os.write(retryPayload.data(), retryPayload.size());
            
            bool added = true;
            if (m_monitor)
                added = m_monitor->Add(retryHeader.GetSeqNum(), retryHeader.GetId());
            
            if (added) {
                m_transport->Send(os, retryHeader);
            } else {
                std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
                m_retryStore.erase(key);
            }
        }
    }

    dmq::transport::ITransport* m_transport = nullptr;
    TransportMonitor*           m_monitor   = nullptr;
    int                         m_maxRetries = 3;
    dmq::xmap<uint32_t, RetryEntry> m_retryStore;
    dmq::RecursiveMutex m_lock;
    dmq::ScopedConnection m_connection;
};

} // namespace dmq::util


#endif // _RETRY_MONITOR_H
