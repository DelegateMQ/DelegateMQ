# transport/serializer

A remote delegate sender/receiver app, built for the transport and serializer you choose. A `Sender` calls a remote delegate every 50 ms. DelegateMQ serializes the arguments, sends them over the transport, and a `Receiver` deserializes them and calls its `DataUpdate()`. Both run in one process, each on its own thread, talking through a real transport (sockets, pipe or message queue), not a direct call.

DelegateMQ is transport- and serializer-agnostic: the app (`main.cpp`, `sender.h`, `receiver.h`) is the same for every combination. What changes is in two small folders:

| Folder | Holds |
| :--- | :--- |
| `transport/<name>/transport.h` | The transport class, how each side creates it, and whether the transport uses acknowledgments |
| `serializer/<name>/data.h` (+ `data.cpp`) | The argument classes (`Data`, `DataPoint`, `DataAux`), written for that serializer |

## Configurations

Pick one transport and one serializer at configure time, one build directory per combination:

```bash
cmake -B build-udp-builtin -DSAMPLE_TRANSPORT=udp -DSAMPLE_SERIALIZER=builtin .
cmake --build build-udp-builtin
build-udp-builtin/delegate_app 5        # Windows: build-udp-builtin\Release\delegate_app.exe 5
```

| `SAMPLE_TRANSPORT` | Transport | Platforms | Acknowledgments |
| :--- | :--- | :--- | :--- |
| `udp` (default) | UDP sockets (`LinuxUdpTransport` / `Win32UdpTransport`) | Linux, Windows | Yes |
| `tcp` | TCP sockets (`LinuxTcpTransport` / `Win32TcpTransport`) | Linux, Windows | Yes |
| `pipe` | Windows named pipe (`Win32PipeTransport`) | Windows | No |
| `uds` | Unix domain datagram socket (`LinuxUdsTransport`) | Linux | No |
| `shm` | Shared memory (`ShmRingTransport`: `Win32ShmTransport` / `LinuxShmTransport`) | Linux, Windows | Yes |
| `lwip` | UDP over the lwIP stack's socket API (`ArmLwipUdpTransport`), on lwIP's Unix port and its loopback interface | Linux | Yes |
| `lwip-netconn` | The same over lwIP's netconn API (`ArmLwipNetconnUdpTransport`) | Linux | Yes |
| `zeromq` | ZeroMQ PUB/SUB (`ZeroMqTransport`) | Linux, Windows | No |
| `nng` | NNG PUB/SUB (`NngTransport`) | Linux, Windows | No |

| `SAMPLE_SERIALIZER` | Serializer | Third-party library | Arguments |
| :--- | :--- | :--- | :--- |
| `builtin` (default) | DelegateMQ's `serialize` class (`serialize::I` per class, `data.cpp`) | none | `Data&, DataAux&` |
| `bitsery` | Bitsery | Bitsery (header-only) | `Data&, DataAux&` |
| `cereal` | Cereal binary archive | Cereal (header-only) | `Data&, DataAux&` |
| `msgpack` | MessagePack (`MSGPACK_DEFINE`) | msgpack-c (header-only) | `Data&, DataAux&` |
| `rapidjson` | JSON | RapidJSON (header-only) | `Data&` |

Any transport works with any serializer. The RapidJSON serializer writes one JSON document per message, so its delegate takes one argument (`SAMPLE_AUX` in its `data.h`).

The `udp`, `tcp`, `pipe`, `uds` and `shm` transports with the `builtin` serializer need nothing beyond DelegateMQ. `lwip` and `lwip-netconn` need lwIP, fetched by `scripts/01_fetch_repos.py`: they run the transports an lwIP board uses (socket or netconn API) on lwIP's own UDP/IP code, through lwIP's 127.0.0.1 loopback interface rather than the host network stack. `transport/lwip/lwipopts.h` holds its lwIP options, and `SampleTransportInit()` in `transport/lwip/lwip_init.h` starts lwIP's tcpip thread before the transports are created. ZeroMQ, NNG and the other serializers are fetched and built by `scripts/01_fetch_repos.py` and `02_build_libs.py` (see [BUILD.md](../../../../docs/BUILD.md)).

## Acknowledgments (UDP, TCP, shared memory, lwIP)

With `SAMPLE_ACKS 1` (set in the transport's `transport.h`), the sender wraps the transport in a `ReliableTransport`: a `TransportMonitor` tracks each message until the receiver's transport acknowledges it, and a `RetryMonitor` resends it on timeout (500 ms, 3 retries), reporting `>> TIMEOUT!` through `TransportMonitor::OnSendStatus`. The sender reads acknowledgments after each send (`Receive()` handles an ACK itself). Shared memory doesn't lose messages either, but runs with acknowledgments to show a `TransportMonitor` over it: on a real two-core link they are how one side finds out the other has hung. ZeroMQ and NNG PUB sockets have no return path for an ACK, and a pipe or Unix domain socket is a reliable local link, so those run without.

## Output

```
Data Message: 0 0 Aux Message
Data Message: 5 5 Aux Message
...
Received 59 messages
Acknowledged 59 messages, 0 timeouts
```

The `Acknowledged` line appears only with acknowledgments. `delegate_app [seconds]` runs for the given time (default 5 s) and exits with status 1 if no message arrived intact, or, with acknowledgments, if the sender never got one, so a broken combination fails `build_all.py`.

## In the test suite

`build_all.py` builds and runs these combinations, each in `out/<platform>/example/sample-projects/transport/serializer/<transport>-<serializer>` (and a clang build of each on Linux with `--clang`), from `full_suite.TRANSPORT_COMBOS`:

`udp-builtin`, `tcp-builtin`, `pipe-builtin` (Windows), `uds-builtin` (Linux), `shm-builtin`, `lwip-builtin` and `lwip-netconn-builtin` (Linux), `zeromq-builtin`, `zeromq-bitsery`, `zeromq-cereal`, `zeromq-msgpack`, `zeromq-rapidjson`, `nng-bitsery`.

Apart from the `uds` and `lwip` builds, these are the combinations of the separate samples this one replaced (`linux-tcp-serializer`, `linux-udp-serializer`, `win32-tcp-serializer`, `win32-udp-serializer`, `win32-pipe-serializer`, `zeromq-serializer`, `zeromq-bitsery`, `zeromq-cereal`, `zeromq-msgpack-cpp`, `zeromq-rapidjson`, `nnq-bitsery`). Their folders were removed; to build what one of them built, configure its combination above (e.g. `zeromq-cereal`: `-DSAMPLE_TRANSPORT=zeromq -DSAMPLE_SERIALIZER=cereal`).

Related samples kept separate, because their app differs: `transport/serialport` (two serial ports, given on the command line), `transport/mqtt` (separate publisher and subscriber apps and a broker) and `transport/zephyr-udp` (a Zephyr `native_sim` app).
