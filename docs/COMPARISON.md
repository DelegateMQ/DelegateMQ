# DelegateMQ — Technology Comparison

This document compares DelegateMQ to related technologies across three areas: **signal/slot libraries**, **remote/IPC communication**, and **asynchronous callback patterns**. The goal is not to declare a winner — each technology has its place — but to identify where DelegateMQ offers a meaningfully different or simpler approach.

---

## Table of Contents

- [DelegateMQ — Technology Comparison](#delegatemq--technology-comparison)
  - [Table of Contents](#table-of-contents)
  - [Signal / Slot Libraries](#signal--slot-libraries)
    - [Qt Signals \& Slots](#qt-signals--slots)
    - [Boost.Signals2](#boostsignals2)
    - [sigslot / nano-signal-slot](#sigslot--nano-signal-slot)
    - [Signal/Slot Summary](#signalslot-summary)
  - [Remote / IPC Communication](#remote--ipc-communication)
    - [DataBus (DDS Lite)](#databus-dds-lite)
    - [DDS (Data Distribution Service)](#dds-data-distribution-service)
    - [gRPC](#grpc)
    - [Raw ZeroMQ / NNG](#raw-zeromq--nng)
    - [Remote Summary](#remote-summary)
  - [Asynchronous Callbacks and Thread Dispatch](#asynchronous-callbacks-and-thread-dispatch)
    - [std::async / std::future](#stdasync--stdfuture)
    - [OS Message Queues (FreeRTOS, Win32, POSIX)](#os-message-queues-freertos-win32-posix)
    - [Boost.Asio / Executors](#boostasio--executors)
    - [Async Summary](#async-summary)
  - [Unified API: The Core Differentiator](#unified-api-the-core-differentiator)

---

## Signal / Slot Libraries

### Qt Signals & Slots

Qt's signal/slot mechanism is mature and widely used, but it carries significant constraints outside of Qt applications.

**Qt requires:**
- All signal/slot classes to inherit `QObject`
- The `moc` (Meta-Object Compiler) pre-processing step to generate connection glue code
- Linking the Qt runtime

**Thread dispatch in Qt** uses `Qt::QueuedConnection`, which routes the call through the event loop of the receiving thread. The receiving object must be "owned" by the target thread (`moveToThread()`), and the thread must be running a Qt event loop.

```cpp
// Qt: class must inherit QObject; moc generates the connection glue
class Sensor : public QObject {
    Q_OBJECT
signals:
    void dataReady(int value);
};

class Display : public QObject {
    Q_OBJECT
public slots:
    void onData(int value) { ... }
};

// Connection — Qt::QueuedConnection for cross-thread
connect(&sensor, &Sensor::dataReady, &display, &Display::onData, Qt::QueuedConnection);
```

**DelegateMQ equivalent** — no base class, no codegen, no event loop required:

```cpp
class Sensor {
public:
    dmq::Signal<void(int)> OnData;  // plain member
};

class Display {
public:
    Display(Sensor& s) {
        m_conn = s.OnData.Connect(dmq::MakeDelegate(this, &Display::OnData, m_thread));
    }
private:
    void OnData(int value) { ... }
    dmq::os::Thread m_thread;
    dmq::ScopedConnection m_conn;
};
```

**Key differences:**

| | Qt | DelegateMQ |
|---|---|---|
| Base class required | Yes (`QObject`) | No |
| Code generation step | Yes (`moc`) | No |
| Thread dispatch mechanism | Qt event loop (`moveToThread`) | Any `dmq::IThread` — RTOS, stdlib, bare-metal |
| Usable without Qt runtime | No | Yes |
| RAII connection handle | `QMetaObject::Connection` (manual `disconnect`) | `dmq::ScopedConnection` (auto on scope exit) |
| Embedded / RTOS use | No | Yes |

---

### Boost.Signals2

Boost.Signals2 is a well-engineered, header-only signal library with thread-safe connection management. Its primary limitation for embedded or cross-thread use is that **slots are always called on the emitting thread**.

```cpp
boost::signals2::signal<void(int)> sig;
boost::signals2::scoped_connection conn = sig.connect([](int v){ ... });
sig(42);  // slot called synchronously on the emitting thread — no dispatch
```

To call a slot on a different thread with Boost.Signals2, you must manually enqueue the call yourself:

```cpp
// Manual thread dispatch — not provided by Boost.Signals2
sig.connect([&queue](int v) {
    queue.push(v);          // push to thread-safe queue
    cv.notify_one();        // wake the worker thread
});
// Worker thread then pops and calls the real handler
```

**DelegateMQ** delivers the slot directly to the subscriber's thread via `dmq::MakeDelegate(..., thread)`:

```cpp
// Slot executed on m_thread, not on the emitting thread
m_conn = signal.Connect(dmq::MakeDelegate(this, &MyClass::OnEvent, m_thread));
```

**Key differences:**

| | Boost.Signals2 | DelegateMQ |
|---|---|---|
| Thread-safe connect/disconnect | Yes | Yes |
| RAII connection handle | `scoped_connection` | `dmq::ScopedConnection` |
| Slot called on subscriber's thread | No — manual dispatch required | Yes — specify thread at connect time |
| Blocking async slot (wait for result) | No | Yes (`dmq::DelegateAsyncWait`) |
| Embedded / no-STL-heap use | No | Yes (fixed-block allocator) |
| Header-only | Yes | Yes |

---

### sigslot / nano-signal-slot

Lightweight single-header signal libraries (e.g., `sigslot`, `nano-signal-slot`, `sgnl`) focus on minimal overhead with no external dependencies. They are synchronous-only and have no concept of thread dispatch.

```cpp
// sigslot: synchronous only, no thread dispatch
sigslot::signal<int> sig;
sigslot::scoped_connection conn = sig.connect(&myHandler);
sig(42);  // always synchronous on the calling thread
```

These libraries are good for in-thread event wiring where every subscriber runs on the same thread as the emitter. They have no answer for:
- Delivering a slot to a specific worker thread
- Blocking until a slot on another thread completes
- Connections across process or network boundaries

**DelegateMQ** covers all three, with the same `dmq::Signal<Sig>` API scaling from synchronous slots to async cross-thread delivery.

---

### Signal/Slot Summary

| Feature | Qt | Boost.Signals2 | sigslot / nano | DelegateMQ |
|---|---|---|---|---|
| No base class required | No | Yes | Yes | Yes |
| No code generation | No (`moc`) | Yes | Yes | Yes |
| RAII auto-disconnect | Partial | Yes | Yes | Yes |
| Slot on subscriber's thread | Yes (event loop) | No | No | Yes (any `dmq::IThread`) |
| Blocking async slot (return value) | No | No | No | Yes |
| Works without OS event loop | No | Yes | Yes | Yes |
| Embedded / RTOS support | No | No | Limited | Yes |
| Remote slots (cross-process) | No | No | No | Yes |

---

## Remote / IPC Communication

### DataBus (DDS Lite)

`dmq::databus::DataBus` is a high-level middleware built on top of DelegateMQ core. It provides a topic-based publish/subscribe system that functions as a "DDS Lite" or "MQTT Lite" specifically optimized for C++ applications that span multiple threads and remote nodes.

| Feature | DDS (Full) | MQTT | DelegateMQ DataBus |
|---|---|---|---|
| Complexity | Very High | Medium | Low |
| Footprint | Large (MBs) | Medium (Client lib) | Tiny (Header-only) |
| IDL Required | Yes | No | No (Plain C++) |
| Thread Safety | Manual Dispatch | Manual Dispatch | Automatic (via `dmq::IThread`) |
| Local inter-thread use | Impractical (10–15 threads overhead) | No | Yes (zero overhead) |
| Remote distribution | Yes | Yes | Yes |
| Location Transparency | Yes | Yes | Yes |
| QoS - Last Value Cache | Yes | Yes (Retained) | Yes |
| QoS - Lifespan (stale LVC expiry) | Yes | No | Yes |
| QoS - Min Separation (rate limiting) | Yes | No | Yes |
| QoS - Deadline Monitoring | Yes | No | Yes (client-side helper) |
| QoS - Reliability | Complex Policies | Levels 0, 1, 2 | Reliable (Unicast) or Best-Effort (Multicast) |
| Primary Use Case | Remote/Network distribution | IoT / Cloud | Inter-thread and/or Remote |

**Key Advantage**: Unlike full DDS systems that require complex IDL compilation and manual thread management to get data into your application's control loop, `dmq::databus::DataBus` handles the thread-safe delivery automatically. You simply provide a pointer to your `dmq::os::Thread` object, and the callback is guaranteed to execute in that specific context.

**Local and Remote with One API**: DDS is designed as network middleware — it is technically capable of intra-process communication, but doing so still spins up 10–15 threads for discovery and RTPS machinery, adds megabytes of footprint, and goes through full serialization overhead. In practice, DDS systems use a separate mechanism (mutexes, condition variables, OS queues) for inter-thread communication and reserve DDS for inter-node distribution. `dmq::databus::DataBus` makes no such distinction. The same `Publish`/`Subscribe` call works whether the subscriber is on the same thread, a different thread in the same process, or a different machine entirely — and local delivery costs nothing extra (no serialization, no network stack, no extra threads). Adding a `dmq::databus::Participant` extends the existing bus to the network; removing it collapses it back to purely local.

**Data Model — One Type Per Topic**: Like DDS, `dmq::databus::DataBus` enforces the data-centric constraint that each topic carries exactly one typed value (`void(T)`). A topic represents the *current state* of something in the system; a publish is a single snapshot of that state. This is intentional — it mirrors `DataWriter::write(const T& data)` in real DDS and encourages well-typed data modeling. When you need to call a remote function with multiple arguments (RPC semantics), use `dmq::RemoteChannel` directly rather than routing through `dmq::databus::DataBus`. See [Pub/Sub vs. RPC](../docs/DATABUS.md#pubsub-vs-rpc) in DATABUS.md for the full comparison.

Additionally, the `dmq::databus::DataBus` supports **UDP Multicast**, allowing a single publisher to reach an unlimited number of subscribers with zero extra CPU or network overhead per client—ideal for high-frequency telemetry and monitoring tools like the [DelegateMQ Spy](#unified-api-the-core-differentiator).

---

### DDS (Data Distribution Service)

DDS (OpenDDS, Fast DDS, RTI Connext DDS) is the standard for high-reliability, real-time pub/sub communication, especially in aerospace, defense, and robotics (ROS2 uses DDS internally). It provides automatic discovery, QoS policies, and RTPS interoperability. For those feature requirements, DDS is the right tool.

For simpler point-to-point or embedded use cases, the complexity gap is significant.

**Type definition — DDS requires IDL + code generation:**

```idl
// Data.idl — separate file, separate build step
struct SensorData {
    long x;
    long y;
    string msg;
};
```

Run `idlc` / `rtiddsgen` to generate `SensorData.h`, `SensorDataPubSubTypes.h`, `SensorDataPubSubTypes.cxx`. These are files you don't write but must maintain and regenerate on every type change.

**Publisher setup — DDS uses a 4-level object hierarchy:**

```cpp
DomainParticipant* participant =
    DomainParticipantFactory::get_instance()
        ->create_participant(0, PARTICIPANT_QOS_DEFAULT);
TypeSupport type(new SensorDataPubSubType());
type.register_type(participant);
Topic* topic = participant->create_topic(
    "SensorTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);
Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
DataWriter* writer = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);

// Send
SensorData data; data.x(5); data.y(10); data.msg("hello");
writer->write(&data);
```

**Subscriber thread dispatch — DDS listener fires on the DDS middleware thread; dispatching to your application thread is your responsibility:**

```cpp
class MyListener : public DataReaderListener {
    void on_data_available(DataReader* reader) override {
        // This runs on a DDS internal thread — NOT your thread
        DataSeq data_seq; SampleInfoSeq info_seq;
        reader->take(data_seq, info_seq, ...);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& s : data_seq)
                m_queue.push(s);
        }
        m_cv.notify_one();
    }
};

// Your thread loop — written manually
void AppThreadLoop() {
    while (running) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [&]{ return !m_queue.empty(); });
        auto d = m_queue.front(); m_queue.pop();
        lk.unlock();
        DataUpdate(d);  // finally on your thread
    }
}
```

**DelegateMQ equivalent — type definition is a plain C++ class, publish/subscribe is a one-liner, thread dispatch is automatic:**

```cpp
// Type definition — no IDL, no codegen
struct SensorData { int x = 0, y = 0; std::string msg; };

// Serializer — one per type, registered once
dmq::serialize::MsgPackSerializer<void(SensorData)> sensorSerializer;

// Publisher — same Publish() call works locally and over the network
DataBus::RegisterSerializer<SensorData>("SensorTopic", sensorSerializer);
DataBus::Publish<SensorData>("SensorTopic", { 5, 10, "hello" });

// Subscriber — DataUpdate() is called directly on m_thread, no manual dispatch
class Display {
public:
    Display() {
        m_thread.CreateThread();
        m_conn = DataBus::Subscribe<SensorData>("SensorTopic",
            dmq::MakeDelegate(this, &Display::DataUpdate, m_thread));
    }
private:
    void DataUpdate(const SensorData& d) { /* always on m_thread */ }
    dmq::os::Thread m_thread;
    dmq::ScopedConnection m_conn;
};
```

**Key differences:**

| Feature | DDS | DelegateMQ |
|---|---|---|
| Type definition | IDL file + code generator + generated files | Plain C++ struct — no base class, no codegen |
| Publisher setup | `Participant → Topic → Publisher → DataWriter` | `dmq::RemoteChannel` + `Bind()` |
| Subscriber setup | `Participant → Topic → Subscriber → DataReader` | `dmq::RemoteChannel` + `Bind()` |
| Thread dispatch on receive | Manual (listener on DDS thread → queue → your thread) | Automatic (Poll runs on your thread) |
| Discovery | Automatic (multicast/unicast, topic matching) | Partial (via Multicast group) |
| QoS policies | Rich (reliability, durability, history, lifespan, ...) | LVC, Lifespan, Min Separation, Deadline Monitoring, Fixed-block, Reliable/Best-Effort |
| Transport | RTPS over UDP/TCP, multicast, secure | Pluggable `dmq::transport::ITransport` — built-in: UDP (Win32/Linux/Zephyr/LwIP/NetX), TCP (Win32/Linux), ZeroMQ, NNG, MQTT, UART, Serial, Win32 Pipe |
| IDL / build tool required | Yes | No |
| Embedded / RTOS support | Limited | Yes |
| Interoperability standard | RTPS (vendor-interoperable) | No standard |

**Thread overhead comparison:**

A full DDS middleware creates a significant number of threads automatically on behalf of the application. A typical `DomainParticipant` spawns:

| Thread | Purpose |
|---|---|
| Receive thread(s) | One per transport/locator — blocking UDP/TCP receive loops |
| Event thread | Drives QoS timers: deadline, liveliness, heartbeat, ACKNACK |
| Async writer thread | Outgoing publish queuing and flow control |
| Discovery thread(s) | SPDP (participant discovery) and SEDP (endpoint discovery) |
| Database/GC thread | History cache cleanup, matched endpoint maintenance |

RTI Connext typically spawns **10–15 threads** per `DomainParticipant`. CycloneDDS is leaner at **3–5**. FastDDS sits in the middle at **4–8**. These threads exist regardless of application complexity and are largely opaque to the developer.

`dmq::databus::DataBus` creates **zero library threads**. The application owns one polling thread that calls `dmq::databus::Participant::ProcessIncoming()` in a loop. Every thread in the system is visible, named, and under application control — a critical requirement for RTOS environments with fixed thread budgets and deterministic scheduling.

| | DDS (RTI Connext) | DDS (CycloneDDS) | DelegateMQ DataBus |
|---|---|---|---|
| Library-owned threads | 10–15 | 3–5 | **0** |
| App-owned threads | 0 | 0 | 1 (polling loop) |
| Thread creation | Automatic on participant creation | Automatic | Manual |
| Suitable for fixed thread-budget RTOS | No | Marginal | Yes |

**Resource usage comparison:**

Full DDS implementations carry substantial binary and RAM overhead driven by discovery protocols, per-topic history queues, QoS tracking tables, and endpoint management structures — all allocated dynamically at runtime.

| | DDS (RTI Connext) | DDS (FastDDS) | DDS (CycloneDDS) | Micro XRCE-DDS | DelegateMQ DataBus |
|---|---|---|---|---|---|
| Library binary size | 15–50 MB | 5–15 MB | 2–5 MB | ~100 KB flash | **0** (header-only) |
| RAM per participant | 10+ MB | Several MB | 1–3 MB | ~20 KB | Per-topic `dmq::Signal<>` + map entry |
| Dynamic heap use | Continuous | Continuous | Continuous | Continuous | Optional (fixed-block allocator available) |
| Bare-metal capable | No | No | No | Limited | Yes |

Key points:

- **Header-only**: DelegateMQ has no precompiled library binary. Compiled code size scales with the templates instantiated — topics and serializers you don't use contribute zero code.
- **Predictable allocation**: Per-topic overhead is a `dmq::Signal<>` and a small `xmap` entry. No hidden history queues or discovery tables consume memory in the background.
- **Fixed-block allocator**: Building with `DMQ_ALLOCATOR=ON` replaces `new`/`delete` with a fixed-block pool, eliminating heap fragmentation. No DDS implementation offers this.
- **Bare-metal proof**: The `bare-metal-arm` sample runs on a Cortex-M4 with no OS, no heap, and no transport — demonstrating the lower bound of the footprint.

Micro XRCE-DDS is the embedded-targeted DDS variant and achieves a much smaller footprint (~100 KB flash, ~20 KB RAM) but at the cost of requiring a full DDS broker/agent process on the other end of the connection and losing most standard QoS policies.

**Codebase size and auditability:**

| | DDS (OpenDDS) | DDS (FastDDS) | DDS (CycloneDDS) | DelegateMQ |
|---|---|---|---|---|
| Approx. source lines (core) | ~500K | ~200K | ~150K | ~10K |
| Readable by one developer | No | No | No | Yes |
| No external build dependencies | No | No | No | Yes (headers only) |
| Qualifiable (DO-178C / IEC 61508) | High cost | High cost | High cost | Low cost |

For safety-critical or certified software, qualification cost scales directly with lines of code — every line is subject to review, traceability, and test coverage requirements. DelegateMQ's core fits comfortably in a single review pass. Full DDS stacks are effectively unqualifiable at practical cost.

Compilation time follows the same pattern. DelegateMQ is included as headers with no pre-built runtime, no CMake find modules, no pkg-config, and no vcpkg/conan package to integrate. Template instantiation scales with your topic count — topics you don't use generate no code.

---

### gRPC

gRPC provides strongly-typed, bidirectional RPC with HTTP/2 transport and Protobuf serialization. It is excellent for service-to-service communication in cloud and microservice architectures.

**gRPC requires:**
- A `.proto` file defining services and message types
- The `protoc` compiler + gRPC plugin to generate stub code (`*.pb.h`, `*.pb.cc`, `*_grpc.pb.h`, `*_grpc.pb.cc`)
- The gRPC and Protobuf runtime libraries

```proto
// sensor.proto
syntax = "proto3";
message SensorData { int32 x = 1; int32 y = 2; string msg = 3; }
message Empty {}
service SensorService { rpc SendData(SensorData) returns (Empty); }
```

The generated code is then used directly in C++. Schema changes require regenerating the stubs and recompiling.

**Thread dispatch:** gRPC server handlers run on gRPC's internal completion queue threads. Getting the call onto your application thread requires the same manual queue + mutex + CV pattern as DDS.

**Key differences:**

| Feature | gRPC | DelegateMQ |
|---|---|---|
| Type + service definition | `.proto` file + `protoc` code generation | Plain C++ class + `Bind()` |
| Call syntax on sender | `stub->SendData(&ctx, request, &response)` | `m_channel(data)` |
| Thread dispatch on receive | Manual (gRPC thread → your thread) | Automatic (Poll on your thread) |
| Transport | HTTP/2 only | Pluggable (`dmq::transport::ITransport`) |
| Streaming | Yes (bidirectional) | No |
| Embedded / RTOS support | No | Yes |
| External dependencies | gRPC + Protobuf runtimes | None (header-only core) |

---

### Raw ZeroMQ / NNG

ZeroMQ and NNG provide fast, flexible socket-style messaging with patterns like pub/sub, push/pull, and request/reply. They handle transport reliability and message framing well, but they are transport-only — serialization, type safety, and thread dispatch are entirely the application's responsibility.

```cpp
// ZeroMQ send — raw bytes, no type system
zmq::message_t msg(sizeof(int) * 2);
memcpy(msg.data(), &myData, sizeof(myData));
socket.send(msg, zmq::send_flags::none);

// ZeroMQ receive — you own deserialization and thread dispatch
zmq::message_t reply;
socket.recv(reply);
MyData* d = static_cast<MyData*>(reply.data());
// dispatch to your thread manually...
```

DelegateMQ can use ZeroMQ or NNG as its `dmq::transport::ITransport` backend, adding type-safe function-call semantics, automatic argument serialization, and thread dispatch on top.

| Feature | Raw ZeroMQ / NNG | DelegateMQ (with ZeroMQ transport) |
|---|---|---|
| Type safety | None — raw bytes | Full — C++ function signature |
| Serialization | Manual | `dmq::ISerializer` (MessagePack, RapidJSON, etc.) |
| Call semantics | `socket.send()` / `socket.recv()` | `m_channel(arg1, arg2)` |
| Thread dispatch on receive | Manual | Automatic |
| Transport reliability | Yes (ZMQ/NNG built-in) | Delegated to transport layer |

---

### Remote Summary

| Feature | DDS | gRPC | Raw ZMQ/NNG | DelegateMQ |
|---|---|---|---|---|
| No IDL / codegen | No | No | Yes | Yes |
| Type-safe call syntax | Partial (generated) | Yes (generated) | No | Yes (C++ template) |
| Thread dispatch on receive | Manual | Manual | Manual | Automatic |
| Embedded / RTOS support | Limited | No | Limited | Yes |
| Transport agnostic | No (RTPS) | No (HTTP/2) | Yes (core) | Yes (`dmq::transport::ITransport`) |
| Automatic discovery | Yes | Via service mesh | No | No |
| Blocking remote call (return value) | No (DDS) | Yes | No | Yes (`dmq::DelegateAsyncWait`) |
| External runtime dependencies | Yes | Yes | Yes | None (core) |

---

## Asynchronous Callbacks and Thread Dispatch

### std::async / std::future

`std::async` runs a callable asynchronously but does not provide control over *which thread* executes it. The standard library selects from a thread pool (or a new thread), so you cannot target a specific application thread.

```cpp
// std::async — no control over which thread runs the callable
auto fut = std::async(std::launch::async, &MyClass::Process, &obj, data);
auto result = fut.get();  // block for result
```

`std::async` also has no publish/subscribe, no multicast, and no remote dispatch. It is a one-shot call primitive, not a messaging layer.

**DelegateMQ's `dmq::DelegateAsyncWait`** dispatches to a *named, specific thread* and returns the result:

```cpp
// Dispatches to 'workerThread', blocks until complete, returns result
auto result = dmq::MakeDelegate(&obj, &MyClass::Process, workerThread, dmq::WAIT_INFINITE)(data);
```

| Feature | `std::async` | DelegateMQ |
|---|---|---|
| Target a specific thread | No | Yes |
| Return value from async call | Yes (`future`) | Yes (`dmq::DelegateAsyncWait` or `AsyncInvoke`) |
| Multicast (multiple targets) | No | Yes (`dmq::MulticastDelegateSafe`) |
| Publish / subscribe | No | Yes (`dmq::Signal`) |
| Remote dispatch | No | Yes (`dmq::RemoteChannel`) |
| Embedded / RTOS | No | Yes |

---

### OS Message Queues (FreeRTOS, Win32, POSIX)

Native OS queues (`xQueueSend`, `PostMessage`, `mq_send`) are low-level primitives for moving data between threads. They are correct and efficient but impose significant boilerplate per message type:

1. Define a message struct or union.
2. Serialize arguments into it manually.
3. Send the struct to the queue.
4. On the receiving thread, dequeue and switch on message type.
5. Deserialize arguments and call the handler.

```c
// FreeRTOS example — one message type, no type safety
typedef struct { int x; int y; } SensorMsg;
SensorMsg m = { .x = 5, .y = 10 };
xQueueSend(sensorQueue, &m, portMAX_DELAY);

// Receiving side
SensorMsg recv;
if (xQueueReceive(sensorQueue, &recv, portMAX_DELAY))
    HandleSensor(recv.x, recv.y);
```

Every new message type requires repeating this scaffolding. There is no automatic argument copying for pointer/reference arguments, no object lifetime safety, and no return values from async calls.

**DelegateMQ on FreeRTOS** — same async dispatch, full type safety, no scaffolding per call:

```cpp
// Same syntax as a local call; arguments marshalled automatically
dmq::MakeDelegate(this, &SensorTask::HandleSensor, workerThread)(5, 10);
```

| Feature | OS queues (FreeRTOS / POSIX) | DelegateMQ |
|---|---|---|
| Type safety | No (void* or union) | Yes (C++ template) |
| Boilerplate per message type | High | None |
| Automatic argument marshalling | No | Yes |
| Object lifetime safety | No | Yes (`weak_ptr`) |
| Return value from async call | No | Yes (`dmq::DelegateAsyncWait`) |
| Multicast / pub-sub | No | Yes |

---

### Boost.Asio / Executors

Boost.Asio's `post(executor, handler)` / `dispatch(executor, handler)` dispatches a callable to a specific executor (thread pool or strand). This is the closest standard-library equivalent to DelegateMQ's async dispatch concept.

```cpp
// Boost.Asio: post a lambda to a strand
boost::asio::post(my_strand, [data]() {
    HandleData(data);
});
```

Asio requires you to capture all arguments into the lambda manually. There is no automatic argument heap-copy for pointer/reference types, no built-in signal/slot, and no remote dispatch.

| Feature | Boost.Asio `post` | DelegateMQ |
|---|---|---|
| Dispatch to specific thread/strand | Yes | Yes |
| Automatic argument marshalling | No (manual capture) | Yes (by-value, by-pointer, by-reference) |
| Signal / slot | No | Yes (`dmq::Signal<Sig>`) |
| Blocking async call with return | No (manual future/promise) | Yes (`dmq::DelegateAsyncWait`) |
| Remote dispatch | No | Yes (`dmq::RemoteChannel`) |
| Embedded / RTOS | No | Yes |

---

### Async Summary

| Feature | `std::async` | OS Queues | Boost.Asio | DelegateMQ |
|---|---|---|---|---|
| Target a specific thread | No | Yes | Yes (strand) | Yes |
| Automatic argument marshalling | N/A | No | No | Yes |
| Blocking async with return value | Yes | No | Manual | Yes |
| Multicast / pub-sub | No | No | No | Yes |
| Remote dispatch | No | No | No | Yes |
| Embedded / RTOS support | No | Yes | No | Yes |
| Object lifetime safety | No | No | No | Yes (`weak_ptr`) |

---

## Unified API: The Core Differentiator

The most distinctive property of DelegateMQ is that **one abstraction covers all invocation modes with the same call syntax**. No other library in any of the categories above spans this full range:

```cpp
// All three use the same operator() call syntax:

// 1. Synchronous — runs on the calling thread
dmq::MakeDelegate(&obj, &MyClass::Process)(data);

// 2. Asynchronous — marshals arguments and runs on workerThread
dmq::MakeDelegate(&obj, &MyClass::Process, workerThread)(data);

// 3. Remote — serializes arguments and sends over transport to remote endpoint
m_remoteChannel(data);
```

And the same `dmq::Signal<Sig>` subscriber can be synchronous or asynchronous simply by how the delegate is constructed at connect time — no change to the signal or the slot:

```cpp
// Synchronous slot (called on emitter's thread)
m_conn = signal.Connect(dmq::MakeDelegate(this, &MyClass::OnEvent));

// Async slot (dispatched to m_thread; emitter does not block)
m_conn = signal.Connect(dmq::MakeDelegate(this, &MyClass::OnEvent, m_thread));
```

This composability — the same delegate model scaling from a local synchronous callback up to a remote cross-processor call — is the central design goal that no single alternative currently provides.
