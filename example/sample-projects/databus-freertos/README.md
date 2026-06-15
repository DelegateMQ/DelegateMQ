# DataBus FreeRTOS Example

Demonstrates `dmq::databus::DataBus` communication between a **FreeRTOS server** (Win32 simulator) and a **standard C++ client** (Windows or Linux). The server simulates an embedded node that publishes periodic sensor readings and alarm state changes; the client subscribes to both and sends rate-control commands back.

The transport layer is the only platform-specific piece. Swapping `Win32UdpTransport` for `ArmLwipUdpTransport` on real hardware requires no changes to the DataBus or application logic.

## Architecture

```
  FreeRTOS Server (Win32 sim)          Linux/Windows Client
  ┌──────────────────────────┐         ┌──────────────────────────┐
  │  Server task (priority 3)│         │  main thread             │
  │  - PublishSensor() loop  │─UDP────>│  - DataBus::Subscribe    │
  │  - PublishAlarm() every  │ 9000    │    prints sensor/temp    │
  │    3 sensor cycles       │         │    prints alarm/status   │
  │  - vTaskDelay(intervalMs)│         │                          │
  │                          │         │                          │
  │  Poll task (priority 2)  │         │  poll thread             │
  │  - ProcessIncoming()     │<─UDP────│  - ProcessIncoming()     │
  │  - adjusts intervalMs    │ 9001    │                          │
  └──────────────────────────┘         └──────────────────────────┘
```

| Direction | Topic | Port | Description |
|-----------|-------|------|-------------|
| Server → Client | `sensor/temp`  | 9000 | Periodic `SensorMsg` (temperature + id) |
| Server → Client | `alarm/status` | 9000 | `AlarmMsg` (alarm id + active state), every 3 sensor cycles |
| Client → Server | `cmd/rate`     | 9001 | `CmdMsg` to change publish interval |

Port 9000 carries both `SensorMsg` and `AlarmMsg` on the same UDP socket. The `DmqHeader` `DelegateRemoteId` field demultiplexes them on the wire — no per-message-type socket is required.

### FreeRTOS task priorities

| Task | FreeRTOS priority | Role |
|------|------------------|------|
| vServerTask | 3 | Publish loop — calls `vTaskDelay` between publishes |
| SrvPoll (Thread) | 2 | Receives commands — blocks in `recvfrom` |
| Timer/Daemon | 6 | `Timer::ProcessTimers()` every 10 ms |

The poll thread is deliberately **lower** priority than vServerTask. The FreeRTOS Windows simulator blocks OS threads inside `recvfrom`, which is invisible to the FreeRTOS scheduler. If the poll thread ran at higher priority, it would starve vServerTask.

## Directory Layout

```
databus-freertos/
  common/          Shared message types and serializers (header-only)
    Msg.h            SensorMsg, CmdMsg, AlarmMsg, topic names and remote IDs
    MsgSerializer.h  Serializer<> aliases for all three message types
  server/          FreeRTOS server — 32-bit Windows only
    Server.h         Active-object: publish loop (sensor + alarm) + poll thread
    main.cpp         FreeRTOS entry point, heap_5 init, software timer
    FreeRTOSConfig.h Scheduler config (1 kHz tick, heap_5, static allocation)
    CMakeLists.txt
  client/          Standard C++ client — Windows or Linux
    Client.h         Active-object: subscribe to sensor/temp and alarm/status, publish commands
    main.cpp         Entry point, toggles rate 500 ms ↔ 2000 ms every 5 s
    CMakeLists.txt
```

## Build

### Server (FreeRTOS Windows simulator — 32-bit Windows only)

The FreeRTOS Win32 simulator port requires a **32-bit** (x86) build. A 64-bit build will fail at link time.

```bat
cd server
cmake -A Win32 -B build .
cmake --build build --config Release
```

Executable: `server\build\Release\delegate_databus_freertos_server.exe`

### Client (Windows or Linux)

```bash
cd client
cmake -B build .
cmake --build build --config Release
```

Executable: `client\build\Release\delegate_databus_freertos_client.exe` (Windows) or `client/build/delegate_databus_freertos_client` (Linux)

## Running

Start the server first, then the client. Both can run in separate terminals on the same machine.

**Terminal 1 — server:**
```bat
server\build\Release\delegate_databus_freertos_server.exe
```

**Terminal 2 — client:**
```bat
client\build\Release\delegate_databus_freertos_client.exe
```

Expected output (server):
```
--- Starting FreeRTOS Scheduler (DataBus Server) ---
[Server] Started. Publishing on sensor/temp every 1000ms.
[Server] Publishing sensor/temp: 20.0 C (interval=1000ms)
[Server] Publishing sensor/temp: 20.1 C (interval=1000ms)
[Server] Publishing sensor/temp: 20.2 C (interval=1000ms)
[Server] Publishing alarm/status: id=1  ACTIVE
[Server] Publishing sensor/temp: 20.3 C (interval=1000ms)
[Server] Received CmdMsg: intervalMs=500
[Server] Publishing sensor/temp: 20.4 C (interval=500ms)
...
```

Expected output (client):
```
Starting DataBus FreeRTOS CLIENT...
[Client] Started. Receiving on sensor/temp and alarm/status.
Sending rate commands every 5s... Press Ctrl+C to quit.
[Client] sensor/temp: id=1  temp=20 C
[Client] sensor/temp: id=1  temp=20.1 C
[Client] sensor/temp: id=1  temp=20.2 C
[Client] alarm/status: id=1  ACTIVE
[Client] sensor/temp: id=1  temp=20.3 C
[Client] Sending cmd/rate: intervalMs=500
...
```

The client alternates the server's publish interval between 500 ms and 2000 ms every 5 seconds. The alarm toggles active/inactive every 3 sensor publishes. Press `Ctrl+C` to stop the client.

## Porting to Real Hardware

The only change needed to move the server from the Win32 simulator to a real FreeRTOS + lwIP target:

1. Replace `port/transport/win32-udp` with `port/transport/arm-lwip-udp` in the build.
2. Update `CMakeLists.txt` to set `DMQ_TRANSPORT` to `DMQ_TRANSPORT_ARM_LWIP_UDP` and link against lwIP.
3. `Server.h`, `Msg.h`, and `MsgSerializer.h` are unchanged.

## Key DataBus Calls

| Call | Where | Purpose |
|------|-------|---------|
| `DataBus::AddParticipant` | Server, Client | Register a network endpoint |
| `DataBus::RegisterSerializer` | Server, Client | Bind a serializer to a topic |
| `DataBus::AddIncomingTopic` | Server, Client | Bridge network → local bus |
| `DataBus::Subscribe` | Server, Client | Subscribe a callback to a topic |
| `DataBus::Publish` | Server, Client | Publish a message to a topic |

See [DelegateMQ DataBus documentation](../../../docs/DATABUS.md) for full API details.
