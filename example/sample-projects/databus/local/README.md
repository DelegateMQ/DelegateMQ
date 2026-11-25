# DataBus Local (Single-Process)

`dmq::databus::DataBus` inside one process: **no transport or serializer between the components, no external libraries**. Start here to see how threads decouple through typed topics before adding a network or IPC link. (With `DMQ_DATABUS_TOOLS`, on by default, `SpyBridge` also sends a stringified copy of every message to `dmq-spy` over UDP; that's optional diagnostics, not how the components talk. See below.)

## What It Shows

Three active objects (`Sensor`, `Controller`, `Logger`) each own a `dmq::os::Thread` and never hold references to one another:

1. **Last-Value Cache (LVC) and `lifespan`:** `config/setpoint` is published before `Controller` exists; `Controller` subscribes with `QoS::lastValueCache = true` and receives the cached setpoint immediately on `ControlThread`. A second cached topic (`config/calibration`), 60 ms old, is delivered to a late subscriber that accepts values up to 5 s old and skipped by one whose `lifespan` is 20 ms.
2. **Asynchronous vs. synchronous delivery:** `Sensor` publishes `SensorReading` every 80 ms from `SensorThread`. `Controller` receives every reading asynchronously on `ControlThread` and publishes `ControlCmd`. `Logger` subscribes to `ControlCmd` synchronously (`thread = nullptr`), running inline on `ControlThread`.
3. **Rate limiting (`QoS::minSeparation`):** `Logger` subscribes to `sensor/temp` on `LoggerThread` with `minSeparation = 250 ms`, throttling high-rate samples while `Controller` still receives every one.
4. **Content filtering (`SubscribeFilter`):** An overheat alarm subscribes to `sensor/temp` with a predicate (`tempC > 28.0f`), firing only on out-of-range samples.
5. **Deadline monitoring (`DeadlineSubscription`):** `Controller` starts its 350 ms deadline when `Sensor` starts publishing (`DeadlineSubscription` arms its timer when created). After 12 samples, `Sensor` stops; the deadline expires on `ControlThread` and `Controller` publishes `HeaterMode::SafeShutdown` once per silence, carrying the last reading.
6. **Bus diagnostics and `dmq-spy`:** `SubscribeUnhandled` catches a publish to `diagnostics/orphan`, `SubscribeError` watches for bus errors, and `RegisterStringifier` formats local messages for `DataBus::Monitor` and `SpyBridge` (no serializers needed). `SubscribeUnhandled` is connected after the LVC topics are seeded: publishing a cached topic before anyone subscribes also counts as unhandled, though the value is kept for late subscribers.

All message structs in [Topics.h](./Topics.h) are fixed-width, trivially copyable structs, so if `Sensor` is later split into a separate process or onto a second MCU core, its topics can cross that link with `dmq::serialization::raw::Serializer` (raw byte copies, for peers that share an ABI) without changing publish/subscribe code.

## Build and Run

```bash
cmake -B build .
cmake --build build --config Release

# Linux
./build/delegate_databus_local

# Windows
.\build\Release\delegate_databus_local.exe
```

To build without `SpyBridge` / `dmq-spy` UDP forwarding:

```bash
cmake -B build -DDMQ_DATABUS_TOOLS=OFF .
cmake --build build --config Release
```

## Watching Traffic in `dmq-spy`

When built with `DMQ_DATABUS_TOOLS=ON` (the default on Windows/Linux), the app starts `SpyBridge`, which sends a stringified copy of every message over UDP to `127.0.0.1:9999`. Start `dmq-spy` first, then run `delegate_databus_local`: all local topics appear with their values under node `LocalNode`. This is the sample's only network traffic, and only for diagnostics.
