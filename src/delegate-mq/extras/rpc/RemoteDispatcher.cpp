// Needs a real thread port (DMQ_HAS_THREADING) and compiles to nothing on bare
// metal, so every DelegateMQ source file can be added to a build (see
// src/DelegateMQ.cpp).
#include "delegate/DelegateSelect.h"
#if defined(DMQ_HAS_THREADING)

#include "RemoteDispatcher.h"
#include "extras/util/TimerDelegate.h"

namespace dmq::rpc {

using namespace dmq;
using namespace dmq::transport;
using dmq::util::TransportMonitor;
using dmq::util::RetryMonitor;

RemoteDispatcher::RemoteDispatcher(dmq::Duration ackTimeout)
    : m_thread("RemoteDispatcher", 25, dmq::os::FullPolicy::DROP)
    , m_transportMonitor(ackTimeout)
    , m_recvThread("NetworkRecv")
{
    m_statusConn = m_transportMonitor.OnSendStatus.Connect(dmq::MakeDelegate(this, &RemoteDispatcher::InternalStatusHandler));
    m_thread.CreateThread();
}

RemoteDispatcher::~RemoteDispatcher()
{
    Stop();
    m_thread.ExitThread();
}

void RemoteDispatcher::Attach(dmq::transport::ITransport& sendTransport, dmq::transport::ITransport& recvTransport)
{
    DMQ_ASSERT_TRUE(m_txTransport == nullptr && m_rxTransport == nullptr); // Attach() called more than once
    m_txTransport = &sendTransport;
    m_rxTransport = &recvTransport;
    m_dispatcher.SetTransport(&sendTransport);
}

void RemoteDispatcher::AttachRetryMonitor(RetryMonitor& retryMonitor)
{
    m_retryMonitor = &retryMonitor;
    m_deliveryFailedConn = retryMonitor.OnDeliveryFailed.Connect(dmq::MakeDelegate(this, &RemoteDispatcher::InternalDeliveryFailedHandler));
}

dmq::Duration RemoteDispatcher::GetInvokeWaitLimit() const
{
    const dmq::Duration ack = m_transportMonitor.GetTimeout();
    int retries = m_retryMonitor ? m_retryMonitor->GetMaxRetries() : 0;
    if (retries < 0)
        retries = 0;
    // Each attempt's TIMEOUT is seen up to one poll interval after it expires;
    // half an ACK timeout on top is scheduling slack.
    return (ack + TIMEOUT_POLL_INTERVAL) * (retries + 1) + ack / 2;
}

dmq::transport::ITransport& RemoteDispatcher::GetSendTransport()
{
    DMQ_ASSERT_TRUE(m_txTransport != nullptr); // Attach() must be called first
    return *m_txTransport;
}

void RemoteDispatcher::Start()
{
    // Blocking marshal (matches Stop()'s own self-marshal below): a caller on
    // another thread must see m_recvThread fully created before Start()
    // returns, or a Start()-then-Stop() sequence from that same thread races
    // Stop()'s direct (non-marshaled) m_recvThread.ExitThread() call against
    // this method's own CreateThread() still running asynchronously on
    // m_thread -- caught by ThreadSanitizer as a data race on StdlibThread's
    // internal std::optional<std::thread>.
    if (!m_thread.IsCurrentThread())
        return dmq::MakeDelegate(this, &RemoteDispatcher::Start, m_thread, dmq::WAIT_INFINITE)();

    DMQ_ASSERT_TRUE(m_txTransport != nullptr && m_rxTransport != nullptr); // Attach() must be called first

    if (!m_recvThreadCreated)
    {
        m_recvThreadCreated = true;
        m_recvThread.CreateThread();

        // Post the "RecvThread" loop to run on this new thread.
        dmq::MakeDelegate(this, &RemoteDispatcher::RecvThread, m_recvThread).AsyncInvoke();
    }

    m_timeoutTimerConn = m_timeoutTimer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &RemoteDispatcher::Timeout, m_thread));
    m_timeoutTimer.Start(TIMEOUT_POLL_INTERVAL);
}

void RemoteDispatcher::Stop()
{
    if (!m_thread.IsCurrentThread()) {
        if (m_recvThreadExit) return;

        // 1. Signal exit flag FIRST so RecvThread knows it should terminate
        m_recvThreadExit = true;

        // 2. Close transports to unblock any pending Receive() calls.
        // This is necessary because some transports (like sockets) are blocking
        // and won't return until a message arrives or the resource is closed.
        // ITransport has no Close() of its own -- the owning class supplies a
        // handler via SetCloseHandler() to close whichever concrete
        // transport(s) it owns. Optional: if never set, this is a no-op.
        if (m_closeHandler)
            m_closeHandler();

        // 3. Exit/Join the receive thread.
        m_recvThread.ExitThread();

        // 4. Marshal the rest of the cleanup to the Network Thread.
        return dmq::MakeDelegate(this, &RemoteDispatcher::Stop, m_thread, dmq::WAIT_INFINITE)();
    }
    m_timeoutTimer.Stop();
    m_timeoutTimerConn.Disconnect();
    m_statusConn.Disconnect();
    m_deliveryFailedConn.Disconnect();
}

