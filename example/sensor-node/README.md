# Sensor Node

A sensor node for the Espressif **ESP32-S3** (dual-core Xtensa LX7, Wi-Fi) and a PC client that monitors and commands it over the network. The node's work is split across threads that talk only through DelegateMQ, so none of them shares state or takes a lock.

DelegateMQ is platform independent, so the whole system was built and tested on **Windows and Linux** first, with stand-ins for the board's peripherals. The same application code (`node/app`) runs on the board; only a small platform layer changes. The simple, single-app board demo is [boards/esp32s3-devkitc](../sample-projects/boards/esp32s3-devkitc/README.md).

**Status:** runs on the desktop (`build_all.py` runs it on Windows and Linux) and on an ESP32-S3-DevKitC-1 N8R8 over Wi-Fi, where the PC client's self-test passes. On the board the fast signal is still the generated test sine; the ADC is next.

## Architecture

Data flows one way through the threads: `SampleThread` and `ControlThread` produce, `NetThread` publishes to the PC. Commands flow back: `NetThread` receives them, the DataBus hands them to `ControlThread`.

| Thread | Core (ESP32-S3) | Work |
| :--- | :--- | :--- |
| `SampleThread` | 1 | Every 10 ms reads a block of the fast signal (like an ADC filling a DMA buffer); at the end of each window (100 ms unless the PC asks otherwise) sends min/max/mean to `NetThread` |
| `ControlThread` | 1 | Once a second: temperature and node status. Owns the LED, the sample rate and the stats window; applies the PC's commands; receives the touch interrupt's events |
| `NetThread` | 0 | DataBus over UDP: publishes to the PC while it's listening, receives commands. `FullPolicy::DROP`, so a slow network can't hold up the others |

- **Between threads (between cores, on the board):** plain DelegateMQ async calls, e.g. `MakeDelegate(this, &SensorNode::PublishStats, m_netThread)(stats)`. On the ESP32-S3 both cores share memory under one FreeRTOS kernel, so this needs no transport.
- **Timers:** each thread's periodic work is a `dmq::util::Timer` connected with `MakeTimerDelegate`, so a slow tick never queues up behind itself.
- **Interrupt:** touch changes are posted from the interrupt (on the desktop, a stand-in thread) to an `IsrDelegate`, which delivers them to `ControlThread`.
- **Network:** commands arrive on `NetThread`; the DataBus delivers each to the thread that owns what it changes (`DataBus::Subscribe(topic, func, &m_controlThread)`).
- **Cores:** `SampleThread` and `ControlThread` are pinned to core 1 and `NetThread` to core 0 (`Thread::SetCoreAffinity()`, set through `NodeConfig`). On the board, core 0 also runs ESP-IDF's Wi-Fi and TCP/IP tasks. The desktop build pins the threads the same way when the machine has at least two cores.
- **Latency:** at startup `ControlThread` times 200 async-wait calls to `NetThread` (the other core) and 200 to `SampleThread` (the same core). The node prints min/mean/max; `NodeStatus` carries the means, and the client shows them.
- **Watchdogs:** every thread runs under a 2 s watchdog.
- **One program for every platform:** `RunNode()` (`node/app/NodeMain.cpp`) starts the node and runs the platform loop: `Timer::ProcessTimers()` and `IsrDelegates::ProcessAll()` every millisecond, the watchdog check every 100 ms, a summary every 10 s. It uses DelegateMQ's clock and `ThisThread::sleep_for()`, so each platform's `main()` only creates its hardware, brings up its network and calls it.

## Protocol

### Start: the Node Publishes Only While a PC Listens

The PC client sends `cmd/start` every second. The node publishes only while they keep coming, and stops 5 s (`SensorNode::PC_TIMEOUT`) after the last one: a lease. A powered node with no client sends nothing, and a client that exits or crashes doesn't leave it sending. (Without this, a board left running kept publishing to the PC and disturbed the desktop test on that PC.)

The Start also says how often to publish statistics: `intervalMs`, the stats window, 100 to 10000 ms in steps of 100 (whole 10 ms ticks, and whole periods of the 10 Hz test signal). Temperature and status stay at once a second.

### Commands Are Reliable

Every command (`cmd/start`, `cmd/rate`, `cmd/led`) is ACKed by the node and, if no ACK comes within 250 ms, sent again, up to 3 times (`ReliableTransport` with a `RetryMonitor`). Each carries a `seq` from one counter in the client; a retry repeats it. The node applies a rate or LED command only if its `seq` differs from the last one it applied for that command, so a retry after a lost ACK isn't applied twice. (A repeated Start just renews the lease.) The client starts its counter from the clock, so a restarted client doesn't repeat the node's last `seq`.

