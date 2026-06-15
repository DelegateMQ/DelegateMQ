# DataBus Shapes Demo Example

The **Shapes Demo** is a graphical demonstration of the **DelegateMQ DataBus** capabilities. It provides a visual proof-of-concept for high-performance messaging, location transparency, and multicast distribution entirely within a modern Terminal User Interface (TUI).

<img src="ShapesDemo.png" alt="DelegateMQ Shapes Demo" style="max-width: 800px; width: 100%;">

## Key Features

- **Location Transparency**: The client renders shape movements calculated in a completely separate server process.
- **UDP Multicast**: A single publisher (Server) can drive any number of graphical clients simultaneously with zero extra network overhead per client.
- **Asynchronous Rendering**: Demonstrates thread-safe data transfer between a background network thread and the main UI rendering thread.
- **TUI Graphics**: Uses the [FTXUI](https://github.com/ArthurSonzogni/FTXUI) library to draw high-resolution shapes inside a standard terminal.

## How to Run

1. **Configure and Build**:
   Use the `03_generate_samples.py` script in the root directory, or run CMake manually in both `client` and `server` folders:
   ```bash
   cmake -B build .
   cmake --build build --config Release
   ```

2. **Start the Server**:
   The server calculates shape trajectories and broadcasts them to the multicast group.
   ```bash
   ./server/build/Release/delegate_databus_shapes_server.exe
   ```

3. **Start Multiple Clients**:
   You can run multiple instances of the client simultaneously. Because the system uses **UDP Multicast**, all clients will show the shapes moving in perfect synchronization.
   ```bash
   ./client/build/Release/delegate_databus_shapes_client.exe
   ```

## DelegateMQ Tools Integration

This project integrates with both **dmq-spy** (DataBus message monitor) and **dmq-monitor** (network node monitor). Both tools are built from `tools/` with `-DDMQ_TOOLS=ON`. See [tools/TOOLS.md](../../../tools/TOOLS.md) for details.

### Prerequisites

### Enabling the Tool Bridges
Build the client and server with `DMQ_DATABUS_TOOLS=ON` to enable the bridge components:

```bash
cmake -B build -DDMQ_DATABUS_TOOLS=ON .
cmake --build build --config Release
```

### dmq-spy (DataBus Message Feed)
The **server** publishes all DataBus traffic to `dmq-spy` via `SpyBridge`. To view the raw coordinate data flowing through the bus:

1. Start the **Spy Console**:
   ```bash
   ./dmq-spy 9999 --multicast 239.1.1.1
   ```
2. Run the Shapes Demo server.
3. Observe the `Shape/Square` and `Shape/Circle` coordinate updates in real-time.

### dmq-monitor (Node Topology View)
Both the **server** and **client** send periodic heartbeats to `dmq-monitor` via `NodeBridge`, reporting uptime, message counts, and active topics.

1. Start the **Monitor Console**:
   ```bash
   ./dmq-monitor
   ```
2. Run the Shapes Demo server and client(s).
3. Each running process appears as a separate row in the live node table.

## Technical Details

- **Multicast Group**: `239.1.1.1:8000`
- **TUI Refresh Rate**: 20 FPS (Optimized for terminal performance)
- **Automatic Interface Detection**: The demo automatically identifies and binds to the primary physical network adapter, ensuring reliable multicast routing on Windows and Linux.