void RemoteDispatcher::RegisterEndpoint(dmq::DelegateRemoteId id, dmq::IRemoteInvoker* endpoint)
{
    // Thread Safety: Ensure this runs on the Network Thread to avoid
    // racing with 'Incoming()' which reads this map.
    if (!m_thread.IsCurrentThread())
    {
        // Marshal the call to the Network Thread. Cast disambiguates the overload
        // set now that RegisterEndpoint() has a templated RemoteChannel<Sig> sibling.
        using RegisterEndpointFn = void (RemoteDispatcher::*)(dmq::DelegateRemoteId, dmq::IRemoteInvoker*);
        dmq::MakeDelegate(this, static_cast<RegisterEndpointFn>(&RemoteDispatcher::RegisterEndpoint), m_thread, dmq::WAIT_INFINITE)(id, endpoint);
        return;
    }

    // Actual insertion (Safe because we are now on the correct thread)
    m_receiveIdMap[id] = endpoint;
}

//------------------------------------------------------------------------------
// RecvThread
//------------------------------------------------------------------------------
/// @brief The main loop for the background receive thread.
void RemoteDispatcher::RecvThread()
{
    // Timeout for enqueuing the message to the main thread.
    static const std::chrono::milliseconds INVOKE_TIMEOUT(1000);

    std::shared_ptr<dmq::xstringstream> arg_data;

    while (!m_recvThreadExit)
    {
        DmqHeader header;

        // Only allocate a new stream if we dispatched the previous one
        if (!arg_data) {
            arg_data = dmq::xmake_shared<dmq::xstringstream>(std::ios::in | std::ios::out | std::ios::binary);
        } else {
            // Reuse the existing stream: clear error flags and empty its contents
            arg_data->clear();
            arg_data->str(dmq::xstring());
        }

        // Block reading from the physical transport
        int error = m_rxTransport->Receive(*arg_data, header);

        if (!error && !arg_data->str().empty() && !m_recvThreadExit)
        {
            // Dispatch processing to the main RemoteDispatcher thread.
            // Passes ownership of the data stream via shared_ptr (no deep copy).
            dmq::MakeDelegate(this, &RemoteDispatcher::Incoming, m_thread, INVOKE_TIMEOUT).AsyncInvoke(header, arg_data);

            // Release our local reference so a new one is allocated next iteration
            arg_data.reset();
        }
        else if (error < 0 && !m_recvThreadExit)
        {
            // Negative is a genuine transport fault (bad frame, socket error);
            // ITransport::RECEIVE_NO_DATA (a receive timeout) is not reported.
            // The m_recvThreadExit guard excludes the one expected non-fault
            // case: Stop() closing the socket to unblock this call.
            InternalErrorHandler(dmq::INVALID_REMOTE_ID, dmq::DelegateError::ERR_TRANSPORT_RECEIVE, error);
        }
    }
}

//------------------------------------------------------------------------------
// Incoming
//------------------------------------------------------------------------------
/// @brief Handles incoming messages on the main Network Thread.
void RemoteDispatcher::Incoming(DmqHeader& header, std::shared_ptr<dmq::xstringstream> arg_data)
{
    // Filter out ACKs; we only dispatch application data here.
    if (header.GetId() != dmq::ACK_REMOTE_ID) {
        // Find the registered endpoint for this Message ID
        auto it = m_receiveIdMap.find(header.GetId());

        // If found and valid, let the endpoint handle deserialization and execution
        if (it != m_receiveIdMap.end() && it->second) {
            it->second->Invoke(*arg_data);
        }
    }
}

void RemoteDispatcher::Timeout() { m_transportMonitor.Process(); }

void RemoteDispatcher::InternalErrorHandler(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux) {
    OnError(id, error, aux);
}

void RemoteDispatcher::InternalStatusHandler(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
    OnStatus(id, seq, status);
}

void RemoteDispatcher::InternalDeliveryFailedHandler(dmq::DelegateRemoteId id, uint16_t seqNum) {
    OnDeliveryFailed(id, seqNum);
}

} // namespace dmq::rpc

#endif // DMQ_HAS_THREADING
