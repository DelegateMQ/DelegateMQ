/// @file NetworkEngine.h
/// @brief Base class for handling network transport, threading, and synchronization.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#ifndef NETWORK_ENGINE_H
#define NETWORK_ENGINE_H

// Only define NetworkEngine if a compatible transport is selected
#if defined(DMQ_TRANSPORT_ZEROMQ) || defined(DMQ_TRANSPORT_WIN32_UDP) || defined(DMQ_TRANSPORT_LINUX_UDP) || defined(DMQ_TRANSPORT_STM32_UART) || defined(DMQ_TRANSPORT_SERIAL_PORT)

#include "delegate/DelegateAsync.h"
#include "delegate/DelegateAsyncWait.h"
#include "extras/util/RemoteEndpoint.h"
#include "extras/util/TransportMonitor.h"
#include "extras/dispatcher/RemoteChannel.h"
#include <atomic>
#include <functional>
#include <utility>

#if defined(DMQ_THREAD_STDLIB)
    #include "port/os/stdlib/Thread.h"
#elif defined(DMQ_THREAD_WIN32)
    #include "port/os/win32/Thread.h"
#elif defined(DMQ_THREAD_FREERTOS)
    #include "port/os/freertos/Thread.h"
#elif defined(DMQ_THREAD_THREADX)
    #include "port/os/threadx/Thread.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/Thread.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/Thread.h"
#endif

// SWITCH: Include the correct transport header based on CMake definitions
#if defined(DMQ_TRANSPORT_ZEROMQ)
#include "port/transport/zeromq/ZeroMqTransport.h"
#elif defined(DMQ_TRANSPORT_WIN32_UDP)
#include "port/transport/win32-udp/Win32UdpTransport.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
#include "port/transport/linux-udp/LinuxUdpTransport.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#elif defined(DMQ_TRANSPORT_STM32_UART)
#include "port/transport/stm32-uart/Stm32UartTransport.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
#include "port/transport/serial/SerialTransport.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#else
#error "Select a NetworkEngine transport"
#endif

namespace dmq::util {

/// @brief Base class for handling network transport, threading, and synchronization.
/// 
/// @details NetworkEngine encapsulates the "plumbing" of the distributed system, 
/// separating transport mechanics from application business logic. It provides a 
/// unified interface regardless of the underlying transport protocol selected at build time.
/// 
/// **Key Responsibilities:**
/// * **Lifecycle Management:** Controls the startup and shutdown of transport sockets and receiver threads.
/// * **Thread Synchronization:** Marshals all outgoing network calls to a dedicated network thread to ensure 
///   thread safety and prevent blocking the caller's UI or logic threads.
/// * **Message Routing:** Maps incoming data (by ID) to specific `DelegateMemberRemote` instances via `RegisterEndpoint`.
/// * **Reliability:** Integrates with `TransportMonitor` to handle Acknowledgments (ACKs) and retransmissions/timeouts.
class NetworkEngine
{
public:
    NetworkEngine();
    virtual ~NetworkEngine();

    NetworkEngine(const NetworkEngine&) = delete;
    NetworkEngine& operator=(const NetworkEngine&) = delete;

    // SWITCH: Initialize signature differs between transports
#if defined(DMQ_TRANSPORT_ZEROMQ)
    // ZeroMQ uses connection strings (e.g., "tcp://*:5555")
    int Initialize(const std::string& sendAddr, const std::string& recvAddr, bool isServer);
#elif defined(DMQ_TRANSPORT_WIN32_UDP) || defined(DMQ_TRANSPORT_LINUX_UDP)
    // UDP requires explicit IP and Port for sending and receiving
    int Initialize(const std::string& sendIp, int sendPort, const std::string& recvIp, int recvPort);
#elif defined(DMQ_TRANSPORT_STM32_UART)
    // Initialize with HAL Handle
    int Initialize(UART_HandleTypeDef* huart);
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
    // Initialize with COM Port name and baud rate
    int Initialize(const std::string& portName, int baudRate);
#endif

