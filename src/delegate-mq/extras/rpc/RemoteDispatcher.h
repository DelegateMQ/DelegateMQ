/// @file RemoteDispatcher.h
/// @brief Base class for point-to-point RPC over any dmq::transport::ITransport,
/// handling threading and synchronization generically.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#ifndef REMOTE_DISPATCHER_H
#define REMOTE_DISPATCHER_H

#include "delegate/DelegateAsync.h"
#include "delegate/DelegateAsyncWait.h"
#include "delegate/Semaphore.h"
#include "delegate/Signal.h"
#include "delegate/UnicastDelegate.h"
#include "extras/rpc/RemoteEndpoint.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#include "extras/dispatcher/RemoteChannel.h"
#include "port/transport/common/ITransport.h"
#include <atomic>
#include <functional>
#include <utility>

#if defined(DMQ_THREAD_STDLIB)
    #include "port/os/stdlib/StdlibThread.h"
#elif defined(DMQ_THREAD_WIN32)
    #include "port/os/win32/Win32Thread.h"
#elif defined(DMQ_THREAD_POSIX)
    #include "port/os/posix/PosixThread.h"
#elif defined(DMQ_THREAD_FREERTOS)
    #include "port/os/freertos/FreeRTOSThread.h"
#elif defined(DMQ_THREAD_THREADX)
    #include "port/os/threadx/ThreadXThread.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/ZephyrThread.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/CmsisRtos2Thread.h"
#endif

namespace dmq::rpc {

// TransportMonitor/RetryMonitor/Timer live in dmq::util (shared reliability
// plumbing with extras/databus, not duplicated here) -- pulled in by name
// rather than `using namespace dmq::util;` so this header doesn't leak all of
// dmq::util into anyone who later reopens namespace dmq::rpc themselves.
using dmq::util::TransportMonitor;
using dmq::util::RetryMonitor;
using dmq::util::Timer;

/// @brief Base class for handling point-to-point RPC transport, threading, and
/// synchronization.
///
/// @details RemoteDispatcher encapsulates the "plumbing" of point-to-point remote
/// calls -- transport dispatch, the receive thread, ACK/timeout bookkeeping --
/// entirely behind `dmq::transport::ITransport`. It never constructs or knows
/// the concrete transport type: an owning class constructs its own transport
/// (Win32UdpTransport, ZeroMqTransport, ...), optionally wraps it in
/// ReliableTransport for ACK/retry reliability, and hands the result to
/// Attach(). This works with any transport that implements ITransport, not a
/// fixed list, and keeps this class itself free of per-transport branching.
///
/// **Key Responsibilities:**
/// * **Lifecycle Management:** Controls the startup and shutdown of the
///   receiver thread.
/// * **Thread Synchronization:** Marshals all outgoing network calls to a
///   dedicated network thread to ensure thread safety and prevent blocking
///   the caller's UI or logic threads.
/// * **Message Routing:** Maps incoming data (by ID) to specific
///   `DelegateMemberRemote` instances via `RegisterEndpoint`.
/// * **Reliability:** Integrates with `TransportMonitor` to handle
///   Acknowledgments (ACKs) and retransmissions/timeouts.
///
/// @note Designed to be held as a member (composition), not inherited from --
/// matching `extras/databus`'s Participant/NetworkNode shape. Status is
/// reported via public Signal members (OnError/OnStatus/OnDeliveryFailed
/// below); transport lifecycle (constructing/closing the concrete transport)
/// is wired through Attach()/SetCloseHandler(). An owning class never needs
/// to subclass RemoteDispatcher just to plug into it.
class RemoteDispatcher
{
public:
    /// @param ackTimeout How long a sent message waits for its ACK before
    /// TIMEOUT (the internal TransportMonitor's timeout). Defaults to
    /// dmq::TRANSPORT_MONITOR_TIMEOUT (DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC).
    explicit RemoteDispatcher(dmq::Duration ackTimeout = dmq::TRANSPORT_MONITOR_TIMEOUT);
    ~RemoteDispatcher();

