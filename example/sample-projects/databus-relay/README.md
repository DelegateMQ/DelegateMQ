# DataBus Relay: Bridging Two Different Links

Three processes carry DataBus topics across two *different* physical links, with the middle node relaying between them:

```
  sensor (A)  <--- Ethernet (UDP) --->  gateway (B)  <--- local IPC --->  monitor (C)
```

The sensor and monitor never talk to each other directly, and neither knows the other link exists. The gateway is on both links and forwards each topic across.

| Topic | Path | Purpose |
|-------|------|---------|
| `sensor/temp` (`TempMsg`) | sensor → UDP → **gateway** → pipe → monitor | telemetry, 2 Hz |
| `cmd/setpoint` (`SetpointMsg`) | monitor → pipe → **gateway** → UDP → sensor | command, every 3 s |

The sensor's simulated temperature drifts toward the current setpoint, so the round trip shows up in the output: the monitor changes the setpoint, and the readings it receives afterwards follow it.

**Platform:** Windows: named pipe / Linux: Unix domain socket. The local IPC hop ("the pipe" below) uses `Win32PipeTransport` on Windows and `LinuxUdsTransport` (`AF_UNIX` datagram socket) on Linux. Both are duplex, so the code is the same apart from creating the transport.

## Relaying Is Explicit

DelegateMQ does no automatic routing. When a node receives a remote topic, it handles it one of two ways:

| API | Received data goes to | Use on |
|-----|-----------------------|--------|
| `DataBus::AddIncomingTopic` | local subscribers only (`PublishLocal`) | ordinary nodes (sensor, monitor) |
| `DataBus::AddRelayTopic` | local subscribers **and** every participant that lists the topic as outgoing (`Publish`) | relay nodes (gateway) |

So with ordinary wiring, data from the sensor would stop at the gateway. The gateway opts in per topic:

```cpp
// sensor/temp: inbound on the UDP participant, outgoing on the pipe participant
DataBus::AddRelayTopic<TempMsg>("sensor/temp", SensorTempId, *udpInParticipant, tempSerializer);
pipeParticipant->AddRemoteTopic("sensor/temp", SensorTempId);

// cmd/setpoint: inbound on the pipe participant, outgoing on the UDP participant
DataBus::AddRelayTopic<SetpointMsg>("cmd/setpoint", CmdSetpointId, *pipeParticipant, setpointSerializer);
udpOutParticipant->AddRemoteTopic("cmd/setpoint", CmdSetpointId);
```

DDS behaves the same way. A DDS participant doesn't forward between networks either: bridging needs a separately deployed service (RTI Routing Service, eProsima DDS Router, or an XRCE Agent for serial-attached MCUs). Here, the relay is just those few lines in the gateway.

## Things to Notice

- **One-way wiring, no loops.** Each relayed topic comes in on one link and goes out on the other. `AddRelayTopic` never makes the inbound participant outgoing for the topic it relays. That matters on the pipe: it's one duplex participant that both receives `cmd/setpoint` and sends `sensor/temp`, and the setpoint is not echoed back to the monitor. Never relay the same topic in both directions on the same node, and never have two relays forward a topic toward each other: that is the [relay loop hazard](../../../docs/DATABUS.md#relay-loop-hazard).
- **The relay node is also a normal node.** Relayed data reaches the gateway's own subscribers too, which is where the `[gateway] relayed ...` lines come from.
- **Two transports in one process.** CMake selects one transport (`DMQ_TRANSPORT_WIN32_UDP` or `DMQ_TRANSPORT_LINUX_UDP`) for `DelegateMQ.h`. Desktop transports are header-only, so the gateway also `#include`s the IPC transport header (`win32-pipe/Win32PipeTransport.h` or `linux-uds/LinuxUdsTransport.h`) directly. No library or build-option change is needed.
- **Each hop is independent.** Remote IDs only have to match between the two ends of one hop, and each hop deserializes and re-serializes. Any delivery guarantee is per hop too: if the UDP hop used `ReliableTransport`, an ACK would mean "the gateway got it", not "the monitor got it". This sample sends everything unreliable; a local pipe or Unix datagram socket doesn't drop messages.
- **Pipe start order (Windows).** The gateway is the pipe server and creates `\\.\pipe\dmq_relay`, and the monitor connects to it. The monitor retries for up to 10 s, but the gateway has to be started first. Until the monitor connects, the gateway's pipe sends fail and it reports that once. The pipe takes a single client and doesn't accept a reconnect: if the monitor exits, restart the gateway too.
- **No start order (Linux).** A Unix datagram socket has no connection. The gateway binds `/tmp/dmq_relay_gateway.sock`, the monitor binds `/tmp/dmq_relay_monitor.sock`, and each sends to the other's path. Nodes can start in any order and the monitor can be restarted freely: sends to a peer that isn't running yet fail (the gateway reports that once) and succeed as soon as it binds. Each node removes a stale socket file from a crashed run before binding, and removes its own on exit. Starting a second copy of a node while the first is running fails with "in use by another process" rather than taking over its socket. The paths are in `/tmp` for simplicity; a real deployment would use a per-user directory such as `$XDG_RUNTIME_DIR`.

## Files

| Path | Contents |
|------|----------|
| `common/` | `TempMsg`, `SetpointMsg`, topic names, remote IDs, ports, pipe name and socket paths |
| `sensor/` | UDP-only node: publishes `sensor/temp`, receives `cmd/setpoint` |
| `gateway/` | UDP + IPC node: relays both topics |
| `monitor/` | IPC-only node: receives `sensor/temp`, publishes `cmd/setpoint` |
| `run_databus_relay.py` | launches all three nodes, each in its own console |

| Link | Endpoint |
|------|----------|
| sensor → gateway | UDP `127.0.0.1:5020` (gateway listens) |
| gateway → sensor | UDP `127.0.0.1:5021` (sensor listens) |
| gateway ↔ monitor (Windows) | named pipe `\\.\pipe\dmq_relay` (gateway = server) |
| gateway ↔ monitor (Linux) | Unix datagram sockets `/tmp/dmq_relay_gateway.sock` ↔ `/tmp/dmq_relay_monitor.sock` |

## Build

From the repo root, `scripts/03_generate_samples.py` then `scripts/04_build_samples.py` builds all three nodes. To build only this sample, run this in each of `gateway/`, `monitor/` and `sensor/`:

```bash
cmake -B build .
cmake --build build --config Release
```

## Run

```bash
python run_databus_relay.py                 # three consoles, run until Ctrl+C
python run_databus_relay.py --duration 30   # each node exits after 30 s
```

Each app also takes an optional run duration in seconds as its first argument. To start them by hand on Windows, start them in this order: gateway, monitor, sensor. On Linux any order works.

Expected output (abridged):

```
[sensor]  publish sensor/temp: 30 C (seq 14)
[gateway] relayed sensor/temp: 30 C (seq 14)  UDP -> pipe
[monitor] sensor/temp received: 30 C (seq 14)
[monitor] publish cmd/setpoint: 18 C
[gateway] relayed cmd/setpoint: 18 C  pipe -> UDP
[sensor]  setpoint received: 18 C
[monitor] sensor/temp received: 27 C (seq 15)
```
