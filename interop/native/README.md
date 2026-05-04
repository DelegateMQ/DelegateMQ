# DelegateMQ Native Interop DLL

This project builds the `DmqInterop` shared library (DLL), which provides a C-compatible API for DelegateMQ remote communication. It is designed to be consumed by other languages like Python and C#.

## Features
- **Reliability**: Implements the `DmqHeader` protocol and automatic ACK handling via `TransportMonitor`.
- **Background Threading**: Runs a native receive loop to handle network traffic efficiently.
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
// Starts the transport
int DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort);

// Registers a callback for a specific ID
void DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb);

// Sends raw bytes
int DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len);

// Stops the transport
void DmqInterop_Stop();
```