    RemoteDispatcher(const RemoteDispatcher&) = delete;
    RemoteDispatcher& operator=(const RemoteDispatcher&) = delete;

    /// @brief Fires when a channel/endpoint reports a technical error
    /// (serialization failure, type mismatch, etc.) via SetErrorHandler().
    dmq::Signal<void(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux)> OnError;

    /// @brief Fires on every send-status update from the internal
    /// TransportMonitor (e.g. TIMEOUT while a RELIABLE message is still
    /// being retried, or SUCCESS on ACK). Not the same as OnDeliveryFailed:
    /// a message can report TIMEOUT several times while still retrying
    /// before either succeeding or finally exhausting its retry budget.
    dmq::Signal<void(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status)> OnStatus;

    /// @brief Fires when a RELIABLE message exhausts its retry budget
    /// without being ACKed (RetryMonitor::OnDeliveryFailed). Only fires if
    /// the derived class called AttachRetryMonitor().
    dmq::Signal<void(dmq::DelegateRemoteId id, uint16_t seqNum)> OnDeliveryFailed;

    /// @brief Attach the transport(s) this engine sends/receives through.
    /// @details Call exactly once, after the owning class's own transport
    /// member(s) are constructed and before Start()/any send -- e.g. from the
    /// owning class's constructor body or a separate Create()-style init
    /// method. The owning class retains ownership; sendTransport/recvTransport
    /// must outlive this object. Pass a decorator (e.g. a ReliableTransport
    /// wrapping a raw one) as sendTransport to add ACK/retry reliability on
    /// top of an unreliable transport -- RemoteDispatcher only ever sees the
    /// ITransport interface, so it neither knows nor cares.
    /// @param sendTransport  Transport used for outgoing sends; also the
    ///                       transport GetSendTransport() returns for
    ///                       constructing RemoteChannel instances against.
    /// @param recvTransport  Transport used for the blocking receive loop.
    void Attach(dmq::transport::ITransport& sendTransport, dmq::transport::ITransport& recvTransport);

    /// @brief Optionally wire a RetryMonitor's delivery-failure signal to
    /// this engine's own OnDeliveryFailed signal.
    /// @details Call once, after Attach(), only if the owning class layered
    /// RetryMonitor/ReliableTransport on top of its transport for ACK/retry
    /// reliability. Skip this call for a self-reliable transport (e.g.
    /// ZeroMQ) that has no RetryMonitor.
    /// Once attached, RemoteInvokeWait() treats a TIMEOUT as a retry in
    /// progress and waits for the final ACK or delivery failure, so the
    /// RetryMonitor must be built on GetTransportMonitor().
    /// @param retryMonitor  The independently-owned RetryMonitor whose
    ///                      OnDeliveryFailed signal should forward here.
    void AttachRetryMonitor(RetryMonitor& retryMonitor);

    /// @brief Set the handler Stop() calls to close the underlying
    /// transport(s).
    /// @details Call once, any time before Stop() (typically right after
    /// Attach()). Necessary because some transports (sockets, serial ports)
    /// are blocking and won't return from Receive() until a message arrives
    /// or the resource is closed -- ITransport itself has no Close() (its
    /// concrete transports each expose their own), so Stop() calls this
    /// handler at the exact point closing needs to happen: after the exit
    /// flag is set, before the receive thread is joined.
    /// @param handler  Called by Stop() to close the transport(s). Optional
    ///                 -- if never set, Stop() simply skips this step, same
    ///                 as the old CloseTransports() hook's no-op default.
    void SetCloseHandler(dmq::UnicastDelegate<void()> handler) { m_closeHandler = std::move(handler); }

    /// @brief Returns the network thread this engine dispatches on.
    /// @details Use `GetThread().IsCurrentThread()` for thread-affinity checks,
    /// or pass `GetThread()` as the destination thread to `dmq::MakeDelegate(...)`
    /// when marshaling a call from the owning class onto the network thread.
    dmq::os::Thread& GetThread() { return m_thread; }

