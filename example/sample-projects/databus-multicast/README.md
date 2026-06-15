# DataBus Multicast System Architecture Example

This example demonstrates a distributed system using `dmq::databus::DataBus` over **UDP Multicast** for efficient one-to-many distribution. It implements a simulated sensor and actuator system where a single publisher (Server) can reach any number of subscribers (Clients) simultaneously.

## Architecture

- **Server (Publisher)**:
  - Initializes a `MulticastTransport` in `PUB` mode.
  - Automatically detects the machine's primary physical network interface.
  - Publishes `DataMsg` to the multicast group `239.1.1.1:8000`.
  - Distributed as "Best Effort" (no individual ACKs required).

- **Client (Subscriber)**:
  - Initializes a `MulticastTransport` in `SUB` mode and joins the group.
  - Registers a `RemoteHandler` to bridge network data to the **local** DataBus.
  - Application components subscribe to topic names and receive data as if it were local.

## How to Run

1. **Configure and Build**:
   Use the `03_generate_samples.py` script in the root directory, or run CMake manually in both `client` and `server` folders:
   ```bash
   cmake -B build .
   cmake --build build --config Release
   ```

2. **Start the Server**:
   ```bash
   ./server/build/Release/delegate_databus_multicast_server.exe
   ```

3. **Start the Client(s)**:
   You can run multiple instances of the client; they will all receive the same data stream.
   ```bash
   ./client/build/Release/delegate_databus_multicast_client.exe
   ```

## DataBus Spy Support

This project includes optional support for the **DelegateMQ Spy Console**. When enabled, the application exports all bus traffic to a multicast group, allowing multiple consoles to monitor the system at once.

### Prerequisites

### Enabling Spy
To enable the spy bridge, configure the project with the `DMQ_DATABUS_TOOLS` option set to `ON`:

```bash
cmake -B build -DDMQ_DATABUS_TOOLS=ON .
cmake --build build --config Release
```

### Monitoring Multicast Traffic
Once built, start the `dmq-spy` application (built from `tools/` with `-DDMQ_TOOLS=ON`) using the matching multicast group. See [tools/TOOLS.md](../../../tools/TOOLS.md) for details.

```bash
# Start the spy console to join the multicast group
./dmq-spy 9999 --multicast 239.1.1.1
```

## Key Features Shown

- **One-to-Many Distribution**: Efficiently reach multiple network nodes with a single publish.
- **Location Transparency**: Application code remains agnostic to whether data is local (in-process) or remote (multicast group).
- **Auto-Interface Detection**: Automatically identifies and binds to the physical network adapter, bypassing Windows routing issues.
- **Asynchronous Spying**: High-visibility diagnostics without impacting real-time loop performance.
