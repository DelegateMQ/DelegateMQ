#ifndef DMQ_INTEROP_H
#define DMQ_INTEROP_H

#include <stdint.h>

/// @file DmqInterop.h
/// @brief C-compatible API for DelegateMQ remote communication DLL.
/// 
/// This DLL wraps the C++ DelegateMQ core (transports, headers, threads)
/// to provide a simple interface for C#, Python, and other languages.

#ifdef _WIN32
#define DMQ_EXPORT __declspec(dllexport)
#define DMQ_CALL __stdcall
#else
#define DMQ_EXPORT __attribute__((visibility("default")))
#define DMQ_CALL
#endif

extern "C" {
    /// @brief Callback signature for received messages.
    /// @param remoteId The DelegateRemoteId (topic ID).
    /// @param data Pointer to the raw payload bytes.
    /// @param len Length of the payload in bytes.
    typedef void (DMQ_CALL *DmqMessageCallback)(uint16_t remoteId, const uint8_t* data, uint32_t len);

    /// @brief Callback signature for error messages.
    /// @param msg The error message string.
    typedef void (DMQ_CALL *DmqErrorCallback)(const char* msg);

    /// @brief Initialize and start the UDP transport and background threads.
    /// @param remoteHost IP address of the remote peer.
    /// @param recvPort Local port to bind for incoming messages.
    /// @param sendPort Remote port to send messages and ACKs.
    /// @return 0 on success, non-zero on error.
    DMQ_EXPORT int DMQ_CALL DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort);
    
    /// @brief Register a callback for a specific Remote ID.
    /// @param remoteId The DelegateRemoteId to listen for.
    /// @param cb The function to call when data arrives.
    DMQ_EXPORT void DMQ_CALL DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb);

    /// @brief Register a callback for internal library errors and faults.
    /// @param cb The function to call when an error occurs.
    DMQ_EXPORT void DMQ_CALL DmqInterop_RegisterErrorCallback(DmqErrorCallback cb);
    
    /// @brief Send raw bytes to a Remote ID.
    /// @details The DLL handles the DmqHeader framing and sequence numbers.
    /// @param remoteId The destination DelegateRemoteId.
    /// @param data Pointer to the payload bytes to send.
    /// @param len Length of the payload in bytes.
    /// @return 0 on success, non-zero on error.
    DMQ_EXPORT int DMQ_CALL DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len);
    
    /// @brief Stop the transport and cleanup resources.
    DMQ_EXPORT void DMQ_CALL DmqInterop_Stop();
}

#endif