    /// @brief Returns the internal TransportMonitor, for wiring into whichever
    /// concrete transport(s) the owning class constructs, e.g.
    /// `transport.SetTransportMonitor(&GetTransportMonitor())`.
    TransportMonitor& GetTransportMonitor() { return m_transportMonitor; }

    /// @brief Returns the transport passed as `sendTransport` to Attach(), for
    /// use by RemoteChannel instances.
    /// @details Pass this to RemoteChannel constructors so each channel owns
    /// its own Dispatcher while sharing the same physical transport.
    /// Hard-faults if called before Attach().
    dmq::transport::ITransport& GetSendTransport();

    /// @brief Starts the network engine and its receiving thread.
    ///
    /// @details This method initializes the internal `RecvThread` to begin polling the
    /// transport layer for incoming messages. It also starts the `Timeout` timer used
    /// by the `TransportMonitor` to track unacknowledged messages.
    ///
    /// @note This method is thread-safe and will automatically marshal the call to the
    /// internal Network Thread if called from a different context. Requires Attach()
    /// to have been called first.
    void Start();

    /// @brief Stops the network engine and releases resources.
    ///
    /// @details This method gracefully shuts down the `RecvThread`, stops the timeout
    /// timer, and calls the handler set via SetCloseHandler() (if any) so the
    /// owning class can close its underlying transport socket(s). It ensures
    /// that all internal threads are joined before returning to prevent
    /// resource leaks.
    ///
    /// @note This call blocks until the shutdown sequence is complete.
    void Stop();

    /// @brief Registers a remote endpoint with the network engine.
    ///
    /// @details This function maps a unique `DelegateRemoteId` to a specific
    /// `DelegateMemberRemote` instance (via the `IRemoteInvoker` interface).
    /// When the `RemoteDispatcher` receives data from the transport layer, it
    /// uses the message ID to look up the registered endpoint in this map and
    /// invokes it to deserialize and handle the payload.
    ///
    /// @param id        The unique identifier for the remote message type.
    /// @param endpoint  Pointer to the endpoint instance responsible for
    ///                  handling this ID.
    void RegisterEndpoint(dmq::DelegateRemoteId id, dmq::IRemoteInvoker* endpoint);

    /// @brief Registers a RemoteChannel endpoint and automatically wires its
    /// error handler to fire this RemoteDispatcher's OnError signal.
    /// @details Equivalent to calling `channel.SetErrorHandler(...)` followed
    /// by `RegisterEndpoint(id, channel.GetEndpoint())`. Prefer this overload
    /// so a new channel's send and receive errors are never left unreported.
    /// Call `channel.SetErrorHandler(...)` again afterward to override with a
    /// custom per-channel handler.
    /// @param id       The unique identifier for the remote message type.
    /// @param channel  The RemoteChannel instance responsible for handling
    ///                 this ID.
    template <class Sig>
    void RegisterEndpoint(dmq::DelegateRemoteId id, dmq::RemoteChannel<Sig>& channel) {
        channel.SetErrorHandler(dmq::MakeDelegate(this, &RemoteDispatcher::InternalErrorHandler));
        RegisterEndpoint(id, channel.GetEndpoint());
    }

