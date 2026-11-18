# DataBus samples

Samples of `dmq::databus::DataBus`, DelegateMQ's topic-based publish/subscribe layer (see [DATABUS.md](../../../docs/DATABUS.md)). Each folder is a separate sample with its own README and CMake build.

| Sample | Shows | External libraries |
| :--- | :--- | :--- |
| [basic](./basic/) | Sensor/actuator system split across a client and a server over UDP. Start here. | none |
| [multicast](./multicast/) | One publisher to many subscribers over UDP multicast. | none |
| [relay](./relay/) | A gateway relaying topics both ways between UDP and local IPC (Windows named pipe, Linux Unix socket) with `AddRelayTopic`. | none |
| [freertos](./freertos/) | A FreeRTOS server (Win32 simulator, Windows only) and a PC client over UDP. | FreeRTOS |
| [zephyr](./zephyr/) | A Zephyr server (`native_sim`, Linux only) and a PC client over UDP. | Zephyr |
| [interop](./interop/) | A C++ server with Python and C# clients, using MessagePack. | msgpack-c |
| [mqtt-gateway](./mqtt-gateway/) | DataBus topics exposed to MQTT tools as JSON with `JsonTopics` and `MqttBridge`. | Paho MQTT |
| [shapes](./shapes/) | A terminal shapes demo over UDP multicast. | FTXUI |

`build_all.py` builds and runs them all (`freertos` on Windows, `zephyr` on Linux); the test scripts name them `databus/<sample>`.
