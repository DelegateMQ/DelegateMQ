# DataBus Zephyr Example

Demonstrates `dmq::databus::DataBus` communication between a **Zephyr server** (official `native_sim` simulation port) and a **standard C++ client** (Windows or Linux) — the Zephyr counterpart to `databus-freertos`. The server simulates an embedded node that publishes periodic sensor readings and alarm state changes; the client subscribes to both and sends rate-control commands back.

Unlike `zephyr-udp-serializer` (both endpoints inside one `native_sim` process, over Zephyr's own internal simulated loopback), the server and client here are **genuinely separate processes** exchanging real UDP packets over the host's actual loopback interface — see "Networking: offloaded sockets" below for how that's possible without root or extra host setup.

`Server.h`, `Msg.h`, and `MsgSerializer.h` mirror `databus-freertos/server`'s structure closely; `client/` is the same DataBus client used there (Client.h/main.cpp), just naming-adjusted, using `dmq::transport::UdpTransport` generically so it works unchanged against whichever transport the other endpoint speaks.

## Architecture

```
  Zephyr Server (native_sim)           Linux/Windows Client
  ┌──────────────────────────┐         ┌──────────────────────────┐
  │  main() publish loop     │         │  main thread             │
  │  - PublishSensor() loop  │─UDP────>│  - DataBus::Subscribe    │
  │  - PublishAlarm() every  │ 9000    │    prints sensor/temp    │
  │    3 sensor cycles       │         │    prints alarm/status   │
  │  - sleep(intervalMs)     │         │                          │
  │                          │         │                          │
  │  Poll thread (SrvPoll)   │         │  poll thread             │
  │  - ProcessIncoming()     │<─UDP────│  - ProcessIncoming()     │
  │  - adjusts intervalMs    │ 9001    │                          │
  └──────────────────────────┘         └──────────────────────────┘
```

| Direction | Topic | Port | Description |
|-----------|-------|------|-------------|
| Server → Client | `sensor/temp`  | 9000 | Periodic `SensorMsg` (temperature + id) |
| Server → Client | `alarm/status` | 9000 | `AlarmMsg` (alarm id + active state), every 3 sensor cycles |
| Client → Server | `cmd/rate`     | 9001 | `CmdMsg` to change publish interval |

Port 9000 carries both `SensorMsg` and `AlarmMsg` on the same UDP socket, demultiplexed by `DelegateRemoteId` in the `DmqHeader` — same design as `databus-freertos`.

### Zephyr thread priorities

| Thread | Zephyr priority | Role |
|--------|-----------------|------|
| main (publish loop) | 0 (default `CONFIG_MAIN_THREAD_PRIORITY`) | `Server::Run()` — sleeps between publishes |
| SrvPoll (Thread)    | 5 (`SetThreadPriority(5)`) | Receives commands — blocks in `zsock_recvfrom` |
| System work queue   | — | `Timer::ProcessTimers()` every 10ms via `k_timer` |

Zephyr's priority convention is the opposite of FreeRTOS's (lower number = higher priority, same as ThreadX). SrvPoll's priority (5) is already lower than main's (0) by `ZephyrThread`'s own default — set explicitly here for clarity, mirroring `databus-freertos/server`'s equivalent (opposite-direction) reasoning about why the poll thread must not preempt the publish loop.

## Networking: offloaded sockets

`ZephyrUdpTransport` uses Zephyr's own BSD-socket API (`zsock_socket()`, `zsock_bind()`, etc.), but which *actual* network path that reaches depends entirely on Kconfig — and getting a genuinely separate host process talking to a `native_sim` instance turns out to need a specific, less commonly documented option:

* **`CONFIG_NET_LOOPBACK`** (what `zephyr-udp-serializer` uses) is Zephyr's own *simulated* loopback interface, implemented entirely inside that one process's IP stack. A second, separate process (like this sample's client) can never see traffic on it — it never reaches the host kernel at all.
* The TAP-based Ethernet driver (`CONFIG_ETH_NATIVE_POSIX`) reaches the real host network, but needs a `zeth` interface pre-created by `net-tools`' `net-setup.sh`, normally requiring root.
* **`CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`** (what this sample uses) instead forwards every `zsock_*` call straight to the host's own BSD socket API. `ZephyrUdpTransport`'s 127.0.0.1 traffic genuinely goes out through the host kernel's real loopback interface — reachable by the client, an ordinary separate process — with **no host-side setup and no root required**. The tradeoff, per Zephyr's own docs, is that Zephyr's own L2 networking layer isn't exercised (irrelevant here, since `ZephyrUdpTransport` only cares about the socket layer).

See `server/prj.conf` for the exact Kconfig set (`CONFIG_NET_SOCKETS_OFFLOAD`, `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`, `CONFIG_ETH_NATIVE_POSIX=n`).

### A DataBus + Zephyr networking conflict, and the real fix

Getting `DataBus` (needed for `dmq::databus::DataBus`/`Participant`) and `ZephyrUdpTransport` to coexist surfaced a genuine, previously-undetected bug: `extras/util/NetworkConnect.h` (pulled in unconditionally by `DataBus.h`) included the *host's* own BSD socket headers (`<netinet/in.h>`, `<net/if.h>`, ...) whenever `__linux__`/`__unix__`/`_WIN32` was defined — true for `native_sim`'s host-GCC build despite targeting Zephyr — which collided outright with Zephyr's own `<zephyr/net/socket.h>` (`struct sockaddr_in` redefined incompatibly, etc.) the moment both headers landed in the same translation unit. Fixed by excluding those host headers (and the desktop-only `NetworkContext::GetLocalAddress()` code that needs them) whenever an embedded `DMQ_THREAD_*` is active — the same fix already applied to `DMQ_DATABUS`'s own default in `Defaults.cmake`/`DelegateOpt.h` when `zephyr-udp-serializer` was brought up.

## Prerequisites

Same as `zephyr-linux`/`zephyr-udp-serializer` — see those samples' READMEs for full `west` workspace setup. No TAP device or `net-setup.sh` needed here (see above).

## Build Instructions

### Server (Zephyr `native_sim`)

```bash
export ZEPHYR_BASE=/path/to/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host
cd /path/to/zephyrproject
west build -b native_sim/native/64 /path/to/DelegateMQ/example/sample-projects/databus-zephyr/server -d build
./build/zephyr/zephyr.exe
```

### Client (Windows or Linux)

```bash
cd client
cmake -B build .
cmake --build build --config Release
```

Executable: `client/build/delegate_databus_zephyr_client` (Linux) or `client\build\Release\delegate_databus_zephyr_client.exe` (Windows)

## Running

Start the server first, then the client, in separate terminals.

Expected output (server):
```
--- Starting Zephyr DataBus Server (native_sim) ---
[Server] Started. Publishing on sensor/temp every 1000ms.
[Server] Publishing sensor/temp: 20.0 C (interval=1000ms)
[Server] Publishing sensor/temp: 20.1 C (interval=1000ms)
[Server] Publishing sensor/temp: 20.2 C (interval=1000ms)
[Server] Publishing alarm/status: id=1  ACTIVE
[Server] Received CmdMsg: intervalMs=500
[Server] Publishing sensor/temp: 20.3 C (interval=500ms)
...
```

Expected output (client):
```
Starting DataBus Zephyr CLIENT...
[Client] Started. Receiving on sensor/temp and alarm/status.
[Client] sensor/temp: id=1  temp=20.1 C
[Client] sensor/temp: id=1  temp=20.2 C
[Client] alarm/status: id=1  ACTIVE
[Client] Sending cmd/rate: intervalMs=500
[Client] sensor/temp: id=1  temp=20.3 C
...
```

Verified across repeated runs: the client receives both `sensor/temp` and `alarm/status`, and the server receives the client's `cmd/rate` command and visibly changes its publish interval — confirming genuine bidirectional traffic over the real host loopback interface, not two independent processes that merely happen to run at the same time.

## Directory Layout

```
databus-zephyr/
  common/          Shared message types and serializers (header-only, copied unchanged from databus-freertos)
    Msg.h            SensorMsg, CmdMsg, AlarmMsg, topic names and remote IDs
    MsgSerializer.h  Serializer<> aliases for all three message types
  server/          Zephyr server (native_sim)
    Server.h         Active-object: publish loop (sensor + alarm) + poll thread
    main_delegate.cpp  Zephyr entry point, k_timer driving Timer::ProcessTimers()
    prj.conf         Kconfig: base DelegateMQ requirements + offloaded-sockets networking
    CMakeLists.txt
  client/          Standard C++ client — Windows or Linux
    Client.h         Active-object: subscribe to sensor/temp and alarm/status, publish commands
    main.cpp         Entry point, toggles rate 500 ms ↔ 2000 ms every 5 s
    CMakeLists.txt
```

## Porting to Real Hardware

Unlike `databus-freertos` (which needs a transport swap to move off the Win32 simulator), `ZephyrUdpTransport` already uses Zephyr's own native networking subsystem — no transport change is needed to move to real hardware. Only `prj.conf`'s link-layer selection changes: swap the offloaded-sockets Kconfig for a real Ethernet/Wi-Fi driver's config (and its devicetree). `Server.h`, `Msg.h`, and `MsgSerializer.h` are unchanged.

## Key DataBus Calls

| Call | Where | Purpose |
|------|-------|---------|
| `DataBus::AddParticipant` | Server, Client | Register a network endpoint |
| `DataBus::RegisterSerializer` | Server, Client | Bind a serializer to a topic |
| `DataBus::AddIncomingTopic` | Server, Client | Bridge network → local bus |
| `DataBus::Subscribe` | Server, Client | Subscribe a callback to a topic |
| `DataBus::Publish` | Server, Client | Publish a message to a topic |

See [DelegateMQ DataBus documentation](../../../docs/DATABUS.md) for full API details.