    /// @brief Generic helper function to synchronously invoke a remote
    /// delegate.
    ///
    /// @details Blocks the calling thread until the message's final outcome:
    /// 1. The remote endpoint acknowledges it (ACK): returns `true`.
    /// 2. It is abandoned: returns `false`. With a RetryMonitor attached
    ///    (AttachRetryMonitor()) that means its retries ran out
    ///    (`OnDeliveryFailed`); intermediate TIMEOUTs are retries in progress
    ///    and don't end the wait. Without one, the first TIMEOUT is final.
    ///
    /// The wait is bounded by GetInvokeWaitLimit(), which covers the whole
    /// retry window. Use RemoteInvokeWaitFor() for a shorter limit.
    ///
    /// **Thread Synchronization Logic:**
    /// * **If called from the Network Thread:** The send operation executes
    ///   immediately and returns the result of the transport send call. No
    ///   blocking wait occurs because we are already on the thread
    ///   responsible for I/O.
    /// * **If called from any other thread:** The call is marshaled to the
    ///   Network Thread. The calling thread blocks on a condition variable.
    ///   When the Network Thread receives an ACK (or timeout), it signals the
    ///   condition variable to wake up the caller.
    ///
    /// @tparam TClass   The class type of the remote endpoint (usually
    ///                  inferred).
    /// @tparam RetType  The return type of the function signature (usually
    ///                  void).
    /// @tparam Args     The argument types of the function signature.
    /// @param endpoint  The specific `RemoteEndpoint` instance to invoke.
    /// @param args      The arguments to forward to the remote function.
    /// @return `true` if the remote acknowledged the message; `false` on
    ///         timeout or transport failure.
    template <class TClass, class RetType, class... Args>
    bool RemoteInvokeWait(dmq::DelegateMemberRemote<TClass, RetType(Args...)>& endpoint, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::DelegateMemberRemote<TClass, RetType(Args...)>, Args...>(
            endpoint, GetInvokeWaitLimit(), std::forward<Args>(args)...);
    }

    /// @brief RemoteInvokeWait() with a caller-supplied limit on the wait.
    /// @details If `timeout` expires before the final outcome, returns `false`
    /// though the message may still be delivered: retries continue in the
    /// background. Use it when the caller can't block for the whole retry window.
    /// @param endpoint  The specific `RemoteEndpoint` instance to invoke.
    /// @param timeout   The most the caller waits.
    /// @param args      The arguments to forward to the remote function.
    /// @return `true` if the remote acknowledged the message within `timeout`.
    template <class TClass, class RetType, class... Args>
    bool RemoteInvokeWaitFor(dmq::DelegateMemberRemote<TClass, RetType(Args...)>& endpoint, dmq::Duration timeout, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::DelegateMemberRemote<TClass, RetType(Args...)>, Args...>(
            endpoint, timeout, std::forward<Args>(args)...);
    }

    /// @brief Overload of RemoteInvokeWait that accepts a RemoteChannel
    /// directly.
    /// @details Equivalent to the DelegateMemberRemote overload; routes
    /// through the channel's internal delegate. Prefer this when the
    /// endpoint is managed by a RemoteChannel (i.e. configured via
    /// `channel.Bind()`).
    /// @tparam RetType  The return type of the function signature (usually
    ///                  void).
    /// @tparam Args     The argument types of the function signature.
    /// @param channel   The RemoteChannel instance to invoke.
    /// @param args      The arguments to forward to the remote function.
    /// @return `true` if the remote acknowledged the message; `false` on
    ///         timeout or transport failure.
    template <class RetType, class... Args>
    bool RemoteInvokeWait(dmq::RemoteChannel<RetType(Args...)>& channel, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::RemoteChannel<RetType(Args...)>, Args...>(
            channel, GetInvokeWaitLimit(), std::forward<Args>(args)...);
    }

    /// @brief RemoteInvokeWait() on a RemoteChannel with a caller-supplied limit.
    /// @details See the DelegateMemberRemote overload of RemoteInvokeWaitFor().
    template <class RetType, class... Args>
    bool RemoteInvokeWaitFor(dmq::RemoteChannel<RetType(Args...)>& channel, dmq::Duration timeout, Args&&... args)
    {
        return RemoteInvokeWaitInternal<dmq::RemoteChannel<RetType(Args...)>, Args...>(
            channel, timeout, std::forward<Args>(args)...);
    }

