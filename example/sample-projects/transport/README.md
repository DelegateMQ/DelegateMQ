# Transport samples

Remote delegates: a delegate call is serialized, sent over a transport, and invoked in another process or processor. DelegateMQ doesn't tie a transport to a serializer, so any transport works with any serializer.

| Sample | Shows | External libraries |
| :--- | :--- | :--- |
| [serializer](./serializer/) | One sender/receiver app, built for the transport (`udp`, `tcp`, `pipe`, `uds`, `lwip`, `lwip-netconn`, `zeromq`, `nng`) and serializer (`builtin`, `bitsery`, `cereal`, `msgpack`, `rapidjson`) chosen at configure time. Start here. | none for UDP, TCP or pipe with the built-in serializer |
| [in-process](./in-process/) | The smallest remote delegate: both ends in one process, the "transport" a shared `std::stringstream`. | none |
| [serialport](./serialport/) | A sender and a receiver on two serial ports. | libserialport |
| [mqtt](./mqtt/) | Publisher and subscriber apps through an MQTT broker, with RapidJSON. | Paho MQTT, RapidJSON |
| [zephyr-udp](./zephyr-udp/) | The serializer sample's UDP app on Zephyr `native_sim` (Linux only). | Zephyr |
| [netx-udp](./netx-udp/) | The serializer sample's app over NetX Duo UDP on ThreadX's Linux simulator (Linux only). | ThreadX, NetX Duo |

Publish/subscribe across nodes, built on these transports, is in [../databus](../databus/). `build_all.py` builds and runs these samples; the test scripts name them `transport/<sample>`.
