# DataBus

`dmq::databus::DataBus` is a high-level middleware built on top of DelegateMQ's core delegates. It provides a topic-based data distribution system (DDS Lite) that works across threads and remote nodes with full location transparency.

---

## Table of Contents

- [DataBus](#databus)
  - [Table of Contents](#table-of-contents)
  - [Core Concepts](#core-concepts)
  - [Design Philosophy](#design-philosophy)
  - [Pub/Sub vs. RPC](#pubsub-vs-rpc)
  - [Features](#features)
  - [Threading Model](#threading-model)
  - [Thread Priority and Dispatch Latency](#thread-priority-and-dispatch-latency)
    - [Subscriber Thread Priority](#subscriber-thread-priority)
    - [Dispatch Latency — Local Inter-Thread](#dispatch-latency--local-inter-thread)
    - [Polling Thread Priority — Remote Distribution](#polling-thread-priority--remote-distribution)
    - [Polling Frequency and Transport Timeout](#polling-frequency-and-transport-timeout)
    - [Dispatch Latency Breakdown — Remote Distribution](#dispatch-latency-breakdown--remote-distribution)
    - [Thread Queue Full Policy](#thread-queue-full-policy)
  - [Ordering and Guarantees](#ordering-and-guarantees)
    - [Plain Async Delegates and Signals](#plain-async-delegates-and-signals)
    - [DataBus Ordering (Multi-Publisher)](#databus-ordering-multi-publisher)
    - [Transport Impact](#transport-impact)
    - [Stale vs. Sequence Requirements](#stale-vs-sequence-requirements)
    - [Why ordering is not automatic](#why-ordering-is-not-automatic)
  - [DataBus Spy](#databus-spy)
  - [Quality of Service (QoS)](#quality-of-service-qos)
    - [Last Value Cache (LVC)](#last-value-cache-lvc)
    - [Lifespan](#lifespan)
    - [Min Separation](#min-separation)
  - [Deadline Monitoring — `dmq::databus::DeadlineSubscription<T>`](#deadline-monitoring--dmqdatabusdeadlinesubscriptiont)
    - [Behaviour](#behaviour)
    - [`dmq::util::Timer::ProcessTimers()` requirement](#dmqutiltimerprocesstimers-requirement)
    - [Composing with QoS](#composing-with-qos)
  - [Example: Local Pub/Sub](#example-local-pubsub)
  - [Example: Last Value Cache (LVC)](#example-last-value-cache-lvc)
  - [Remote Distribution](#remote-distribution)
    - [Setup Checklist — Silent Failure Hazards](#setup-checklist--silent-failure-hazards)
    - [Relay Loop Hazard](#relay-loop-hazard)
    - [Unicast (Point-to-Point)](#unicast-point-to-point)
    - [Multicast (One-to-Many)](#multicast-one-to-many)
    - [Duplicate Packet Protection](#duplicate-packet-protection)
    - [Comparison Table](#comparison-table)
  - [Example: Remote Distribution](#example-remote-distribution)
  - [Example: Multi-Node Topology](#example-multi-node-topology)
    - [CPU-A Setup](#cpu-a-setup)
    - [CPU-B Setup (Bridge Node)](#cpu-b-setup-bridge-node)
    - [MCU-C / MCU-D Setup](#mcu-c--mcu-d-setup)
    - [Reliability Summary for This Topology](#reliability-summary-for-this-topology)

---

## Core Concepts

- **Topic**: A unique string identifier for a data stream (e.g., `"sensor/battery_voltage"`).
- **Participant**: Represents a node in the network. Manages remote topic mappings and transport integration. See `dmq::databus::Participant`.
- **QoS (Quality of Service)**: Per-subscription configuration for topic behavior — see [Quality of Service (QoS)](#quality-of-service-qos). See `dmq::databus::QoS`.
- **Data Model**: Each topic carries exactly one typed value — `void(T)`. A topic represents the *current state* of something; a publish is one snapshot of that state. All fields travel inside `T`.

---

## Design Philosophy

The `dmq::databus::DataBus` is built on a **"zero-cost abstraction"** and **"zero-internal threads"** philosophy. This makes it suitable for everything from high-performance desktop applications to resource-constrained bare-metal microcontrollers.

1. **Zero Internal Threads**: Unlike standard DDS implementations that may spawn 10–15 internal threads for discovery and maintenance, the DataBus spawns **none**. All work (serialization, delivery, polling) happens on application-provided threads. This gives the developer absolute control over CPU utilization and priority management.
2. **Zero-Cost Safety**: Utilities like `dmq::util::MonotonicGuard` are intentionally non-thread-safe. By design, DelegateMQ encourages assigning subscribers to specific threads (Active Objects). Because the subscriber lambda is guaranteed to run on a single thread, the guard does not need internal mutexes. You only "pay" for synchronization at the application level if you choose a multi-threaded design.
3. **Optional Metadata**: The DataBus does not force an internal header or timestamp on your data. While this means the application is responsible for adding fields like `uint64_t timestamp` if ordering is required, it ensures that low-bandwidth transports (like CAN or slow Serial) are not burdened by "middleware bloat."
4. **Data-Centricity**: The bus is focused on the **State** of the system. Each topic represents a single piece of information. By decoupling the *producer* of data from the *consumer*, the system remains modular and easy to test.

---

## Pub/Sub vs. RPC

`dmq::databus::DataBus` follows the **pub/sub (data-centric)** paradigm inherited from DDS: each topic has exactly one associated C++ type `T`, and every `Publish`/`Subscribe` call carries a single `const T&` argument. This is not a limitation of the underlying delegate machinery — it is a deliberate modeling constraint. Real DDS systems enforce the same rule: `DataWriter::write(const T& data)` takes one sample because a topic is a named data stream, not a function call. Pack everything into `T`:

```cpp
struct MotorState { float rpm; float torque; uint32_t timestamp_ms; };
dmq::databus::DataBus::Publish<MotorState>("motor/state", { 3000.0f, 12.5f, now });
```

When you need **RPC semantics** — calling a remote function with multiple distinct arguments, or receiving a return value — use `dmq::RemoteChannel` directly without going through `dmq::databus::DataBus`:

```cpp
// RPC: arbitrary signature, no topic string, point-to-point
dmq::RemoteChannel<void(int, float, std::string)> channel(transport, serializer);
channel(42, 3.14f, "config");   // all 3 args serialized and sent
```

| | `dmq::databus::DataBus` (Pub/Sub) | `dmq::RemoteChannel` direct (RPC) |
|:---|:---|:---|
| Paradigm | Data-centric (DDS model) | Call-centric |
| Arguments | One typed value `void(T)` | Arbitrary `RetType(A, B, C, ...)` |
| Addressing | Topic string | Remote ID |
| Subscribers | Many (fan-out via `dmq::Signal`) | One target function |
| Return value | None | Supported (via `RemoteInvokeWait`) |
| Use case | State, telemetry, events | Commands, configuration, queries |

---

## Features

1. **Location Transparency**: Components subscribe to topics by name. They do not need to know if the publisher is on the same thread, a different thread, or a completely different machine. This is the defining characteristic that separates `dmq::databus::DataBus` from both raw signal/slot libraries (local-only) and DDS (remote-oriented, impractical for inter-thread use due to 10–15 threads of overhead). The same `Publish`/`Subscribe` API covers all three cases; adding a `dmq::databus::Participant` extends an existing local bus to the network without changing any publisher or subscriber code.
2. **Dynamic Discovery (Manual)**: While not fully automatic like industrial DDS, `dmq::databus::DataBus` allows adding `dmq::databus::Participant` objects at runtime to bridge local buses across a network.
3. **Type Safety**: Runtime checks ensure that if two components use the same topic name, they must use the same data type; otherwise, a fault is triggered.
4. **Spy/Monitor Support**: Call `dmq::databus::DataBus::Monitor()` to receive a callback for every single message published on the bus. This is the foundation for the [DelegateMQ Tools](../tools/TOOLS.md) diagnostic consoles. The callback receives a `dmq::databus::SpyPacket` containing:
    - **topic**: The unique string name of the data topic.
    - **value**: A stringified version of the data (provided by user-registered stringifiers).
    - **timestamp_us**: A high-resolution timestamp (microseconds since epoch) taken when the publisher called `dmq::databus::DataBus::Publish`.
5. **Duplicate Protection**: `dmq::databus::Participant` automatically filters out redundant network packets (retries) using a history of recently seen sequence numbers, ensuring that redundant messages are not re-processed by the application logic.

---

## Threading Model

The `dmq::databus::DataBus` library itself has **no internal threads**. All threading responsibility belongs to the application.

**Publish** (`dmq::databus::DataBus::Publish`) is synchronous. It runs on the calling thread and delivers to all local subscribers before returning. If a subscriber was registered with a worker thread argument, the delivery is an async delegate post and returns immediately; otherwise the subscriber's callback runs inline on the publisher's thread.

**Receive** (incoming network data) requires the application to call `dmq::databus::Participant::ProcessIncoming()` in a polling loop. A typical pattern is one dedicated background thread that polls all participants:

```cpp
std::thread receiveThread([&]() {
    while (running) {
        commandParticipant->ProcessIncoming();  // blocks briefly on transport receive
        dataParticipant->ProcessIncoming();
        monitor.Process();                      // drive ACK/retry timeouts
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
});
```

`ProcessIncoming()` blocks inside the transport's `Receive()` call (typically 10–100 ms depending on the transport timeout). When a message arrives, it deserializes and invokes the registered handler **synchronously on the polling thread**, which may then re-publish to the local `dmq::databus::DataBus` to fan out to other local subscribers.

`dmq::databus::Participant` protects its internal channel map with a `dmq::RecursiveMutex`. The handler callback is intentionally invoked **outside** that lock to prevent deadlock if the handler itself calls back into the same `dmq::databus::Participant`. `ProcessIncoming()` is therefore safe to call from a single dedicated thread while the main thread calls `Publish`, `Subscribe`, or `AddParticipant` concurrently. Calling `ProcessIncoming()` simultaneously from multiple threads on the same `dmq::databus::Participant` is not safe; transport thread-safety is transport-implementation dependent.

**Contrast with NetworkMgr**: `dmq::util::NetworkEngine` (used by the `system-architecture` samples) creates two threads internally — a dedicated receive thread and a marshal/dispatch thread — so the application never calls `ProcessIncoming()`. `dmq::databus::DataBus` trades that automation for explicit control, making it a better fit for embedded or RTOS environments where every thread must be accounted for.

---

## Thread Priority and Dispatch Latency

### Subscriber Thread Priority

When a subscriber registers with a worker thread, the callback is posted to that thread's message queue as an async delegate. The subscriber thread priority therefore determines how quickly the callback executes after `Publish()` is called — regardless of whether the publish originated locally or arrived over the network.

- Set the subscriber thread priority to match the urgency of the data. High-frequency telemetry and low-priority status updates can share a lower-priority worker thread; alarm or safety-critical callbacks warrant a dedicated high-priority thread.
- On RTOS targets with a fixed priority budget, assign subscriber thread priorities in relation to the other application threads that produce and consume the same data.
- Synchronous subscribers (no thread argument) execute inline on the publishing thread and inherit its priority — no separate priority decision is needed, but the publisher blocks until all synchronous subscribers return.

### Dispatch Latency — Local Inter-Thread

For local pub/sub (no network, no `dmq::databus::Participant`), `dmq::databus::DataBus::Publish()` fires a `dmq::Signal` synchronously. The end-to-end latency has two components:

```
local latency = Signal fire on publisher thread  (~microseconds)
              + subscriber thread wake-up         (OS/RTOS scheduler dependent)
```

The signal fire itself is negligible. The dominant term is the subscriber thread wake-up time, which depends on the OS scheduler quantum and the subscriber thread's priority relative to other runnable threads.

### Polling Thread Priority — Remote Distribution

When a `dmq::databus::Participant` is added, a dedicated polling thread calls `ProcessIncoming()` in a loop. This thread's priority controls how quickly an arriving network packet becomes a `dmq::databus::DataBus::Publish()` call. There are then two separate priority decisions:

| Stage | Controlled by |
|:---|:---|
| Network bytes → `Publish()` | Polling thread priority |
| `Publish()` → subscriber callback | Subscriber's worker thread priority |

- Set the polling thread priority high enough to drain the transport receive buffer before it overflows — typically at or near the priority of the threads that consume the arriving data.
- For alarm or safety-critical messages, consider a dedicated high-priority polling thread rather than sharing one thread across all participants.
- On RTOS targets, a typical placement is one or two priority levels above the lowest-priority application thread.

### Polling Frequency and Transport Timeout

`ProcessIncoming()` blocks inside the transport's `Receive()` call for up to the configured transport timeout. The `sleep_for` in the typical pattern adds additional idle time *after* the timeout:

```
worst-case receive latency = transport timeout + sleep duration
```

- **Minimize latency**: remove the `sleep_for` entirely and let the transport timeout alone control the polling rate. For most transports a 10–50 ms timeout is a good balance.
- **Yield CPU on RTOS/embedded**: keep a short `sleep_for` (1–10 ms) if the polling thread must share CPU time with other tasks. Setting the transport timeout to 5 ms and adding a 5 ms sleep yields the CPU while bounding latency to ~10 ms.
- **Busy-wait (zero latency)**: set the transport timeout to 0 and remove the sleep. Minimizes latency to near zero but consumes a full CPU core — only appropriate on a dedicated core or for benchmarking.

### Dispatch Latency Breakdown — Remote Distribution

Total end-to-end latency from a remote sender to the local subscriber callback has four components:

```
remote latency = network transit
               + transport receive blocking  (0 → timeout + sleep, worst case)
               + dmq::databus::DataBus::Publish() on polling thread  (~microseconds)
               + subscriber thread wake-up   (if async — OS/RTOS scheduler dependent)
```

The dominant terms are network transit and receive blocking. Once `Publish()` is called on the polling thread the remaining cost is the same as the local inter-thread case above.

### Thread Queue Full Policy

When a subscriber is registered with a worker thread, `dmq::databus::DataBus::Publish()` posts a message to that thread's internal queue. If a slow subscriber falls behind a fast publisher the queue grows. `dmq::os::Thread` provides a configurable `FullPolicy` to control what happens when the queue reaches its limit (`maxQueueSize`):

| Policy | Behavior when full | Publisher stalled? |
|:---|:---|:---|
| `dmq::os::FullPolicy::FAULT` (default) | `DispatchDelegate()` triggers a system fault and terminates the application | No (Crash) |
| `dmq::os::FullPolicy::DROP` | `DispatchDelegate()` discards the message and returns immediately | No |
| `dmq::os::FullPolicy::TIMEOUT` | `DispatchDelegate()` waits up to `dispatchTimeout` (default 2 s), logs a warning, then drops | Yes (briefly) |

**Choosing a policy**

Use `FAULT` (the default) for safety-critical systems where an overfilled queue represents a fundamental design failure or a violation of real-time constraints. It ensures that the system does not continue running in an unpredictable state if data is being lost.

Use `DROP` for topics where losing an occasional sample is acceptable and stalling the publisher is not. High-rate sensor telemetry, display updates, and best-effort multicast data are natural fits — a missed reading is superseded by the next one.

Use `TIMEOUT` for topics where every message should be delivered if at all possible, but unbounded stalling is not acceptable. The publisher waits up to `dispatchTimeout` (default `dmq::DEFAULT_DISPATCH_TIMEOUT` = 2 s) for the consumer to drain a slot, then logs a warning and drops the message. Commands, configuration updates, and state transitions are natural fits — the 2 s window absorbs transient bursts while protecting the system from indefinite stalls.

```cpp
// Sensor thread: drop stale readings rather than stall the publisher
dmq::os::Thread sensorThread("SensorThread", /*maxQueueSize=*/10, dmq::os::FullPolicy::DROP);
sensorThread.CreateThread();

// Command thread: wait up to 2 s for the consumer before logging and dropping
dmq::os::Thread cmdThread("CmdThread", /*maxQueueSize=*/50, dmq::os::FullPolicy::TIMEOUT);
cmdThread.CreateThread();

// Custom timeout: wait up to 500 ms instead of the default 2 s
dmq::os::Thread fastCmdThread("FastCmdThread", /*maxQueueSize=*/50,
    dmq::os::FullPolicy::TIMEOUT, std::chrono::milliseconds(500));
fastCmdThread.CreateThread();

auto connSensor = dmq::databus::DataBus::Subscribe<ImuData>("sensor/imu",
    [](const ImuData& d) { /* update display */ },
    &sensorThread);

auto connCmd = dmq::databus::DataBus::Subscribe<ActuatorCmd>("cmd/actuator",
    [](const ActuatorCmd& cmd) { /* apply command */ },
    &cmdThread);
```

**Trade-offs**

- `FullPolicy` is a thread-level setting, not per-subscription. All subscribers on the same `dmq::os::Thread` instance share the same policy. If a single thread serves both drop-tolerant and drop-intolerant topics, split them across two threads.
- With `TIMEOUT`, a backed-up subscriber thread stalls the publishing thread for up to `dispatchTimeout`. Choose a timeout that is shorter than the system watchdog timeout so that a stalled consumer is detected and reported rather than silently waiting forever.
- With `DROP`, a fast publisher and slow subscriber can result in the subscriber seeing only a fraction of publishes. Pair `DROP` with `minSeparation` QoS on the subscriber to proactively rate-limit delivery before the queue ever fills — this keeps delivery uniform rather than bursty-then-silent.
- Setting `maxQueueSize = 0` disables the limit entirely; `FullPolicy` has no effect and all messages are queued regardless of consumer speed.

---

## Ordering and Guarantees

Understanding the ordering guarantees of DelegateMQ is critical for safety-related logic (e.g., Alarms). Guarantees vary depending on the feature being used.

### Plain Async Delegates and Signals

Plain `dmq::Signal` and asynchronous delegates (outside the DataBus) provide **strict ordering** for a single producer-consumer pair:

- **Async Delegates**: Messages are pushed directly to a locked OS queue. If you call `delegate(SET)` then `delegate(CLEAR)`, they are guaranteed to be queued and executed in that order.
- **Signals**: `dmq::Signal` (and `dmq::MulticastDelegateSafe`) perform dispatch inside an internal lock. This ensures that the entire set of subscribers is notified atomically relative to other publishers.

### DataBus Ordering (Multi-Publisher)

The `dmq::databus::DataBus` prioritizes deadlock prevention and concurrency, which introduces a narrow reordering risk when **multiple threads publish to the same topic**:

1. **The Multi-Publisher Race**: The DataBus updates its internal state (LVC) under a lock, but releases that lock before firing the local signal. If Thread A (SET) and Thread B (CLEAR) publish simultaneously, it is possible for the final LVC state to be CLEAR while the last signal delivered was SET.
2. **The LVC Race**: When a new subscriber joins, it receives the cached LVC value. If a live `Publish()` occurs at the exact same instant, the subscriber's worker thread may receive the fresh "live" value followed by the slightly older "cached" value.

**Mitigation Strategies:**
- **Single Publisher Thread**: Ensure only one thread in the system publishes to a specific topic (e.g., an "Alarm Manager" thread).
- **Monotonic Data**: Include a `uint64_t timestamp` or sequence number in your data struct. The receiver should ignore any update with a timestamp older than the one it last processed.
  - **Single Thread Publisher**: A simple `uint32_t m_counter++` is sufficient.
  - **Multi-Thread Publisher**: Use `std::atomic<uint32_t>` to ensure a perfectly increasing sequence across threads on the same node.
  - **Cross-Node Publishers**: Atomics do not work across different CPUs. Use timestamps (`dmq::Clock`) for "good enough" ordering, or designate a single node as the master publisher for that topic.

### Transport Impact

The underlying network transport significantly affects ordering for **Remote Distribution**:

- **UDP / Serial**: These are unordered. If the Reliability Layer retransmits a lost packet, it may arrive at the receiver *after* subsequent packets. **Application logic must be robust against out-of-order remote updates.**
- **TCP / ZeroMQ**: These protocols provide stream-level ordering. Switching to `dmq::transport::Win32TcpTransport` or `dmq::transport::ZeroMqTransport` ensures that remote messages arrive and are processed in the order they were sent.

### Stale vs. Sequence Requirements

When choosing a transport and ordering strategy, distinguish between two different application requirements:

1. **State Consistency (Discarding Stale Data)**: Most pub/sub data represents the "Current State" (e.g., Temperature, Speed). If updates 1, 3, 2 arrive, it is safe to process 1, process 3, and discard 2 as "stale" because 3 is the newer truth. **UDP + `dmq::util::MonotonicGuard`** is the ideal high-performance solution for this requirement.
2. **Step Consistency (Preserving Every Step)**: Some data represents a required sequence of operations (e.g., Unlock Door → Open Door). If update 2 is discarded because 3 arrived first, the sequence is broken and the operation may fail. If your application cannot tolerate skipping any message in a stream, you must use an ordered transport like **TCP** or **ZeroMQ**.

### Why ordering is not automatic

High-end DDS implementations often handle message ordering and "late-arriver" filtering automatically in the middleware. DelegateMQ leaves this responsibility to the application for several reasons:

1. **Memory**: Storing a "last timestamp" or "highest sequence number" for every topic on every subscriber would consume significant RAM in a header-only library designed for small embedded systems.
2. **Logic**: Not all data needs strict ordering. Some telemetry (like "Total Energy Used") is fine to process out of order, whereas command state (like "Stop Motor") is critical. Application-level handling allows for surgical application of safety checks only where needed.
3. **Duplicate Protection**: DelegateMQ's `dmq::databus::Participant` **already handles duplicate filtering** automatically at the network layer. Since duplicates are removed before reaching the subscriber, the application only needs to perform a simple "Greater Than" check on a timestamp or counter to ensure strict ordering.

---

## DataBus Spy

**[DelegateMQ Tools](../tools/TOOLS.md)** provides standalone TUI diagnostic consoles (`dmq-spy`, `dmq-monitor`) for the DataBus. `dmq-spy` acts as a "Software Logic Analyzer" — it captures, filters, and displays all bus traffic in real-time without blocking the main application thread.

To enable spy support in a sample project, build with `-DDMQ_DATABUS_TOOLS=ON`. This activates an asynchronous "Spy Bridge" that exports packets over UDP to the standalone console.

---

## Quality of Service (QoS)

QoS options are set per-subscription via the `dmq::databus::QoS` struct and passed as the last argument to `dmq::databus::DataBus::Subscribe`. Options compose freely — any combination of the fields below is valid.

```cpp
dmq::databus::QoS qos;
qos.lastValueCache = true;
qos.lifespan       = std::chrono::seconds(5);
qos.minSeparation  = std::chrono::milliseconds(50);

auto conn = dmq::databus::DataBus::Subscribe<SensorData>("sensor/temp", handler, &thread, qos);
```

### Last Value Cache (LVC)

LVC is a QoS feature that ensures new subscribers receive the most recent published value immediately upon subscribing, rather than waiting for the next publisher update.

- **Cache depth**: exactly one value per topic. The `dmq::databus::DataBus` stores only the latest snapshot.
- **On subscription**: if a cached value exists, it is dispatched to the new subscriber immediately — synchronously, or on the specified worker thread.
- **On publication**: every `Publish` call overwrites the cached value.
- **Memory**: the `dmq::databus::DataBus` only caches values for topics where at least one subscriber has requested LVC (`lastValueCache = true`).

```cpp
dmq::databus::QoS qos;
qos.lastValueCache = true;

auto conn = dmq::databus::DataBus::Subscribe<float>("sensor/temp", [](float v) {
    std::cout << "Initial or updated temp: " << v << "\n";
}, nullptr, qos);
// Subscriber immediately receives the last published value, if any.
```

### Lifespan

Lifespan caps the age of a cached LVC value. If the cached value is older than `lifespan` when a new subscriber connects, it is treated as stale and not delivered. Only meaningful when `lastValueCache = true`.

```cpp
dmq::databus::QoS qos;
qos.lastValueCache = true;
qos.lifespan = std::chrono::seconds(5);

auto conn = dmq::databus::DataBus::Subscribe<float>("sensor/temp", handler, nullptr, qos);
// LVC delivery is skipped if the last publish was more than 5 seconds ago.
```

Each subscriber sets its own lifespan independently. A subscriber that needs fresh data can set a short lifespan; another that tolerates older data can set a longer one or omit lifespan entirely — the cache itself is unaffected.

### Min Separation

Min separation rate-limits delivery to a subscriber. If a new publish arrives before `minSeparation` has elapsed since the last delivery to this subscriber, it is silently dropped for that subscriber only. Other subscribers on the same topic are unaffected.

```cpp
dmq::databus::QoS qos;
qos.minSeparation = std::chrono::milliseconds(50); // at most 20 Hz

auto conn = dmq::databus::DataBus::Subscribe<ImuData>("sensor/imu", handler, &thread, qos);
// A publisher running at 1 kHz delivers to this subscriber at ~20 Hz.
// All other subscribers receive at the full 1 kHz.
```

Min separation and LVC compose: the LVC delivery on subscription counts as the first delivery. Rapid publishes immediately after a new subscriber connects are throttled by the same interval.

---

## Deadline Monitoring — `dmq::databus::DeadlineSubscription<T>`

Deadline monitoring detects a publisher that has gone silent. It is implemented as a client-side helper class rather than a built-in QoS field, because it requires an active timer and the callback dispatch context is application-specific.

`dmq::databus::DeadlineSubscription<T>` wraps a `dmq::databus::DataBus::Subscribe` call with a `dmq::util::Timer`. Every incoming message resets the timer. If the timer fires before the next message arrives, the `onMissed` callback is invoked. Both callbacks are dispatched to the same optional worker thread.

```cpp
dmq::databus::DeadlineSubscription<SensorData> m_watch{
    "sensor/temp",
    std::chrono::milliseconds(500),          // deadline window
    [](const SensorData& d) { /* handle */ },
    []() { /* publisher has gone silent */ },
    &m_workerThread                          // optional: both callbacks on this thread
};
```

The object is non-copyable and non-movable. Declare it as a class member or use `unique_ptr<dmq::databus::DeadlineSubscription<T>>` for heap allocation. When it is destroyed, the `dmq::databus::DataBus` connection and the timer are both cleaned up automatically — no dangling callbacks.

### Behaviour

- **Timer arms at construction** — `onMissed` fires if no publish arrives within the deadline, even before the first message ever appears on the topic.
- **Each delivery resets the window** — the timer is restarted on every message, so `onMissed` only fires after a genuine gap.
- **Recovery is automatic** — once publishes resume, the window resets and no further misses are reported until the next gap.
- **Thread dispatch** — with a thread argument, both the data callback and `onMissed` are dispatched to that thread. Without one, the data callback fires on the publisher's thread and `onMissed` fires on the `dmq::util::Timer::ProcessTimers()` thread.

### `dmq::util::Timer::ProcessTimers()` requirement

The deadline fires only when `dmq::util::Timer::ProcessTimers()` is called. On platforms where a `dmq::os::Thread` with a watchdog is already running, this is driven automatically. On bare-metal targets, call `ProcessTimers()` from the main super-loop or a SysTick handler. If `ProcessTimers()` is never called, `onMissed` silently never fires.

On bare-metal with no thread argument, `onMissed` may execute in an ISR context — keep it short and non-blocking, or use a flag that is processed in the main loop.

### Composing with QoS

`dmq::databus::DeadlineSubscription` and `dmq::databus::QoS` fields are independent. The deadline window monitors the gap between deliveries; `minSeparation` controls the maximum delivery rate. They can coexist — a subscriber that throttles high-frequency data to 20 Hz while also expecting at least one delivery every 500 ms would use both:

```cpp
// Subscribe with rate limiting via QoS
dmq::databus::QoS qos;
qos.minSeparation = std::chrono::milliseconds(50); // at most 20 Hz

auto conn = dmq::databus::DataBus::Subscribe<ImuData>("sensor/imu", handler, &thread, qos);

// Separately monitor that the topic doesn't go silent for more than 500 ms
dmq::databus::DeadlineSubscription<ImuData> m_watch{
    "sensor/imu",
    std::chrono::milliseconds(500),
    [](const ImuData&) {},  // no-op data handler — monitoring only
    []() { /* sensor silent */ },
    &m_workerThread
};
```

---

## Example: Local Pub/Sub

```cpp
// 1. Subscriber A (Synchronous)
auto conn1 = dmq::databus::DataBus::Subscribe<int>("system/status", [](int status) {
    // Process status...
});

// 2. Subscriber B (Asynchronous on workerThread)
auto conn2 = dmq::databus::DataBus::Subscribe<int>("system/status", [](int status) {
    // Process status on workerThread context...
}, &workerThread);

// 3. Publisher
dmq::databus::DataBus::Publish<int>("system/status", 1);
```

---

## Example: Last Value Cache (LVC)

LVC ensures that new subscribers get the "latest and greatest" data immediately upon subscription, even if the publisher hasn't sent an update recently.

```cpp
dmq::databus::QoS qos;
qos.lastValueCache = true;

// This subscriber will immediately receive the last value published to "sensor/temp"
auto conn = dmq::databus::DataBus::Subscribe<float>("sensor/temp", [](float v) {
    std::cout << "Initial or updated temp: " << v << "\n";
}, nullptr, qos);
```

---

## Remote Distribution

The `dmq::databus::DataBus` supports two primary patterns for network distribution: **Unicast** and **Multicast**.

### Setup Checklist — Silent Failure Hazards

Remote distribution requires several manual wiring calls. Each omission silently disables delivery with no compile-time or runtime error. This is the most common source of "why isn't my topic arriving?" bugs.

**Outgoing (this node → remote node)** — all three calls are required:

| Step | Call | Omission effect |
|:---|:---|:---|
| 1 | `participant->AddRemoteTopic(topic, remoteId)` | Topic never serialized or sent — `Publish()` succeeds locally only |
| 2 | `DataBus::AddParticipant(participant)` | Participant never sees `Publish()` events |
| 3 | `DataBus::RegisterSerializer<T>(topic, serializer)` | Topic found but serialization fails silently at runtime |

**Incoming (remote node → this node)** — choose one of two combined calls:

| Step | Call | Behavior |
|:---|:---|:---|
| 1a | `DataBus::AddIncomingTopic<T>(topic, remoteId, participant, serializer)` | Delivers to local subscribers and LVC only — does **not** forward to remote participants. Use for subscriber nodes. |
| 1b | `DataBus::AddRelayTopic<T>(topic, remoteId, participant, serializer)` | Delivers locally **and** forwards to all registered remote participants. Use only on explicit bridge/relay nodes. |

> **Tip:** If a topic is silently dead, verify the checklist above in order. Step 1 (`AddRemoteTopic`) is the most commonly missed because it lives on the `Participant` object rather than on `DataBus` itself.

### Relay Loop Hazard

A **relay loop** occurs when the same topic is registered as both incoming and outgoing on a node, and the incoming path triggers re-broadcast to remote participants. Each node bounces the message to its peers indefinitely.

```
Safety publishes FAULT ──▶ GUI receives FAULT
                               │
                               ▼ (re-broadcasts via outgoing)
Safety receives FAULT ◀── GUI sends FAULT back
    │
    ▼ (re-broadcasts via outgoing)
GUI receives FAULT ◀── Safety sends FAULT back
    ...  (infinite, ~network round-trip interval)
```

**Root cause:** `DataBus::Publish` triggers both local subscribers and all registered remote participants. If a message arrives from the network and is re-published with `Publish`, any outgoing serializer on the same topic sends it back out — creating a loop.

**Prevention:** `AddIncomingTopic` uses `DataBus::PublishLocal` internally. This dispatches to local subscribers, the spy, and the LVC, but skips remote participants — breaking the loop. Only use `AddRelayTopic` on nodes whose **sole purpose** is forwarding (bridge nodes with no loop path back to the originator).

**Danger pattern** — both sides have the same topic as incoming AND outgoing:
```cpp
// Node A and Node B — both do this for "sys/fault":
DataBus::AddIncomingTopic<FaultMsg>("sys/fault", ...);       // receives from peers
DataBus::RegisterOutgoingTopic<FaultMsg>("sys/fault", ...);  // sends to peers
// Result: every fault bounces A→B→A→B→... indefinitely
```

**Safe pattern** — use `AddIncomingTopic` (local only) for subscriber nodes:
```cpp
// Each node that consumes but does not need to relay:
DataBus::AddIncomingTopic<FaultMsg>("sys/fault", ...);  // local delivery only, no loop
```

**Safe pattern** — bridge node with no loop path:
```cpp
// CPU-B: receives sensor data from MCU-C, forwards to CPU-A.
// CPU-A never sends sensor data back, so there is no loop.
DataBus::AddRelayTopic<TempData>("sensor/temp", ...);  // local + forward to CPU-A
```

### Unicast (Point-to-Point)
- **Transport**: `dmq::transport::Win32UdpTransport`, `dmq::transport::Win32TcpTransport`, `dmq::transport::ZeroMqTransport`, etc.
- **Behavior**: Data is sent to a specific IP address.
- **Reliability**: Fully supported via the Reliability Layer (ACKs/Retries). The publisher knows if the specific remote node received the message.
- **Use Case**: Critical commands, configuration updates, or small networks where specific node delivery confirmation is required.

### Multicast (One-to-Many)
- **Transport**: `dmq::transport::MulticastTransport`.
- **Behavior**: Data is published to a multicast group (e.g., `239.1.1.1`). Any number of clients can "join" the group to receive the data stream simultaneously.
- **Reliability**: "Best Effort." There are no individual ACKs from subscribers. If a packet is lost, it is not retransmitted.
- **Use Case**: High-frequency sensor data, telemetry, discovery, and the [DelegateMQ Spy](#databus-spy) tool. Multicast is extremely efficient as the network hardware handles cloning the packets for all subscribers.

### Duplicate Packet Protection

The `dmq::databus::DataBus` (via the `dmq::databus::Participant` class) provides built-in protection against duplicate network packets. This is critical for reliable topics where a sender might re-transmit a packet if an Acknowledgment (ACK) was lost or delayed.

- **Mechanism**: Each `dmq::databus::Participant` maintains a circular history buffer of the last several sequence numbers received for each `Remote ID`.
- **Filtering**: When a packet arrives, its sequence number is checked against the history. If it matches a recently seen number, the packet is identified as a duplicate and is silently discarded.
- **Ordered/Unordered Transports**: This approach protects against duplicates on all transport types. Because it uses a history set rather than a simple "last seen" counter, it correctly handles out-of-order packets on unordered transports like UDP (e.g., if packet #10 arrives before packet #9, both are still accepted exactly once).
- **Semantics**: Combined with the Reliability Layer, this ensures "Execute-Once" semantics for critical system commands.

### Comparison Table

| Feature | Unicast | Multicast |
| :--- | :--- | :--- |
| **Connectivity** | 1-to-1 | 1-to-Many |
| **Network Traffic** | Increases with each subscriber | Constant (one packet for all) |
| **Reliability** | Guaranteed (with ACKs) | Best Effort |
| **Overhead** | Higher (Tracking state) | Lower (Fire-and-forget) |

---

## Example: Remote Distribution

To send data across the network, register a serializer and a participant.

```cpp
// 1. Setup transport and participant
dmq::transport::Win32UdpTransport transport;
auto remoteNode = std::make_shared<dmq::databus::Participant>(transport);
remoteNode->AddRemoteTopic("telemetry/data", 100); // Map topic to remote ID 100

// 2. Register with the bus
dmq::databus::DataBus::AddParticipant(remoteNode);
dmq::databus::DataBus::RegisterSerializer<MyData>("telemetry/data", mySerializer);

// 3. Publish normally
// The bus handles local distribution AND remote sending via the participant
dmq::databus::DataBus::Publish<MyData>("telemetry/data", someData);
```

---

## Example: Multi-Node Topology

This example uses a realistic mixed-platform hierarchy spanning three tiers.

| Node | Platform | DelegateMQ OS Port | Transport |
|:---|:---|:---|:---|
| CPU-A | Linux | `port/os/stdlib` | `port/transport/linux-udp` |
| CPU-B | FreeRTOS + lwIP | `port/os/freertos` | `port/transport/arm-lwip-udp` (UDP), `port/transport/stm32-uart` (serial) |
| MCU-C, MCU-D | Bare metal (no OS) | `port/os/bare-metal` (clock only) | Custom `dmq::transport::ITransport` over UART |

The `dmq::databus::DataBus::Publish`, `dmq::databus::DataBus::Subscribe`, and `AddIncomingTopic` calls are identical on all three tiers. Bridge nodes (CPU-B) use `AddRelayTopic` instead of `AddIncomingTopic` so that incoming data is forwarded to remote participants on the outgoing leg. The only other differences are the port headers included and, on bare metal, the absence of an RTOS thread. Platform-specific notes appear in each subsection below.

This example shows a fully bidirectional three-node system. CPU-A sends actuator commands down to MCU-C and MCU-D via CPU-B. The MCUs publish sensor data back up through CPU-B to CPU-A. CPU-B acts as a bridge between the Ethernet and serial transports in both directions.

```
                              Ethernet                    Serial
  CPU-A ─────────────────────────────────── CPU-B ──────────────── MCU-C
                                              │
                                              └────────────────── MCU-D

  CPU-A ──▶ CPU-B ──▶ MCU-C, MCU-D  :  cmd/actuator  (unicast,   reliable)
  MCU-C ──▶ CPU-B ──▶ CPU-A         :  sensor/temp   (serial then multicast)
  MCU-D ──▶ CPU-B ──▶ CPU-A         :  sensor/temp   (serial then multicast)
```

**Topic assignments:**

| Topic | Path | Transport | Reliability |
|:---|:---|:---|:---|
| `cmd/actuator` | CPU-A → CPU-B | Unicast UDP | ACK + retry (requires `dmq::util::ReliableTransport`) |
| `cmd/actuator` | CPU-B → MCU-C, MCU-D | Serial (unicast) | ACK + retry (requires `dmq::util::ReliableTransport`) |
| `sensor/temp` | MCU-C, MCU-D → CPU-B | Serial (unicast) | ACK + retry (requires `dmq::util::ReliableTransport`) |
| `sensor/temp` | CPU-B → CPU-A | Multicast UDP | Best effort, no callback |

> **Note:** ACK + retry on UDP and serial legs requires wrapping the transport with `dmq::util::ReliableTransport` and `dmq::util::RetryMonitor`. The code snippets below omit this wiring for clarity. See the `system-architecture` sample project for a complete reliability setup.

---

### CPU-A Setup

**Platform: Linux.** Include `port/os/stdlib/Thread.h` and `port/transport/linux-udp/LinuxUdpTransport.h`. `dmq::os::Thread` maps to `std::thread`. No platform-specific changes to any of the DataBus calls below.

CPU-A sends actuator commands via unicast (reliable, ACK expected) and subscribes to sensor data arriving from CPU-B via multicast (best effort). The transport choice at setup time is the reliability contract — the `Publish` and `Subscribe` calls themselves are identical regardless of transport.

```cpp
// --- Unicast: actuator commands to CPU-B ---
dmq::transport::LinuxUdpTransport cmdTransport;
cmdTransport.Create(dmq::transport::LinuxUdpTransport::Type::PUB, "192.168.1.2", 5000);
auto toB = std::make_shared<dmq::databus::Participant>(cmdTransport);
toB->AddRemoteTopic("cmd/actuator", ids::CMD_ACTUATOR_ID);
dmq::databus::DataBus::AddParticipant(toB);
dmq::databus::DataBus::RegisterSerializer<ActuatorCmd>("cmd/actuator", cmdSerializer);

// --- Failure notification (unicast only) ---
// Failure notification requires dmq::util::ReliableTransport + dmq::util::RetryMonitor wiring and
// is surfaced via dmq::util::TransportMonitor::OnSendStatus (Status::TIMEOUT) or by
// subclassing dmq::util::NetworkEngine and overriding OnStatus(). Multicast never
// generates failure callbacks. See the system-architecture sample for full wiring.

// --- Multicast receive: sensor data forwarded by CPU-B ---
dmq::transport::MulticastTransport sensorRecv;
sensorRecv.Create(dmq::transport::MulticastTransport::Type::SUB, "239.1.1.1", 5001);
auto fromB = std::make_shared<dmq::databus::Participant>(sensorRecv);
dmq::databus::DataBus::AddIncomingTopic<TempData>(
    "sensor/temp", ids::SENSOR_TEMP_ID, *fromB, sensorSerializer);

dmq::databus::DataBus::Subscribe<TempData>("sensor/temp", [](const TempData& t) {
    // process aggregated sensor data from MCU-C and MCU-D
}, &workerThread);

// --- Publish and polling ---
dmq::databus::DataBus::Publish<ActuatorCmd>("cmd/actuator", cmd);  // unicast, ACK expected

// dmq::os::Thread is the DelegateMQ abstraction — maps to stdlib, FreeRTOS, RTX, etc.
// Priority can be set before or after CreateThread().
dmq::os::Thread m_pollSensor("PollSensor");
m_pollSensor.CreateThread();
dmq::MakeDelegate(this, &CpuA::PollSensor, m_pollSensor).AsyncInvoke();

// void CpuA::PollSensor() { while (running) fromB->ProcessIncoming(); }
```

---

### CPU-B Setup (Bridge Node)

**Platform: FreeRTOS + lwIP.** Include `port/os/freertos/Thread.h` and `port/transport/arm-lwip-udp/ArmLwipUdpTransport.h`. The lwIP transport exposes the same `UdpTransport` class name and `Create(Type, addr, port)` signature as the Linux version — no code changes to the snippets below. `dmq::os::Thread` maps to a FreeRTOS task created via `xTaskCreate`; priority can be set per-thread with `SetThreadPriority()`. For the serial legs, include `port/transport/stm32-uart/Stm32UartTransport.h`, which uses interrupt-driven ring buffering and a binary semaphore so the FreeRTOS task sleeps until data arrives.

> **`Stm32UartTransport` single-channel constraint.** The current implementation routes all UART ISR bytes through a single global pointer (`g_uartTransportInstance`). Each `Create()` call overwrites that pointer, so only the last transport to call `Create()` receives ISR bytes. For a two-MCU bridge you need two separate globals and two separate `HAL_UART_RxCpltCallback` dispatchers — one per UART peripheral. The snippets below assume that extension is in place. Alternatively, a single-MCU variant of CPU-B (one serial link) avoids the issue entirely.

CPU-B is purely a bridge — it does not originate or consume any topics itself. It receives on three incoming transports (one Ethernet, two serial) and forwards in both directions. `AddRelayTopic` handles the re-publish in one line per topic per transport; the full `Publish` path is used so the data is forwarded to all registered remote participants on the outgoing leg. The serializer can differ per leg; CPU-B always works with plain C++ objects between the two legs. Each polling thread is a named `dmq::os::Thread` instance whose priority can be set independently — for example, the serial MCU threads could be raised above the Ethernet thread if sensor latency is more critical than command latency on a given target.

```cpp
// ── Inbound from CPU-A: actuator commands ──────────────────────────────
dmq::transport::ArmLwipUdpTransport cmdRecv;
cmdRecv.Create(dmq::transport::ArmLwipUdpTransport::Type::SUB, "0.0.0.0", 5000);
auto fromA = std::make_shared<dmq::databus::Participant>(cmdRecv);
dmq::databus::DataBus::AddRelayTopic<ActuatorCmd>(
    "cmd/actuator", ids::CMD_ACTUATOR_ID, *fromA, cmdEthSerializer);

// ── Serial links to MCU-C and MCU-D (one transport per physical UART) ─────
// dmq::transport::SerialTransport (or Stm32UartTransport) is full-duplex: one instance owns the UART handle and
// handles both Send() (HAL_UART_Transmit) and Receive() (ISR ring buffer).
// Splitting one UART into separate send/receive objects would create two
// competing owners of the same hardware — use one instance per physical link.
dmq::transport::SerialTransport serialC;   // full-duplex link to MCU-C
dmq::transport::SerialTransport serialD;   // full-duplex link to MCU-D

// Outbound: forward actuator commands to each MCU
auto toMcuC = std::make_shared<dmq::databus::Participant>(serialC);
auto toMcuD = std::make_shared<dmq::databus::Participant>(serialD);
toMcuC->AddRemoteTopic("cmd/actuator", ids::MCU_CMD_ID);
toMcuD->AddRemoteTopic("cmd/actuator", ids::MCU_CMD_ID);
dmq::databus::DataBus::AddParticipant(toMcuC);
dmq::databus::DataBus::AddParticipant(toMcuD);
dmq::databus::DataBus::RegisterSerializer<ActuatorCmd>("cmd/actuator", cmdSerialSerializer);

// Inbound: sensor data from each MCU — same transport object, separate dmq::databus::Participant
auto fromMcuC = std::make_shared<dmq::databus::Participant>(serialC);
auto fromMcuD = std::make_shared<dmq::databus::Participant>(serialD);
dmq::databus::DataBus::AddRelayTopic<TempData>(
    "sensor/temp", ids::MCU_TEMP_ID, *fromMcuC, sensorSerialSerializer);
dmq::databus::DataBus::AddRelayTopic<TempData>(
    "sensor/temp", ids::MCU_TEMP_ID, *fromMcuD, sensorSerialSerializer);

// ── Outbound to CPU-A: forward sensor data via multicast ───────────────
dmq::transport::MulticastTransport sensorMulticast;
sensorMulticast.Create(dmq::transport::MulticastTransport::Type::PUB, "239.1.1.1", 5001);
auto toA = std::make_shared<dmq::databus::Participant>(sensorMulticast);
toA->AddRemoteTopic("sensor/temp", ids::SENSOR_TEMP_ID);
dmq::databus::DataBus::AddParticipant(toA);
dmq::databus::DataBus::RegisterSerializer<TempData>("sensor/temp", sensorEthSerializer);

// ── Polling threads: one per incoming transport (three total) ──────────
// Named dmq::os::Thread instances — priority settable per thread, portable to RTOS.
dmq::os::Thread m_pollFromA("PollFromA");
dmq::os::Thread m_pollFromC("PollFromMcuC");
dmq::os::Thread m_pollFromD("PollFromMcuD");
m_pollFromA.CreateThread();
m_pollFromC.CreateThread();
m_pollFromD.CreateThread();
dmq::MakeDelegate(this, &Bridge::PollFromA,    m_pollFromA).AsyncInvoke();
dmq::MakeDelegate(this, &Bridge::PollFromMcuC, m_pollFromC).AsyncInvoke();
dmq::MakeDelegate(this, &Bridge::PollFromMcuD, m_pollFromD).AsyncInvoke();

// void Bridge::PollFromA()    { while (running) fromA->ProcessIncoming(); }
// void Bridge::PollFromMcuC() { while (running) fromMcuC->ProcessIncoming(); }
// void Bridge::PollFromMcuD() { while (running) fromMcuD->ProcessIncoming(); }
```

When `cmd/actuator` arrives from CPU-A, `AddRelayTopic` re-publishes it via `Publish` and the bus fans it out to both `toMcuC` and `toMcuD` automatically. When `sensor/temp` arrives from either MCU, it is likewise relayed via `Publish` and forwarded to CPU-A via multicast — both MCUs feed the same topic, so CPU-A receives a single stream of temperature readings.

---

### MCU-C / MCU-D Setup

**Platform: Bare metal (no OS).** There is no RTOS, so there are no `dmq::os::Thread` instances and no blocking primitives. Two things change from the FreeRTOS nodes above:

1. **No polling thread.** `ProcessIncoming()` is called directly from the main super-loop (or from a timer/DMA callback). There is no `dmq::os::Thread::CreateThread()` or `AsyncInvoke()`.
2. **Synchronous dispatch.** `dmq::databus::DataBus::Subscribe` is called *without* a thread argument. When `ProcessIncoming()` delivers a message, the subscriber callback fires synchronously in the same call stack — no queue, no context switch.

The serial transport on bare metal is a custom `dmq::transport::ITransport` implementation that polls or interrupt-drives your MCU's UART peripheral directly. `BareMetalClock.h` (`port/os/bare-metal/BareMetalClock.h`) provides the timing source, fed by your SysTick handler incrementing `g_ticks`. The `dmq::databus::DataBus::Publish` and `AddIncomingTopic` calls are unchanged.

The MCUs see only their serial link to CPU-B. They subscribe to actuator commands and publish sensor data. They have no knowledge of the Ethernet layer or of each other.

```cpp
// On MCU-C (and identically on MCU-D) — bare metal, no RTOS

// BareMetalUartTransport is your dmq::transport::ITransport implementation over the MCU's
// UART peripheral. dmq::transport::DmqHeader framing and CRC16 are the only requirements.
BareMetalUartTransport serial;

// One dmq::databus::Participant handles both directions on the full-duplex serial port.
// AddIncomingTopic registers the receive handler; AddRemoteTopic + AddParticipant
// register the send side — both on the same object.
auto cpuBLink = std::make_shared<dmq::databus::Participant>(serial);

// ── Inbound: actuator commands from CPU-B ──────────────────────────────
dmq::databus::DataBus::AddIncomingTopic<ActuatorCmd>(
    "cmd/actuator", ids::MCU_CMD_ID, *cpuBLink, cmdSerialSerializer);

// No thread argument — callback fires synchronously inside ProcessIncoming()
dmq::databus::DataBus::Subscribe<ActuatorCmd>("cmd/actuator", [](const ActuatorCmd& cmd) {
    // apply actuator command
});

// ── Outbound: sensor data to CPU-B ─────────────────────────────────────
cpuBLink->AddRemoteTopic("sensor/temp", ids::MCU_TEMP_ID);
dmq::databus::DataBus::AddParticipant(cpuBLink);
dmq::databus::DataBus::RegisterSerializer<TempData>("sensor/temp", sensorSerialSerializer);

// Publish sensor reading (triggered by timer or hardware event)
dmq::databus::DataBus::Publish<TempData>("sensor/temp", reading);

// ── Super-loop polling (no dmq::os::Thread, no RTOS) ────────────────────────────
// In main():
while (true)
{
    cpuBLink->ProcessIncoming();  // deliver any waiting inbound messages
    // ... other bare-metal tasks
}
```

---

### Reliability Summary for This Topology

| Leg | Topic | Reliable | Failure Notification |
|:---|:---|:---|:---|
| CPU-A → CPU-B (Ethernet unicast) | `cmd/actuator` | Yes (with `dmq::util::ReliableTransport`) | Via `dmq::util::TransportMonitor::OnSendStatus` or `dmq::util::NetworkEngine::OnStatus()` override |
| CPU-B → MCU-C, MCU-D (Serial) | `cmd/actuator` | Yes (with `dmq::util::ReliableTransport`) | Via `dmq::util::TransportMonitor::OnSendStatus` |
| MCU-C, MCU-D → CPU-B (Serial) | `sensor/temp` | Yes (with `dmq::util::ReliableTransport`) | Via `dmq::util::TransportMonitor::OnSendStatus` |
| CPU-B → CPU-A (Multicast) | `sensor/temp` | No | No — next reading follows shortly |

The multicast leg from CPU-B to CPU-A is intentionally best effort. Sensor data is high-frequency and a lost packet is simply superseded by the next sample. Actuator commands travel the full path reliably with failure notification at each hop — but only when `dmq::util::ReliableTransport` and `dmq::util::RetryMonitor` are wired in. See the `system-architecture` sample project for the complete setup.