    /// @brief Default limit for RemoteInvokeWait():
    /// (retries + 1) x (ACK timeout + TIMEOUT_POLL_INTERVAL) + ACK timeout / 2.
    /// Each attempt's TIMEOUT can be reported up to one poll interval late; the
    /// half ACK timeout is scheduling slack. Retries are 0 without a
    /// RetryMonitor attached. With the defaults (2 s, 3 retries): 9.4 s.
    dmq::Duration GetInvokeWaitLimit() const;

    /// How often the network thread checks for ACK timeouts.
    static constexpr std::chrono::milliseconds TIMEOUT_POLL_INTERVAL{ 100 };

private:
    dmq::os::Thread m_thread;
    Dispatcher m_dispatcher;
    TransportMonitor m_transportMonitor;
    dmq::UnicastDelegate<void()> m_closeHandler;

    /// @brief Shared synchronization state for RemoteInvokeWaitInternal().
    /// @details All fields are guarded by `mtx`. The dispatched sequence number
    /// is not known until the send executes on the network thread, but the final
    /// outcome (ACK, final TIMEOUT, or delivery failure) can arrive first — e.g.
    /// on a loopback transport, or an immediate send failure inside the send.
    /// Outcomes that arrive before the seq is recorded are buffered in `early`
    /// and reconciled by the send lambda, so a fast one is never lost.
    struct InvokeWaitState {
        bool complete = false;      ///< Final outcome received for expectedSeq
        bool success = false;       ///< Final outcome was an ACK
        bool seqSet = false;        ///< expectedSeq is valid
        uint16_t expectedSeq = 0;   ///< Seq assigned to the dispatched message
        /// Final outcomes (true = ACKed) received before seqSet, by seq.
        dmq::xmap<uint16_t, bool> early;

        /// @brief Record a final outcome for `seq`.
        /// @return true if it completed the wait, so the caller must Signal().
        bool Report(uint16_t seq, bool acked) {
            dmq::LockGuard<dmq::Mutex> lock(mtx);
            if (!seqSet) {
                early.emplace(seq, acked);   // keep the first outcome per seq
                return false;
            }
            if (complete || seq != expectedSeq)
                return false;
            complete = true;
            success = acked;
            return true;
        }
        dmq::Mutex mtx;              // Generic Mutex
        dmq::Semaphore sem;          // Portable wake signal (DMQ_HAS_SEMAPHORE is
                                     // available everywhere this file is included --
                                     // see DelegateMQ.h's #if defined(DMQ_HAS_SEMAPHORE)
                                     // guard -- unlike dmq::ConditionVariable, which
                                     // has no port on Zephyr/CMSIS-RTOS2/NuttX)
        XALLOCATOR
    };

    /// @brief Shared implementation for both RemoteInvokeWait() overloads.
    /// @tparam Target  A sender exposing operator()(Args...), GetRemoteId(),
    ///                 GetError(), and GetLastSeqNum() -- i.e.
    ///                 DelegateMemberRemote or RemoteChannel.
    template <class Target, class... Args>
    bool RemoteInvokeWaitInternal(Target& target, dmq::Duration timeout, Args&&... args)
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

        // 2. [Caller Thread] Define the callbacks that report the final outcome.
        // With a RetryMonitor attached, a TIMEOUT only means a retry follows; the
        // message is final on SUCCESS or on the RetryMonitor's delivery failure.
        // Without one, the first TIMEOUT is final.
        const bool retrying = (m_retryMonitor != nullptr);
        std::function<void(dmq::DelegateRemoteId, uint16_t, TransportMonitor::Status)> statusCbFunc =
            [state, remoteId, retrying](dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
                if (id != remoteId)
                    return;
                const bool acked = (status == TransportMonitor::Status::SUCCESS);
                if (!acked && retrying)
                    return;
                if (state->Report(seq, acked))
                    state->sem.Signal();
            };
        std::function<void(dmq::DelegateRemoteId, uint16_t)> failedCbFunc =
            [state, remoteId](dmq::DelegateRemoteId id, uint16_t seq) {
                if (id == remoteId && state->Report(seq, false))
                    state->sem.Signal();
            };

