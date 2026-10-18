Remote delegates over MQTT (`MqttTransport`) with RapidJSON serialization. `pub` sends data, `sub` receives it, both through an MQTT broker.

## Broker

The apps connect to a local broker at `tcp://localhost:1883` by default. Install and start one, e.g. [Mosquitto](https://mosquitto.org/download/):

```bash
mosquitto -v                  # listens on localhost:1883
```

To use a different broker, pass its URI as the first argument to both apps:

```bash
delegate_pub_app tcp://10.0.0.5:1883
delegate_sub_app tcp://10.0.0.5:1883
```

A public test broker (e.g. `tcp://broker.hivemq.com:1883`) works for a quick try without installing anything, but anyone can read and publish on it. Don't send real data there.

## Build and Run

1. Set up the external library dependencies (see [Example Ecosystem (Sandbox)](../../../docs/BUILD.md#example-ecosystem-sandbox) in `BUILD.md`).
2. Run CMake in the `pub` and `sub` directories:
   `cmake -B build .`
3. Build both applications in their `build` directories.
4. Start the broker.
5. Start `delegate_pub_app`, then `delegate_sub_app`, with the same broker argument if you use one.
6. The apps exchange data through the broker and print it to the console.
