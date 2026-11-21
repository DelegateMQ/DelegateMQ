# Sensor Node

A sensor node for the Espressif **ESP32-S3** (dual-core Xtensa LX7, Wi-Fi) and a PC client that monitors and commands it over the network. The node's work is split across threads that talk only through DelegateMQ, so none of them shares state or takes a lock.

DelegateMQ is platform independent, so the whole system is built and tested on **Windows and Linux** first, with stand-ins for the board's peripherals. The same application code then runs on the board; only a small platform layer changes. The simple, single-app board demo is [boards/esp32s3-devkitc](../sample-projects/boards/esp32s3-devkitc/README.md).

**Status:** the desktop build runs and passes its self-test, with its threads pinned to cores. The ESP32-S3 port (the ESP-IDF UDP transport, Wi-Fi, the peripherals) is next.

## Architecture

Data flows one way through the threads: `SampleThread` and `ControlThread` produce, `NetThread` publishes to the PC. Commands flow back: `NetThread` receives them, the DataBus hands them to `ControlThread`.

| Thread | Core (ESP32-S3) | Work |
| :--- | :--- | :--- |
| `SampleThread` | 1 | Every 10 ms reads a block of the fast signal (like an ADC filling a DMA buffer), and every 100 ms sends min/max/mean to `NetThread` |
| `ControlThread` | 1 | Once a second: temperature and node status. Owns the LED and the sample rate; handles the PC's commands and the touch pads |
| `NetThread` | 0 | DataBus over UDP: publishes everything to the PC, receives commands. `FullPolicy::DROP`, so a slow network can't hold up the others |

- **Between threads (between cores, on the board):** plain DelegateMQ async calls, e.g. `MakeDelegate(this, &SensorNode::PublishStats, m_netThread)(stats)`. On the ESP32-S3 both cores share memory under one FreeRTOS kernel, so this needs no transport.
- **Timers:** each thread's periodic work is a `dmq::util::Timer` connected with `MakeTimerDelegate`, so a slow tick never queues up behind itself.
- **Interrupt:** touch changes are posted from the interrupt (on the desktop, a stand-in thread) to an `IsrDelegate`, which delivers them to `ControlThread`.
- **Network:** commands arrive on `NetThread`; the DataBus delivers each to `ControlThread` (`DataBus::Subscribe(topic, func, &m_controlThread)`). Data from the node and commands from the PC are ACKed and retried (`ReliableTransport`).
- **Cores:** `SampleThread` and `ControlThread` are pinned to core 1 and `NetThread` to core 0 (`Thread::SetCoreAffinity()`, set through `NodeConfig`). The desktop build pins them the same way when the machine has at least two cores.
- **Latency:** at startup `ControlThread` times 200 async-wait calls to `NetThread` (the other core) and 200 to `SampleThread` (the same core). The node prints min/mean/max; `NodeStatus` carries the means, and the client shows them.
- **Watchdogs:** every thread runs under a 2 s watchdog.

## Topics

| Topic | Direction | Message | When |
| :--- | :--- | :--- | :--- |
| `sensor/signal/stats` | node → PC | `SignalStats`: rate, sample count, min, max, mean | every 100 ms |
| `sensor/temp` | node → PC | `Temperature` | every second |
| `sensor/touch` | node → PC | `Touch`: pad, touched or released | on change |
| `node/status` | node → PC | `NodeStatus`: uptime, rate, LED colour, commands applied, messages dropped, cross-core and same-core round trip (µs) | every second, and after each command |
| `cmd/rate` | PC → node | `SetRate`: 100-10000 Hz, a multiple of 100 | |
| `cmd/led` | PC → node | `SetLed`: r, g, b | |

Messages use DelegateMQ's built-in serializer (`common/SensorMessages.h`). The node sends to the PC on UDP port 9100; the PC sends commands to the node on port 9101.

## Folders

| Folder | Contents |
| :--- | :--- |
| `common/` | Topics, remote IDs, ports and messages, shared by the node and the PC |
| `node/app/` | The node's application, platform independent (`SensorNode`), and the hardware interfaces it uses (`Hal.h`) |
| `node/platform/desktop/` | Desktop stand-ins for the hardware (`DesktopHal.h`) and `main()` |
| `node/platform/esp32s3/` | The ESP32-S3 build (in progress) |
| `pc-client/` | The PC client and its self-test |

## Desktop Stand-ins

| Board | Desktop |
| :--- | :--- |
| ADC samples | A 10 Hz sine of amplitude 1: each 100 ms window holds exactly one period, so the client can check min, max and mean |
| Internal temperature sensor | 24 °C, drifting slowly |
| RGB LED | A line on the console |
| Touch pad interrupt | A thread that touches and releases pad 0 every 700 ms, posting through the same `IsrDelegate` |

## Build and Run

```bash
cd example/sensor-node
cmake -B build .
cmake --build build --config Release

# Two terminals (Windows: build\Release\sensor_node.exe ...)
./build/sensor_node                  # the node; Ctrl+C to stop
./build/sensor_pc                    # the PC client: self-test, then the node's data
```

Arguments: `sensor_node [seconds] [pc-address]` and `sensor_pc [seconds] [node-address]`. `seconds` = 0 (the default) runs until Ctrl+C. The addresses default to `127.0.0.1`; on two machines, give each the other's address.

## Self-Test

`sensor_pc` checks, in order:

1. Stats, temperature and status arrive; a stats window holds one period of the test sine (count = rate / 10, min < -0.9, max > 0.9, |mean| < 0.05); the temperature is in range.
2. A `cmd/rate` command is applied: the node reports the new rate and its windows are taken at it.
3. A `cmd/led` command is applied: the node reports the new colour.
4. Touch changes arrive, the node counted both commands, its network thread dropped nothing, and it measured its cross-core and same-core round trips.

On a desktop OS the round trips (typically a few µs to a few tens of µs) depend on the scheduler and vary from run to run; the board's numbers are the meaningful ones.

It prints `ALL TESTS PASSED` and returns 0. The node prints `NODE CHECKS PASSED` when it published stats and temperatures and dropped nothing. `build_all.py` builds both and runs them together for 10 seconds over loopback UDP, on Windows and Linux.

## A Note on Polling the Sockets

`NetThread` polls its sockets from a 10 ms timer tick, and also publishes. A UDP SUB socket's default receive timeout (2 s) suits a thread that only receives; here it would stall `NetThread` for 2 s whenever no command is waiting, past its watchdog. The node sets the command socket's timeout to 1 ms (`SetRecvTimeout()`), so a poll returns at once.
