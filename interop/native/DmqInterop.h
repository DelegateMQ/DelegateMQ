#ifndef DMQ_INTEROP_H
#define DMQ_INTEROP_H

#include <stdint.h>

/// @file DmqInterop.h
/// @brief C-compatible API for DelegateMQ remote communication DLL.
///
/// This DLL wraps the C++ DelegateMQ core (transports, headers, threads)
/// to provide a simple interface for C#, Python, and other languages.
///
/// Reliability: outgoing messages (DmqInterop_Send) use the same stack as a C++
/// NetworkNode's RELIABLE mode -- TransportMonitor + RetryMonitor +
/// ReliableTransport. Each message is tracked until the peer ACKs it and is
/// resent on timeout; the outcome is reported per message through the status
/// callback. Incoming messages (the multicast recv channel) are best-effort and
/// not ACKed.
///
/// Threading: callbacks run on native threads -- the receive thread, the
/// send-monitor thread, or (for an immediate send failure) the thread calling
/// DmqInterop_Send. Keep them short and thread-safe.

#ifdef _WIN32
#define DMQ_EXPORT __declspec(dllexport)
#define DMQ_CALL __stdcall
#else
#define DMQ_EXPORT __attribute__((visibility("default")))
#define DMQ_CALL
#endif

extern "C" {
    /// @brief Outcome of one outgoing message, reported via DmqStatusCallback.
    enum DmqSendStatus {
        DMQ_STATUS_ACKED = 0,            ///< Peer acknowledged the message. Final.
        DMQ_STATUS_TIMEOUT = 1,          ///< An attempt timed out without an ACK. A retry follows
                                         ///< unless DMQ_STATUS_DELIVERY_FAILED is reported next.
        DMQ_STATUS_DELIVERY_FAILED = 2   ///< Abandoned: retries exhausted, or the send failed
                                         ///< immediately. Final.
    };

    /// @brief Error categories reported via DmqErrorCallback.
    enum DmqErrorCode {
        DMQ_ERR_EXCEPTION = 1,           ///< A native exception was caught; msg has details.
        DMQ_ERR_CAP_EXCEEDED = 2,        ///< Too many unacknowledged messages outstanding; new
                                         ///< sends fail until ACKs or timeouts free slots.
        DMQ_ERR_PENDING_EXCEEDED = 3,    ///< Timed-out messages are piling up faster than they
                                         ///< can be processed (link down or peer overloaded).
        DMQ_ERR_FAULT = 4,               ///< Library fault (failed assertion). The process aborts
                                         ///< after this callback returns.
        DMQ_ERR_WATCHDOG = 5,            ///< Thread watchdog expired. The process aborts after this
                                         ///< callback returns.
        DMQ_ERR_CALLBACK = 6,            ///< Reserved for language wrappers: an exception in a user
                                         ///< callback or payload deserialization. Never raised natively.
        DMQ_ERR_TRANSPORT = 7            ///< A transport couldn't be created (bind/join failed);
                                         ///< DmqInterop_Start() returns non-zero.
    };

    /// @brief Callback signature for received messages.
    /// @param remoteId The DelegateRemoteId (topic ID).
    /// @param data Pointer to the raw payload bytes. Valid only during the call.
    /// @param len Length of the payload in bytes.
    typedef void (DMQ_CALL *DmqMessageCallback)(uint16_t remoteId, const uint8_t* data, uint32_t len);

    /// @brief Callback signature for per-message delivery status.
    /// @param remoteId The DelegateRemoteId the message was sent to.
    /// @param seqNum The sequence number returned by DmqInterop_Send.
    /// @param status A DmqSendStatus value.
    typedef void (DMQ_CALL *DmqStatusCallback)(uint16_t remoteId, uint16_t seqNum, int status);

    /// @brief Callback signature for errors.
    /// @param code A DmqErrorCode value.
    /// @param remoteId The related DelegateRemoteId, or 0 if none.
    /// @param msg Human-readable description. Valid only during the call.
    typedef void (DMQ_CALL *DmqErrorCallback)(int code, uint16_t remoteId, const char* msg);

    /// @brief Configure outgoing reliability. Call before DmqInterop_Start().
    /// @param enabled 1 (default) tracks ACKs and retries; 0 sends fire-and-forget with no
    /// status callbacks -- use when the peer doesn't send ACKs.
    /// @param timeoutMs Time to wait for an ACK before an attempt times out. <= 0 keeps the
    /// library default (DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC, 2 s).
    /// @param maxRetries Resends after the first attempt before giving up. < 0 keeps the
    /// library default (DMQ_RETRY_MONITOR_MAX_RETRIES, 3); 0 tracks ACKs without resending.
    /// @return 0 on success, -1 if already started.
    DMQ_EXPORT int DMQ_CALL DmqInterop_SetReliability(int enabled, int timeoutMs, int maxRetries);

    /// @brief Initialize and start the transports and background threads.
    /// @param remoteHost IP address of the remote peer (used for the unicast send/command channel).
    /// @param recvPort Local port to bind/join for incoming messages.
    /// @param sendPort Remote port to send messages to. ACKs come back to the same socket.
    /// @param multicastGroup Multicast group address to join for the incoming (recv) channel, so
    /// multiple clients can receive the same stream concurrently. Ignored by transports (e.g. ZeroMQ)
    /// whose PUB/SUB sockets already fan out to multiple subscribers without OS-level port sharing.
    /// @return 0 on success, non-zero on error.
    DMQ_EXPORT int DMQ_CALL DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort, const char* multicastGroup);

    /// @brief Register a callback for a specific Remote ID. May be called before Start().
    /// @param remoteId The DelegateRemoteId to listen for.
    /// @param cb The function to call when data arrives, or NULL to unregister.
    DMQ_EXPORT void DMQ_CALL DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb);

    /// @brief Register the per-message delivery status callback. May be called before Start().
    /// @param cb The function to call on ACK, timeout or delivery failure, or NULL to unregister.
    DMQ_EXPORT void DMQ_CALL DmqInterop_RegisterStatusCallback(DmqStatusCallback cb);

    /// @brief Register the error callback. May be called before Start().
    /// @param cb The function to call when an error occurs, or NULL to unregister.
    DMQ_EXPORT void DMQ_CALL DmqInterop_RegisterErrorCallback(DmqErrorCallback cb);

    /// @brief Send raw bytes to a Remote ID.
    /// @details The DLL handles the DmqHeader framing and sequence numbers. With reliability
    /// enabled, the message is tracked and its outcome reported via the status callback.
    /// @param remoteId The destination DelegateRemoteId.
    /// @param data Pointer to the payload bytes to send.
    /// @param len Length of the payload in bytes.
    /// @param seqNum Receives the message's sequence number, to match against status
    /// callbacks. May be NULL.
    /// @return 0 on success, non-zero on error (not started, or the send failed immediately;
    /// an immediate failure is also reported as DMQ_STATUS_DELIVERY_FAILED).
    DMQ_EXPORT int DMQ_CALL DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len, uint16_t* seqNum);

    /// @brief Stop the transport and cleanup resources. Blocks until native threads exit.
    /// Messages still awaiting an ACK are dropped without a final status.
    DMQ_EXPORT void DMQ_CALL DmqInterop_Stop();
}

#endif