        // 3. [Caller Thread] Register the callbacks.
        dmq::ScopedConnection conn = m_transportMonitor.OnSendStatus.Connect(dmq::MakeDelegate(statusCbFunc));
        dmq::ScopedConnection failedConn;
        if (retrying)
            failedConn = OnDeliveryFailed.Connect(dmq::MakeDelegate(failedCbFunc));

        // 4. [Caller Thread] Define the "Send" logic lambda.
        auto* targetPtr = &target;
        std::function<bool(Args...)> asyncCallFunc = [targetPtr, state](auto&&... fwdArgs) -> bool {
            (*targetPtr)(std::forward<decltype(fwdArgs)>(fwdArgs)...);
            bool notify = false;
            {
                dmq::LockGuard<dmq::Mutex> lock(state->mtx);
                state->expectedSeq = targetPtr->GetLastSeqNum();
                state->seqSet = true;

                // Reconcile an outcome that arrived before the seq was known
                auto it = state->early.find(state->expectedSeq);
                if (it != state->early.end()) {
                    state->complete = true;
                    state->success = it->second;
                    notify = true;
                }

                state->early.clear();
            }
            if (notify)
                state->sem.Signal();
            return (targetPtr->GetError() == dmq::DelegateError::SUCCESS);
        };

        // 5. [Caller Thread] Dispatch the lambda to the Network Thread queue.
        // Must block until asyncCallFunc actually runs (or throws) on the network
        // thread rather than timing out on the *queueing* wait: asyncCallFunc
        // captures a raw pointer to the caller's `target`. If this wait timed out
        // while the message was still queued (a short SEND_TIMEOUT was previously
        // used here), a caller seeing "failed" could free `target` while the
        // still-queued closure later runs against a dangling pointer. Every other
        // marshal-to-network-thread call site in this file (Start, RegisterEndpoint,
        // Stop) uses WAIT_INFINITE for the same reason.
        auto retVal = dmq::MakeDelegate(std::move(asyncCallFunc), m_thread, dmq::WAIT_INFINITE)
            .AsyncInvoke(std::forward<Args>(args)...);

        if (retVal.has_value() && retVal.value() == true)
        {
            // 6. [Caller Thread] BLOCK until the status callback (or the send
            // lambda's reconciliation) completes the wait, or timeout. Whichever
            // lambda sets state->complete/state->success calls state->sem.Signal()
            // exactly once, outside the lock; Signal()-before-Wait() is safe since
            // dmq::Semaphore retains its signaled state (see delegate/Semaphore.h).
            // If Wait() times out, state->success is still correctly read below --
            // it was initialized false and nothing sets it without also signaling.
            state->sem.Wait(timeout);
            dmq::LockGuard<dmq::Mutex> lock(state->mtx);
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
    void InternalDeliveryFailedHandler(dmq::DelegateRemoteId id, uint16_t seqNum);

    dmq::os::Thread m_recvThread;
    std::atomic<bool> m_recvThreadExit{ false };
    bool m_recvThreadCreated = false;
    Timer m_timeoutTimer;
    dmq::ScopedConnection m_timeoutTimerConn;

    // Set by Attach(). Not owned -- the derived class retains ownership of
    // whatever concrete transport(s) these point to.
    dmq::transport::ITransport* m_txTransport = nullptr;
    dmq::transport::ITransport* m_rxTransport = nullptr;

    dmq::xmap<dmq::DelegateRemoteId, dmq::IRemoteInvoker*> m_receiveIdMap;
    dmq::ScopedConnection m_statusConn;
    dmq::ScopedConnection m_deliveryFailedConn;

    /// Set by AttachRetryMonitor(); not owned. Decides whether a TIMEOUT is
    /// final for RemoteInvokeWait() and sizes GetInvokeWaitLimit().
    RetryMonitor* m_retryMonitor = nullptr;
};

} // namespace dmq::rpc

#endif // REMOTE_DISPATCHER_H
