# System architecture samples

One client/server system (managers, alarms, sensors and actuators, point-to-point RPC with `RemoteDispatcher`), in three builds.

| Sample | Shows | External libraries |
| :--- | :--- | :--- |
| [no-deps](./no-deps/) | The system over UDP with no third-party library. Start here. | none |
| [full](./full/) | The system over ZeroMQ with MessagePack. | ZeroMQ, msgpack-c |
| [python](./python/) | A Python client for `full`'s server, speaking the same wire format. | pyzmq, msgpack (Python) |

`build_all.py` builds and runs all three (`python` against `full`'s server); the test scripts name them `system-architecture/<sample>`.