    /// @brief Starts the network engine and its receiving thread.
    /// 
    /// @details This method initializes the internal `RecvThread` to begin polling the 
    /// transport layer for incoming messages. It also starts the `Timeout` timer used 
    /// by the `TransportMonitor` to track unacknowledged messages. 
    /// 
    /// @note This method is thread-safe and will automatically marshal the call to the 
    /// internal Network Thread if called from a different context.
    void Start();

    /// @brief Stops the network engine and releases resources.
    /// 
    /// @details This method gracefully shuts down the `RecvThread`, stops the timeout 
    /// timer, and closes the underlying transport sockets (send/receive). It ensures 
    /// that all internal threads are joined before returning to prevent resource leaks.
    /// 
    /// @note This call blocks until the shutdown sequence is complete.
    void Stop();

    /// @brief Registers a remote endpoint with the network engine.
    /// 
    /// @details This function maps a unique `DelegateRemoteId` to a specific `DelegateMemberRemote`
    /// instance (via the `IRemoteInvoker` interface). When the `NetworkEngine` receives
    /// data from the transport layer, it uses the message ID to look up the registered
    /// endpoint in this map and invokes it to deserialize and handle the payload.
    /// 
    /// @param[in] id The unique identifier for the remote message type.
    /// @param[in] endpoint Pointer to the endpoint instance responsible for handling this ID.
    void RegisterEndpoint(dmq::DelegateRemoteId id, dmq::IRemoteInvoker* endpoint);

    /// @brief Generic helper function to synchronously invoke a remote delegate.
    /// 
    /// @details This function blocks the calling thread until one of two conditions is met:
    /// 1. The remote endpoint acknowledges receipt of the message (ACK).
    /// 2. The operation times out (as defined by `RECV_TIMEOUT`).
    ///
    /// **Thread Synchronization Logic:**
    /// * **If called from the Network Thread:** The send operation executes immediately 
    ///     and returns the result of the transport send call. No blocking wait occurs 
    ///     because we are already on the thread responsible for I/O.
    /// * **If called from any other thread:** The call is marshaled to the Network Thread.
    ///     The calling thread blocks on a condition variable. When the Network Thread 
    ///     receives an ACK (or timeout), it signals the condition variable to wake up 
    ///     the caller.
    ///
    /// @tparam TClass The class type of the remote endpoint (usually inferred).
    /// @tparam RetType The return type of the function signature (usually void).
    /// @tparam Args The argument types of the function signature.
    /// @param[in] endpoint The specific `RemoteEndpoint` instance to invoke.
    /// @param[in] args The arguments to forward to the remote function.
    /// @return `true` if the remote acknowledged the message; `false` on timeout or transport failure.
    template <class TClass, class RetType, class... Args>
    bool RemoteInvokeWait(dmq::DelegateMemberRemote<TClass, RetType(Args...)>& endpoint, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::DelegateMemberRemote<TClass, RetType(Args...)>, Args...>(
            endpoint, std::forward<Args>(args)...);
    }

    /// @brief Overload of RemoteInvokeWait that accepts a RemoteChannel directly.
    /// @details Equivalent to the DelegateMemberRemote overload; routes through the
    /// channel's internal delegate. Prefer this when the endpoint is managed by a
    /// RemoteChannel (i.e. configured via `channel.Bind()`).
    template <class RetType, class... Args>
    bool RemoteInvokeWait(dmq::RemoteChannel<RetType(Args...)>& channel, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::RemoteChannel<RetType(Args...)>, Args...>(
            channel, std::forward<Args>(args)...);
    }

protected:
    /// @brief Returns the send-side transport for use by RemoteChannel instances.
    /// @details Derived classes can pass this to RemoteChannel constructors so each
    /// channel owns its own Dispatcher while sharing the same physical transport.
    dmq::transport::ITransport& GetSendTransport() {
#if defined(DMQ_TRANSPORT_ZEROMQ)
        return m_sendTransport;
#else
        return m_reliableTransport;
#endif
    }

    dmq::os::Thread m_thread;
    Dispatcher m_dispatcher;
    TransportMonitor m_transportMonitor;

