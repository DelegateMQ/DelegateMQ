# DelegateMQ Sample Interop

This directory contains a complete cross-language demonstration of DelegateMQ. It shows a C++ server communicating with C# and Python clients using a shared native C++ DLL.

For architecture details and synchronization strategies, see:
**[docs/INTEROP.md](../../docs/INTEROP.md)**

## Topology

- **C++ Server**: Publishes `SensorData` (ID 100) and receives `Command` (ID 101).
- **C# Client**: Subscribes to `SensorData` and sends `Command`.
- **Python Client**: Subscribes to `SensorData` and sends `Command`.

## Running the Sample

1.  **Build the Native DLL**:
    ```powershell
    cd interop/native
    cmake -B build .
    cmake --build build --config Release
    ```

2.  **Start the C++ Server**:
    ```powershell
    cd example/sample-interop/cpp-server
    cmake -B build .
    cmake --build build --config Release
    # Windows:
    .\build\Release\InteropServer.exe
    # Linux:
    ./build/InteropServer
    ```

3.  **Start the C# Client**:
    ```powershell
    cd example/sample-interop/csharp
    dotnet run
    ```

4.  **Start the Python Client**:
    ```powershell
    cd example/sample-interop/python
    python main.py
    ```

Each app runs until stopped (Ctrl+C), or for a set time when given one in seconds: `InteropServer 14`, `dotnet run -- 10`, `python main.py 10`. A timed client prints how many sensor messages it received and how many commands the server acknowledged, and exits with status 1 unless both are above zero. `build_all.py` runs the server with the Python client and, on Windows, the C# client this way.

## Delivery Status

Both clients send their commands reliably: each is tracked until the server ACKs it and resent on timeout. The console shows the outcome of each command by sequence number:

```
[SEND] Command seq=1: pollingRateMs=250
[ACK]  Command seq=1 acknowledged by server
```

Stop the server while a client is running to see the retry path: each unacknowledged command reports `[WARN] ... retrying` on every timeout (default 2 s, 3 retries), then `[FAIL] ... not delivered after retries`. Restart the server and any command still retrying is delivered by its next attempt and ACKed, as are later commands. The server applies each command once, even if a retry resends one it already received. See [Reliability and Delivery Status](../../docs/INTEROP.md#reliability-and-delivery-status).
