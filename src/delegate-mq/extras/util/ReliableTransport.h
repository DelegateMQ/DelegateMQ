#ifndef RELIABLE_TRANSPORT_H
#define RELIABLE_TRANSPORT_H

/// @file ReliableTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Reliability adapter for the DelegateMQ transport layer.
/// 
/// @details
/// This class applies the **Decorator Pattern** to any existing `ITransport` implementation,
/// adding automatic retransmission capabilities without modifying the underlying transport logic.
/// 
/// ### The Reliability Stack
/// It functions as the entry point to the reliability system, routing outgoing messages 
/// through the `RetryMonitor` to ensure delivery guarantees (ACKs) are met.
/// 
/// @verbatim
///        Application
///             | Send()
///             v
///  +-----------------------+
///  |   ReliableTransport   |  <-- Adapter / Entry Point
///  +-----------------------+
///             |
///             v
///  +-----------------------+                      +------------------+
///  |     RetryMonitor      | <----(Signaled)----- | TransportMonitor |
///  | (Manages Re-sending)  |                      | (Tracks Timeouts)|
///  +-----------------------+                      +------------------+
///             |                                            ^
///             | Send()                                     | Add(SeqNum)
///             v                                            |
///  +-----------------------+                               |
///  |   PhysicalTransport   | ------------------------------+
///  | (TCP, UDP, Serial...) |
///  +-----------------------+
///             |
///             v
///          Network
/// @endverbatim
/// 
/// **Key Responsibilities:**
/// 1. **Send Interception**: Redirects `Send()` calls to `RetryMonitor::SendWithRetry()`.
/// 2. **Receive Pass-through**: Forwards `Receive()` calls directly to the physical transport 
///    (no interference with incoming data).
/// 
/// @brief Notes on Packet Ordering and Reliability
///
/// The DelegateMQ reliability layer (RetryMonitor) ensures *eventual delivery*, 
/// but the order of execution depends heavily on the underlying transport:
///
/// 1. **UDP / Serial (Unordered Transports)**:
///    These transports do not buffer or reorder packets. If Packet #2 is lost 
///    and re-transmitted by the RetryMonitor, the Receiver will process them 
///    in the order of arrival: #1 -> #3 -> #2 (Retry). 
///    *Application logic must be robust against out-of-order updates.*
///
/// 2. **TCP (Ordered Stream)**:
///    The TCP protocol stack enforces strict ordering. If Packet #2 is lost 
///    (tcp segment loss), the OS holds Packet #3 in the kernel buffer until 
///    #2 is retransmitted by the TCP stack itself. The Receiver application 
///    will always see: #1 -> #2 -> #3.
///    *Note: If the TCP connection is fully severed and re-connected, the 
///    DelegateMQ retry logic may kick in, potentially leading to duplicates 
///    or ordering issues across the connection boundary.*

#include "../../port/transport/ITransport.h"
#include "RetryMonitor.h"

namespace dmq::util {

/// @brief Adapter to enable automatic retries on any ITransport.
/// @details Routes Send() calls through the RetryMonitor before passing them
/// to the physical transport.
class ReliableTransport : public dmq::transport::ITransport
{
    XALLOCATOR
public:
    ReliableTransport() = default;

    ReliableTransport(dmq::transport::ITransport& transport, RetryMonitor& retry)
    {
        Init(transport, retry);
    }

    void Init(dmq::transport::ITransport& transport, RetryMonitor& retry)
    {
        m_transport = &transport;
        m_retry     = &retry;
    }

    virtual int Send(dmq::xostringstream& os, const dmq::transport::DmqHeader& header) override {
        ASSERT_TRUE(m_retry != nullptr);
        return m_retry->SendWithRetry(os, header);
    }

    virtual int Receive(dmq::xstringstream& is, dmq::transport::DmqHeader& header) override {
        ASSERT_TRUE(m_transport != nullptr);
        return m_transport->Receive(is, header);
    }

private:
    dmq::transport::ITransport* m_transport = nullptr;
    RetryMonitor*               m_retry     = nullptr;
};

} // namespace dmq::util


#endif