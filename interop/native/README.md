# DelegateMQ Native Interop DLL

This project builds the `DmqInterop` shared library (DLL), which provides a C-compatible API for DelegateMQ remote communication. It is designed to be consumed by other languages like Python and C#.

## Features
- **Reliability**: Outgoing messages use the same stack as a C++ `NetworkNode` in `RELIABLE` mode (`ReliableTransport` → `RetryMonitor` → `TransportMonitor`): each message is tracked until the peer ACKs it, resent on timeout, and its outcome (`ACKED`, `TIMEOUT`, `DELIVERY_FAILED`) reported per sequence number.
- **Structured Errors**: An error callback with a code (`DmqErrorCode`), related remote ID and message, including backpressure (`CAP_EXCEEDED`, `PENDING_EXCEEDED`).
- **Background Threading**: A native receive thread for incoming data, and a separate thread that drains ACKs and drives timeouts/retries, so ACK handling isn't held up by the multicast receive.
- **Portability**: Uses standard Winsock/Sockets code compatible with Windows and Linux.

## Build Instructions

### Windows
```powershell
cmake -B build .
cmake --build build --config Release
```
Output: `build/bin/Release/DmqInterop.dll`

### Linux
```bash
cmake -B build .
cmake --build build --config Release
```
Output: `build/lib/libDmqInterop.so`

## C-API (`DmqInterop.h`)

```cpp
// Optional, before Start: outgoing reliability (default: on, 2 s timeout, 3 retries)
int DmqInterop_SetReliability(int enabled, int timeoutMs, int maxRetries);

// Starts the transports and native threads
int DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort, const char* multicastGroup);

// Registers a callback for a specific ID (NULL unregisters)
void DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb, void* context);

// Per-message delivery status: callback(context, remoteId, seqNum, DmqSendStatus)
void DmqInterop_RegisterStatusCallback(DmqStatusCallback cb, void* context);

// Errors: callback(context, DmqErrorCode, remoteId or 0, message)
void DmqInterop_RegisterErrorCallback(DmqErrorCallback cb, void* context);

// Sends raw bytes; the sequence number (for matching status callbacks) is returned via seqNum
int DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len, uint16_t* seqNum);

// Stops the transport (blocks until native threads exit)
void DmqInterop_Stop();
```

See `DmqInterop.h` for the status and error code values, and [docs/INTEROP.md](../../docs/INTEROP.md) for the reliability model.