    virtual void OnError(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux);
    virtual void OnStatus(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status);

private:
    /// @brief Shared synchronization state for RemoteInvokeWaitInternal().
    /// @details All fields are guarded by `mtx`. The dispatched sequence number
    /// is not known until the send executes on the network thread, but the ACK
    /// (or timeout) status can arrive on the receive/timer thread first — e.g.
    /// on a loopback transport. Statuses that arrive before the seq is recorded
    /// are buffered in `early` and reconciled by the send lambda, so a fast ACK
    /// is never lost.
    struct InvokeWaitState {
        bool complete = false;      ///< Terminal status received for expectedSeq
        bool success = false;       ///< Terminal status was SUCCESS
        bool seqSet = false;        ///< expectedSeq is valid
        uint16_t expectedSeq = 0;   ///< Seq assigned to the dispatched message
        /// Statuses received before seqSet land here.
        dmq::xmap<uint16_t, TransportMonitor::Status> early;
        dmq::Mutex mtx;              // Generic Mutex
        dmq::ConditionVariable cv;   // Generic CV
        XALLOCATOR
    };

    /// @brief Shared implementation for both RemoteInvokeWait() overloads.
    /// @tparam Target A sender exposing operator()(Args...), GetRemoteId(),
    /// GetError(), and GetLastSeqNum() — i.e. DelegateMemberRemote or RemoteChannel.
    template <class Target, class... Args>
    bool RemoteInvokeWaitInternal(Target& target, Args&&... args)
    {
        // [Network Thread] Already on the correct thread: send immediately,
        // no blocking wait is possible (we would deadlock waiting on ourselves).
        if (m_thread.IsCurrentThread())
        {
            target(std::forward<Args>(args)...);
            return (target.GetError() == dmq::DelegateError::SUCCESS);
        }

        // 1. [Caller Thread] Create shared synchronization state.
        auto state = dmq::xmake_shared<InvokeWaitState>();
        dmq::DelegateRemoteId remoteId = target.GetRemoteId();

        // 2. [Caller Thread] Define the status callback that wakes us up later.
        // Fires on the receive thread (ACK) or timer thread (timeout).
        std::function<void(dmq::DelegateRemoteId, uint16_t, TransportMonitor::Status)> statusCbFunc =
            [state, remoteId](dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
                if (id != remoteId)
                    return;
                bool notify = false;
                {
                    dmq::LockGuard<dmq::Mutex> lock(state->mtx);
                    if (!state->seqSet) {
                        // Send thread has not recorded the seq yet; buffer the
                        // status so the send lambda can reconcile it.
                        state->early[seq] = status;
                    }
                    else if (!state->complete && seq == state->expectedSeq) {
                        state->complete = true;
                        state->success = (status == TransportMonitor::Status::SUCCESS);
                        notify = true;
                    }
                }
                if (notify)
                    state->cv.notify_one();
            };

        // 3. [Caller Thread] Register the callback.
        dmq::ScopedConnection conn = m_transportMonitor.OnSendStatus.Connect(dmq::MakeDelegate(statusCbFunc));

        // 4. [Caller Thread] Define the "Send" logic lambda.
        auto* targetPtr = &target;
        std::function<bool(Args...)> asyncCallFunc = [targetPtr, state](auto&&... fwdArgs) -> bool {
            (*targetPtr)(std::forward<decltype(fwdArgs)>(fwdArgs)...);
            bool notify = false;
            {
                dmq::LockGuard<dmq::Mutex> lock(state->mtx);
                state->expectedSeq = targetPtr->GetLastSeqNum();
                state->seqSet = true;

                // Reconcile any status that arrived before the seq was known
                auto it = state->early.find(state->expectedSeq);
                if (it != state->early.end()) {
                    state->complete = true;
                    state->success = (it->second == TransportMonitor::Status::SUCCESS);
                    notify = true;
                }
                
                state->early.clear();
            }
            if (notify)
                state->cv.notify_one();
            return (targetPtr->GetError() == dmq::DelegateError::SUCCESS);
        };

        // 5. [Caller Thread] Dispatch the lambda to the Network Thread queue.
        // Must block until asyncCallFunc actually runs (or throws) on the network
        // thread rather than timing out on the *queueing* wait: asyncCallFunc
        // captures a raw pointer to the caller's `target`. If this wait timed out
        // while the message was still queued (a short SEND_TIMEOUT was previously
        // used here), a caller seeing "failed" could free `target` while the
        // still-queued closure later runs against a dangling pointer. Every other
        // marshal-to-network-thread call site in this file (Initialize, Start,
        // RegisterEndpoint, Stop) uses WAIT_INFINITE for the same reason.
        auto retVal = dmq::MakeDelegate(std::move(asyncCallFunc), m_thread, dmq::WAIT_INFINITE)
            .AsyncInvoke(std::forward<Args>(args)...);

        if (retVal.has_value() && retVal.value() == true)
        {
            // 6. [Caller Thread] BLOCK until the status callback (or the send
            // lambda's reconciliation) completes the wait, or timeout.
            dmq::UniqueLock<dmq::Mutex> lock(state->mtx);
            state->cv.wait_for(lock, RECV_TIMEOUT, [&] { return state->complete; });
            return state->success;
        }

        // 7. [Caller Thread] Send failed or timed out queueing to the network
        // thread. 'conn' disconnects on scope exit; 'state' stays alive via
        // shared_ptr if a late status callback races with our return.
        dmq::LockGuard<dmq::Mutex> lock(state->mtx);
        return state->success;
    }

