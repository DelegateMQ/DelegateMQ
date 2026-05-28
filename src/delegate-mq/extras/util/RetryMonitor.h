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
public:
    /// @brief Storage for a message that might need retransmission.
    struct RetryEntry {
        dmq::xstring packetData;    ///< The raw serialized arguments
        dmq::transport::DmqHeader header;           ///< Original metadata (ID, SeqNum, etc.)
        int attemptsRemaining;      ///< Counter for retry budget
    };

    /// @brief Constructor
    /// @param transport The underlying transport to use for re-sending.
    /// @param monitor The monitor that detects the timeouts.
    /// @param maxRetries Number of retries before giving up (default 3).
    RetryMonitor(dmq::transport::ITransport& transport, TransportMonitor& monitor, int maxRetries = 3)
        : m_transport(transport), m_monitor(monitor), m_maxRetries(maxRetries) 
    {
        // Connection handled via RAII dmq::Connection member
        m_connection = m_monitor.OnSendStatus.Connect(dmq::MakeDelegate(this, &RetryMonitor::OnStatusChanged));
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
        // Critical Section: Store the packet for retry before sending.
        // If Send() fails we remove the entry immediately so it doesn't leak.
        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            RetryEntry entry;
            entry.attemptsRemaining = m_maxRetries;
            entry.header = header;
            entry.packetData = os.str(); // Copy data
            m_retryStore[header.GetSeqNum()] = entry;
        }

        // Non-Critical Section: Send via Transport.
        // We must NOT hold m_lock while calling Send().
        // Send() calls TransportMonitor::Add(), which takes its own lock.
        int result = m_transport.Send(os, header);

        // If the send failed, TransportMonitor::Add() was never called so
        // OnStatusChanged() will never fire for this seqNum. Remove the entry
        // now to prevent it from leaking in m_retryStore indefinitely.
        if (result != 0)
        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_lock);
            m_retryStore.erase(header.GetSeqNum());
        }

        return result;
    }

private:
    /// @brief Callback handled when a message is either ACK'd or Timed Out.
    void OnStatusChanged(dmq::DelegateRemoteId id, uint16_t seqNum, TransportMonitor::Status status)
    {
        (void)id;
        // Variables to hold data for the retry OUTSIDE the lock
        bool shouldRetry = false;
        dmq::xstring retryPayload;
        dmq::transport::DmqHeader retryHeader;

        {
            // 1. Critical Section: Read/Modify Map ONLY
            const std::lock_guard<dmq::RecursiveMutex> lock(m_lock);

            auto it = m_retryStore.find(seqNum);
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
        if (shouldRetry)
        {
            // Re-prepare the stream from our local copy
            dmq::xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            os.write(retryPayload.data(), retryPayload.size());

            // Re-send. The transport will re-add this to the TransportMonitor.
            // This runs without holding m_lock, preventing a deadlock.
            m_transport.Send(os, retryHeader);
        }
    }

    dmq::transport::ITransport& m_transport;
    TransportMonitor& m_monitor;
    const int m_maxRetries;
    xmap<uint16_t, RetryEntry> m_retryStore;
    dmq::RecursiveMutex m_lock;
    dmq::ScopedConnection m_connection;
};

} // namespace dmq::util


#endif // _RETRY_MONITOR_H
