# DataBus System Architecture Example

This example demonstrates a distributed system using `dmq::databus::DataBus` for inter-process communication over UDP. It implements a simulated sensor and actuator system across two standalone applications (Client and Server), showcasing location transparency and decoupled architecture.

## Architecture

- **Server (Publisher)**:
  - Initializes a `UdpTransport` in `PUB` mode.
  - Adds a `Participant` representing the Client endpoint.
  - Periodically publishes `DataMsg` to the `Topic/DataMsg` topic.
  - The Data Bus handles the internal routing, serialization, and network transmission.

- **Client (Subscriber)**:
  - Initializes a `UdpTransport` in `SUB` mode.
  - Registers a `RemoteHandler` for the `DataMsg` topic.
  - When network data arrives, it is automatically republished to the **local** Data Bus.
  - Application components subscribe to the topic name and receive the data as if it were local.

## How to Run

1. **Configure and Build**:
   Use the `03_generate_samples.py` script in the root directory, or run CMake manually in both `client` and `server` folders:
   ```bash
   cmake -B build .
   cmake --build build --config Release
   ```

2. **Start the Server**:
   ```bash
   ./server/build/Release/delegate_databus_server_app.exe
   ```

3. **Start the Client**:
   ```bash
   ./client/build/Release/delegate_databus_client_app.exe
   ```

## DataBus Spy Support

This project includes optional support for the **DelegateMQ Spy Console**. When enabled, the application exports all bus traffic to an external TUI dashboard for real-time monitoring.

### Prerequisites

### Enabling Spy
To enable the spy bridge, configure the project with the `DMQ_DATABUS_TOOLS` option set to `ON`:

```bash
cmake -B build -DDMQ_DATABUS_TOOLS=ON .
cmake --build build --config Release
```

Once built, start the `dmq-spy` application (built from `tools/` with `-DDMQ_TOOLS=ON`) before running the client/server. See [tools/TOOLS.md](../../../tools/TOOLS.md) for details.

## Key Features Shown

- **Location Transparency**: Application code remains agnostic to whether data is local (in-process) or remote (network).
- **UDP Distribution**: Zero-config point-to-point data delivery.
- **Custom Serialization**: Uses the lightweight `msg_serialize` classes for dependency-free binary serialization.
- **Asynchronous Spying**: Demonstrates how to hook into the bus monitor to export diagnostic data without impacting real-time performance.