    void RecvThread();
    void Incoming(dmq::transport::DmqHeader& header, std::shared_ptr<dmq::xstringstream> arg_data);
    void Timeout();
    void InternalErrorHandler(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux);
    void InternalStatusHandler(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status);

    dmq::os::Thread m_recvThread;
    std::atomic<bool> m_recvThreadExit{ false };
    bool m_recvThreadCreated = false;
    Timer m_timeoutTimer;
    dmq::ScopedConnection m_timeoutTimerConn;

    // SWITCH: Transport Members
#if defined(DMQ_TRANSPORT_ZEROMQ)
    dmq::transport::ZeroMqTransport m_sendTransport;
    dmq::transport::ZeroMqTransport m_recvTransport;

    // ZeroMQ already has reliable communication

#elif defined(DMQ_TRANSPORT_WIN32_UDP)
    dmq::transport::Win32UdpTransport m_sendTransport;
    dmq::transport::Win32UdpTransport m_recvTransport;

    // Reliability Layers
    RetryMonitor m_retryMonitor;
    ReliableTransport m_reliableTransport;

#elif defined(DMQ_TRANSPORT_LINUX_UDP)
    dmq::transport::LinuxUdpTransport m_sendTransport;
    dmq::transport::LinuxUdpTransport m_recvTransport;

    // Reliability Layers
    RetryMonitor m_retryMonitor;
    ReliableTransport m_reliableTransport;

#elif defined(DMQ_TRANSPORT_STM32_UART)
    // Single Shared Transport Instance (Owns the buffers/state)
    dmq::transport::Stm32UartTransport m_transport;

    // References (Aliases used by generic code)
    dmq::transport::Stm32UartTransport& m_sendTransport;
    dmq::transport::Stm32UartTransport& m_recvTransport;

    // Reliability Layers
    RetryMonitor m_retryMonitor;
    ReliableTransport m_reliableTransport;

#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
    // Single Shared Transport Instance
    dmq::transport::SerialTransport m_transport;

    // References required by generic code
    dmq::transport::SerialTransport& m_sendTransport;
    dmq::transport::SerialTransport& m_recvTransport;

    // Reliability Layers
    RetryMonitor m_retryMonitor;
    ReliableTransport m_reliableTransport;
    #endif

    dmq::xmap<dmq::DelegateRemoteId, dmq::IRemoteInvoker*> m_receiveIdMap;
    dmq::ScopedConnection m_statusConn;

    static const std::chrono::milliseconds RECV_TIMEOUT;
};

} // namespace dmq::util


#endif // Defined Transport Check
#endif // NETWORK_ENGINE_H
