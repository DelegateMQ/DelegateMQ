# Bridges

A bridge is a small component you add to your own application to send its DataBus activity outside the process. Bridges are opt-in: nothing runs and nothing costs anything until you call a bridge's `Start()`.

There are two kinds, and they aren't related.

| Kind | Folders | Output | Read by |
|------|---------|--------|---------|
| **Diagnostic** | `spy/`, `node/` | DelegateMQ binary packets over UDP | DelegateMQ's own consoles: `dmq-spy`, `dmq-monitor`, `dmq-thread` |
| **JSON** | `common/`, `mqtt/` | One topic per DataBus topic, readable JSON payloads | Any third-party tool: MQTT Explorer, Node-RED, Home Assistant, Grafana, `mosquitto_sub`, ... |

Use a **diagnostic bridge** to watch and debug a DelegateMQ system with the bundled tools. Use a **JSON bridge** to let other software see, and optionally command, your application.

Neither one connects DelegateMQ apps to each other. That's the job of a transport (`src/delegate-mq/port/transport/`).

## Folders

| Folder | Contents | What it does | Details |
|--------|----------|--------------|---------|
| `spy/` | `SpyBridge` | Sends every DataBus message (topic, timestamp, value as text) to the `dmq-spy` console, unicast or multicast. Register a stringifier per topic to see its values. | [TOOLS.md: SpyBridge](../TOOLS.md#integrating-spybridge-into-your-app) |
| `node/` | `NodeBridge`, `NodeInfoPacket.h` | Sends a heartbeat once a second with the node's ID, uptime, published topics and message counts to `dmq-monitor`. Also carries `ThreadMonitor` statistics to `dmq-thread`. Topics are discovered automatically. | [TOOLS.md: NodeBridge](../TOOLS.md#integrating-nodebridge-into-your-app), [thread monitoring](../TOOLS.md#integrating-thread-monitoring-into-your-app) |
| `common/` | `JsonTopics`, `BridgeJson.h`, `BridgeCommon.cmake` | Shared layer for JSON bridges. The application declares once which topics to expose as JSON (`Expose<T>`) and which commands to accept (`Accept<T>`). Every JSON bridge serves that same set. `BridgeJson.h` is a small flat-JSON writer and reader for the converters. | [TOOLS.md: JsonTopics](../TOOLS.md#jsontopics-shared-layer) |
| `mqtt/` | `MqttBridge`, `MqttBridge.cmake` | JSON bridge to an MQTT broker. Exposed topic `t` goes to `<prefix>/t`, and commands arrive on `<prefix>/t/set`. Latched topics are retained, reliable topics use QoS 1, and `<prefix>/online` reports the connection. | [TOOLS.md: MqttBridge](../TOOLS.md#mqttbridge), [Using MQTT Tools](../TOOLS.md#using-mqtt-tools) |

`MqttBridge` is different from `port/transport/mqtt/MqttTransport`. The transport carries DelegateMQ's binary messages between DelegateMQ apps on one fixed MQTT topic, which other MQTT tools can't read.

## Adding a Bridge to Your Build

**Diagnostic bridges:**
1. Enable `DMQ_DATABUS_TOOLS`.
2. Add the bridge's `.cpp` to your target, e.g. `tools/bridge/spy/SpyBridge.cpp` or `tools/bridge/node/NodeBridge.cpp`.
3. Add the include directories for the bridge folder and `tools/net`.

```cpp
SpyBridge::Start("127.0.0.1", 9999, "Node-Alpha");    // to dmq-spy
NodeBridge::Start("Node-Alpha", "127.0.0.1", 9998);   // to dmq-monitor / dmq-thread
```

**JSON bridges:** use the bridge's CMake helper. It also adds the `common/` layer, and for MQTT it builds Paho MQTT C from `external/mqtt` (fetched by `01_fetch_repos.py`). Requires `DMQ_DATABUS=ON`. Desktop only.

```cmake
include(<DelegateMQ>/tools/bridge/mqtt/MqttBridge.cmake)
dmq_add_mqtt_bridge(my_app)
```

```cpp
JsonTopics::Expose<TelemetryMsg>("pump/telemetry", &TelemetryToJson);
JsonTopics::Accept<PumpCmd>("pump/cmd", &CmdFromJson);   // optional; validate everything
MqttBridge::Start(options);
```

## Where They're Used

- **Cellutron** (`example/cellutron/`): `SpyBridge` and `NodeBridge` on every node.
- **Pumptron** (`example/pumptron/`): `MqttBridge` in the GUI, enabled with `--mqtt`.
- **`databus-mqtt-gateway`** (`example/sample-projects/`): a minimal `MqttBridge` example.

## Adding a New Bridge

Put it in its own `tools/bridge/<name>/` folder.

A new **JSON bridge** (for example WebSocket or Foxglove) must:
- implement `JsonTopics::ISink` and serve whatever is registered, with no per-topic API of its own;
- send on its own thread with `FullPolicy::DROP`, and not queue while disconnected;
- replay `JsonTopics::LatchedValues()` on every connect;
- pass inbound commands straight to `JsonTopics::Inbound()`;
- report through `JsonTopics::OnEvent()`, prefixed with the bridge name;
- provide a `<Name>Bridge.cmake` with a `dmq_add_<name>_bridge(target)` helper, following `mqtt/MqttBridge.cmake`.
