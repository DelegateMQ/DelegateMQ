# DataBus samples

Samples of `dmq::databus::DataBus`, DelegateMQ's topic-based publish/subscribe layer (see [DATABUS.md](../../../docs/DATABUS.md)). Each folder is a separate sample with its own README and CMake build.

| Sample | Shows | External libraries |
| :--- | :--- | :--- |
| [local](./local/README.md) | Single-process `DataBus` across three threads: no transport or serializer between them (the optional `SpyBridge` sends a copy to `dmq-spy`). LVC/lifespan, sync vs. async, `minSeparation`, `SubscribeFilter`, `DeadlineSubscription`, `SubscribeUnhandled`, and `dmq-spy` stringifiers. Start here. | none |
| [basic](./basic/README.md) | Sensor/actuator system split across a client and a server over UDP. | none |
| [multicast](./multicast/README.md) | One publisher to many subscribers over UDP multicast. | none |
| [relay](./relay/README.md) | A gateway relaying topics both ways between UDP and local IPC (Windows named pipe, Linux Unix socket) with `AddRelayTopic`. | none |
| [freertos](./freertos/README.md) | A FreeRTOS server (Win32 simulator, Windows only) and a PC client over UDP. | FreeRTOS |
| [zephyr](./zephyr/README.md) | A Zephyr server (`native_sim`, Linux only) and a PC client over UDP. | Zephyr |
| [interop](./interop/README.md) | A C++ server with Python and C# clients, using MessagePack. | msgpack-c |
| [mqtt-gateway](./mqtt-gateway/README.md) | DataBus topics exposed to MQTT tools as JSON with `JsonTopics` and `MqttBridge`. | Paho MQTT |
| [shapes](./shapes/README.md) | A terminal shapes demo over UDP multicast. | FTXUI |

`build_all.py` builds and runs them all (`freertos` on Windows, `zephyr` on Linux); the test scripts name them `databus/<sample>`.
