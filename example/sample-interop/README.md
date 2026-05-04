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
    .\build\Release\InteropServer.exe
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
