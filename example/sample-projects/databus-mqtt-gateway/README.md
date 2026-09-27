# DataBus ↔ MQTT Gateway Demo

A simulated thermostat that is an ordinary DataBus application, exposed to MQTT tools. Its topics are declared once with `JsonTopics` ([`tools/bridge/common`](../../../tools/bridge/common/JsonTopics.h)), the shared JSON bridge layer, and served over MQTT by `MqttBridge` ([`tools/bridge/mqtt`](../../../tools/bridge/mqtt/MqttBridge.h)). Each DataBus topic becomes its own MQTT topic with a JSON payload, so anything that speaks MQTT (MQTT Explorer, Node-RED, Home Assistant, Grafana, cloud IoT) can watch it and change its setpoint.

| Direction | MQTT topic | Payload |
|-----------|-----------|---------|
| Out | `thermo/room/climate` | `{"celsius":20.4,"humidity":41.0}` every second |
| Out (retained) | `thermo/thermostat/status` | `{"setpoint":21.0,"heating":true}` on change |
| Out (retained) | `thermo/online` | `true` while running; `false` on exit or via Last Will if it dies |
| In | `thermo/thermostat/setpoint/set` | `{"celsius":23.5}` (5–35 °C accepted) |

The application code is plain DataBus: `Thermostat` publishes `Climate` and `ThermostatStatus` and subscribes to `SetpointCmd`. The only bridge-facing code is three small JSON converter functions, three `JsonTopics` registrations and the `MqttBridge` start/stop in `main()`.

## Build

Paho MQTT C is built from the workspace checkout (`../mqtt`, fetched by `01_fetch_repos.py`).

```bash
cmake -B build .
cmake --build build --config Release
```

## Run

You need an MQTT broker. [Mosquitto](https://mosquitto.org/) is the usual choice (`mosquitto -v`, or `docker run -p 1883:1883 eclipse-mosquitto`).

```bash
# Windows: build\Release\databus_mqtt_gateway_app.exe
./build/databus_mqtt_gateway_app                          # broker tcp://127.0.0.1:1883, prefix "thermo"
./build/databus_mqtt_gateway_app tcp://192.168.1.10:1883 --prefix lab1
```

Watch everything it publishes:

```bash
mosquitto_sub -v -t 'thermo/#'
```

Change the setpoint from MQTT. The thermostat reacts, and the new status comes straight back out on `thermo/thermostat/status`:

```bash
mosquitto_pub -t thermo/thermostat/setpoint/set -m '{"celsius":23.5}'
mosquitto_pub -t thermo/thermostat/setpoint/set -m '{"celsius":99}'    # rejected: out of range
```

[MQTT Explorer](https://mqtt-explorer.com/) shows the same topics as a live tree with history charts and a publish panel.

## What to Notice

- **Retained state.** Connect a subscriber after the demo starts: it immediately gets the current status, because the bridge publishes it retained. State published before the broker connection came up is re-published once it connects.
- **Liveness.** Kill the demo (not Ctrl-C) and `thermo/online` becomes `false` anyway: the broker publishes the bridge's Last Will.
- **Reconnect.** Start the demo with no broker running, then start the broker. The bridge reports the failure once and connects when the broker appears.
- **Validation.** Rejected commands are reported in the demo's console (`[Bridge] MQTT: rejected command on ...`) and never reach the DataBus.