The node's data is ACKed but not retried: the next window replaces a lost one.

### Topics

| Topic | Direction | Message | When |
| :--- | :--- | :--- | :--- |
| `sensor/signal/stats` | node → PC | `SignalStats`: rate, sample count, min, max, mean | every window (`intervalMs`) |
| `sensor/temp` | node → PC | `Temperature` | every second |
| `sensor/touch` | node → PC | `Touch`: pad, touched or released | on change |
| `node/status` | node → PC | `NodeStatus`: uptime, rate, stats window, LED colour, commands applied, messages dropped, cross-core and same-core round trip (µs) | every second, and after each command |
| `cmd/start` | PC → node | `StartPublishing`: seq, intervalMs | every second |
| `cmd/rate` | PC → node | `SetRate`: seq, 100-10000 Hz (a multiple of 100) | |
| `cmd/led` | PC → node | `SetLed`: seq, r, g, b | |

Messages use DelegateMQ's built-in serializer (`common/SensorMessages.h`). The node sends to the PC on UDP port 9100; the PC sends commands to the node on port 9101.

## Folders

| Folder | Contents |
| :--- | :--- |
| `common/` | Topics, remote IDs, ports and messages, shared by the node and the PC |
| `node/app/` | The node's application, platform independent: `SensorNode`, `RunNode()`, the hardware interfaces (`Hal.h`) and the generated test signal (`TestSignal.h`) |
| `node/platform/desktop/` | Desktop stand-ins for the hardware (`DesktopHal.h`) and `main()` |
| `node/platform/esp32s3/` | The ESP32-S3 build: an ESP-IDF project ([README](node/platform/esp32s3/README.md)) |
| `pc-client/` | The PC client and its self-test |

## Hardware on Each Platform

| Node needs | ESP32-S3-DevKitC-1 | Desktop |
| :--- | :--- | :--- |
| Fast signal | The generated test sine for now (the ADC is next) | A 10 Hz sine of amplitude 1: each 100 ms holds exactly one period, so the client can check min, max and mean |
| Temperature | The chip's internal sensor (the die, a few °C above the room) | 24 °C, drifting slowly |
| LED | The RGB LED (WS2812, GPIO 38) | A line on the console |
| Touch | The BOOT button's GPIO interrupt (pad 0) | A thread that touches and releases pad 0 every 700 ms, posting through the same `IsrDelegate` |

## Build and Run on the Desktop

```bash
cd example/sensor-node
cmake -B build .
cmake --build build --config Release

# Two terminals (Windows: build\Release\sensor_node.exe ...)
./build/sensor_node                  # the node; Ctrl+C to stop
./build/sensor_pc                    # the PC client: self-test, then the node's data
```

Arguments: `sensor_node [seconds] [pc-address]` and `sensor_pc [seconds] [node-address]`. `seconds` = 0 (the default) runs until Ctrl+C. The addresses default to `127.0.0.1`; on two machines, give each the other's address.

For the board, see [node/platform/esp32s3](node/platform/esp32s3/README.md); the PC side is the same `sensor_pc`, given the board's address.

## Self-Test

`sensor_pc` checks, in order:

1. Stats, temperature and status arrive (the first Start starts them); a stats window holds one period of the test sine (count = rate / 10, min < -0.9, max > 0.9, |mean| < 0.05); the temperature is in range.
2. A `cmd/rate` command is applied: the node reports the new rate and its windows are taken at it.
3. A `cmd/led` command is applied: the node reports the new colour. The client then sends the same command with the same `seq`, as a retry would.
4. The Start's `intervalMs` set to 200: the node reports it, and its windows hold 200 ms of samples (two periods of the sine).
5. Touch changes arrive (on the board: press BOOT within 30 s). The node applied the rate and LED commands once each (the repeated `seq` was ignored), its network thread dropped nothing, and it measured its cross-core and same-core round trips.

It prints `ALL TESTS PASSED` and returns 0. The node prints `NODE CHECKS PASSED` when it published stats and temperatures and dropped nothing. `build_all.py` builds both and runs them together for 10 seconds over loopback UDP, on Windows and Linux.

On a desktop OS the round trips (typically a few µs to a few tens of µs) depend on the scheduler and vary from run to run. On the board they are about 200 µs, both across cores and within one: under investigation.

## A Note on Polling the Sockets

`NetThread` polls its sockets from a 10 ms timer tick, and also publishes. A UDP SUB socket's default receive timeout (2 s) suits a thread that only receives; here it would stall `NetThread` for 2 s whenever no command is waiting, past its watchdog. The PUB socket, which receives the PC's ACKs, defaults to 2 ms on the desktop but 50 ms on lwIP. The node sets both to 1 ms (`SetRecvTimeout()`), so a poll returns at once.
