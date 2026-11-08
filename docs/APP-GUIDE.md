# Application Guide

Rules and patterns for writing an application **with** DelegateMQ. It's short on purpose: it lists what an application must get right and points to the detailed docs for the rest. Written for developers and for coding agents (Claude Code and others) that generate application code.

[CLAUDE.md](../CLAUDE.md) holds the rules for changing the library itself (`src/delegate-mq/`). Most of them **don't** apply to application code. See [Library Rules That Don't Apply](#library-rules-that-dont-apply-to-application-code).

## Table of Contents
- [Start From the Nearest Sample](#start-from-the-nearest-sample)
- [Setup](#setup)
- [Threads](#threads)
- [Connections Must Be Stored](#connections-must-be-stored)
- [Timers](#timers)
- [Shutdown and Object Lifetime](#shutdown-and-object-lifetime)
- [DataBus](#databus)
- [Remote DataBus](#remote-databus)
- [Remote RPC](#remote-rpc)
- [Diagnostics and Interop](#diagnostics-and-interop)
- [Library Rules That Don't Apply to Application Code](#library-rules-that-dont-apply-to-application-code)
- [Checklist](#checklist)

## Start From the Nearest Sample

Most applications are a small step from an existing sample. Copy its structure, CMake file and shutdown sequence, then change it.

| You want | Start from |
|---|---|
| Async delegates and signals between threads, one process | `example/sample-code/` (`ActiveObject.cpp`, `SysDataNoLock.cpp`), `delegate_app` |
| DataBus pub/sub between threads and processes on a PC | `example/sample-projects/databus` |
| Two DelegateMQ apps over a specific transport and serializer | `example/sample-projects/<transport>-<serializer>` (e.g. `zeromq-msgpack-cpp`, `linux-udp-serializer`) |
| Client/server with managers, alarms and point-to-point RPC (`RemoteDispatcher`) | `example/sample-projects/system-architecture` (or `-no-deps`) |
| DataBus gateway relaying between two different links | `example/sample-projects/databus-relay` |
| One publisher to many subscribers over UDP multicast | `example/sample-projects/databus-multicast` |
| DataBus topics exposed to MQTT tools as JSON | `example/sample-projects/databus-mqtt-gateway` |
| FreeRTOS, runnable on a PC | `example/sample-projects/freertos-linux`, `databus-freertos` |
| Zephyr, ThreadX, NuttX, CMSIS-RTOS2, runnable on a PC | `zephyr-linux`, `threadx-linux`, `nuttx-sim`, `cmsis-rtos2-linux` |
| Bare metal, runnable in QEMU | `bare-metal-arm`, `bare-metal-riscv` |
| MCU firmware plus a PC GUI over serial | `example/pumptron` (STM32F4 + FTXUI; also runs on a FreeRTOS simulator) |
| Several nodes (GUI, controller, safety) on one DataBus | `example/cellutron` |
| Python or C# peers | `example/sample-interop`, `example/sample-projects/databus-interop` |

The RTOS and bare-metal samples run on a desktop (POSIX/Win32 simulators, `native_sim`, NuttX `sim`, QEMU), so you can build, run and fix code before it reaches a board.

## Setup

- **CMake:** set `DMQ_*` options, add DelegateMQ with `FetchContent` or `add_subdirectory`, then `target_link_libraries(app PRIVATE DelegateMQ::DelegateMQ)`.
- **Without CMake:** add `src/delegate-mq` to the include path, copy `delegate/DelegateMQConfig_Template.h` to your project as `DelegateMQConfig.h`, select ports there, and compile `src/DelegateMQ.cpp`.
- Include `"DelegateMQ.h"`.
- Pick one OS port (`DMQ_THREAD_*`), and a serializer and transport only if you go remote. Third-party ports need `DMQ_DEPS_DIR`.
- Use the same options in every file. Keep them in `DelegateMQConfig.h` or project-wide `-D` flags.
- On RTOS and bare metal, set `DMQ_ALLOCATOR` and `DMQ_ASSERTS`, and build with `-Os`/`-O2` or `DMQ_FORCE_OPTIMIZE_DEBUG` (unoptimized templates use a lot of stack and flash).

Details: [BUILD](BUILD.md).

## Threads

The library creates no threads of its own, except the receive thread inside `NetworkNode`/`RemoteDispatcher`. Each `dmq::os::Thread` is one worker with a message queue:

```cpp
dmq::os::Thread m_thread{"Worker"};                 // name, queue size, FullPolicy, ...
m_thread.CreateThread(std::chrono::seconds(2));     // optional watchdog timeout
auto d = dmq::MakeDelegate(this, &Worker::Handle, m_thread);       // fire and forget
d(arg);
auto w = dmq::MakeDelegate(this, &Worker::Get, m_thread, dmq::WAIT_INFINITE);
int v = w();                                        // blocks for the return value
```

- **Queue full:** the default `FullPolicy::FAULT` halts. For a thread fed by a fast or untrusted source, choose `DROP` (with `SetDroppedHandler()`) or `TIMEOUT`.
- **Arguments are copied** into the message. The data behind pointer and reference arguments is copied too, so out-parameters only work with blocking calls (`WAIT_INFINITE` or a timeout).
- **Blocking calls with a timeout can fail.** Check `IsSuccess()` on the delegate before using the return value, or call `AsyncInvoke()`, which returns a `std::optional`. A timed-out call is abandoned, not run later.
- **Priority:** `d.SetPriority(dmq::Priority::HIGH)` puts that delegate's messages ahead of `NORMAL` ones. Within a priority, order is FIFO.
- **Exit:** `ExitThread()` runs everything queued by default (`ExitPolicy::DRAIN`). `ExitPolicy::DISCARD` drops the queue and cancels blocked senders instead.
- **Synchronized start:** to hold several threads until the whole system is wired, give each a `dmq::os::StartGate` with `SetStartGate(&gate)` before `CreateThread()`, then `gate.Open()`. Not available on bare metal.
- **Handlers must return.** A handler that loops forever (`while (true) { ...; Sleep(); }`) blocks `ExitThread()`, and a `Thread`'s destructor calls `ExitThread()`, so the program hangs at exit. Loop on a stop flag that shutdown clears, or use a `Timer`.
- **Watchdog:** if a thread has a watchdog timeout, something must call `dmq::os::Thread::WatchdogCheckAll()` periodically, from a task, never from an ISR.
- **Don't delete a `Thread` from one of its own handlers.**

## Connections Must Be Stored

`Signal::Connect()` and every `DataBus::Subscribe*()` return a `[[nodiscard]]` `dmq::ScopedConnection`. If you discard it, the subscription ends at once, with no callbacks and no error.

```cpp
m_conn = signal.Connect(dmq::MakeDelegate(this, &X::OnEvent, m_thread));   // member: lives with X
```

Store each connection in a member (or a container of them). Destroying it, or calling `Disconnect()`, ends the subscription.

A slot connected **without** a thread runs synchronously on whichever thread emits the signal. If that's a network or timer thread, the slot blocks it and runs at its priority. Pass a thread to `MakeDelegate` unless the slot is short and safe on every emitting thread.

## Timers

`dmq::util::Timer` only fires when something calls `dmq::util::Timer::ProcessTimers()` periodically:

```cpp
m_timerConn = m_timer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &X::OnTick, m_thread));
m_timer.Start(std::chrono::milliseconds(100));
```

- Connect timers with `MakeTimerDelegate`, not `MakeDelegate`. It allows at most one tick in flight, so a slow handler can't flood the queue.
- Call `ProcessTimers()` from a dedicated loop or task on desktop and RTOS ports. Only on bare metal may it run in a hardware ISR. It must never block, so never connect a `DelegateAsyncWait` to `OnExpired`.

Details: the Timer and MakeTimerDelegate sections of [llms.txt](../llms.txt).

## Shutdown and Object Lifetime

Most mistakes happen here. An async delegate bound with a raw `this` keeps a plain pointer. The rule is:

**No call may reach an object once its destruction starts.** That needs two things:

1. **Queued messages run or are cancelled first.** If the object owns its target thread, its destructor or `Stop()` calls `m_thread.ExitThread()` explicitly, before any member is destroyed. The default `ExitPolicy::DRAIN` runs what's queued while the object is still whole.
2. **Nothing new arrives.** Every subscription feeding the object is disconnected, and every thread that emits to it (network receive threads, the timer thread, other workers) is stopped, before the object is destroyed. `Disconnect()` doesn't wait for an emit already running on another thread: that emit can still dispatch once after `Disconnect()` returns ([SIGNALS](SIGNALS.md#disconnect-and-calls-already-in-progress)).

### A complete component

Copy this shape for a component with its own thread, a subscription and a timer:

```cpp
class MyService {
public:
    MyService() : m_thread("MyService") {}
    ~MyService() { Stop(); }   // explicit: see "Member order" below

    void Start() {
        m_thread.CreateThread();
        m_dataConn = dmq::databus::DataBus::Subscribe<SensorMsg>(
            "sensor/data", dmq::MakeDelegate(this, &MyService::OnData), &m_thread);
        m_timerConn = m_timer.OnExpired.Connect(
            dmq::util::MakeTimerDelegate(this, &MyService::OnTick, m_thread));
        m_timer.Start(std::chrono::milliseconds(100));
    }

    void Stop() {
        m_timer.Stop();
        m_timerConn.Disconnect();
        m_dataConn.Disconnect();
        m_thread.ExitThread();   // drains queued messages while every member is still alive
    }

private:
    void OnData(const SensorMsg& msg) { /* runs on m_thread */ }
    void OnTick() { /* runs on m_thread */ }

    dmq::os::Thread m_thread;
    dmq::util::Timer m_timer;
    dmq::ScopedConnection m_dataConn;
    dmq::ScopedConnection m_timerConn;
    std::vector<int> m_data;     // used by the handlers
};
```

This is safe with a raw `this` only if its emitters are stopped before it's destroyed (the next section). The destructor calls `Stop()` again after an explicit `Stop()`, as the samples do.

**Member order:** always call `ExitThread()` explicitly in the destructor or `Stop()`; don't rely on `~Thread()`. Members are destroyed in reverse declaration order, so `m_data`, declared after `m_thread`, would be destroyed *before* `~Thread()` drains the queue, and a queued handler would touch a destroyed member.

### Shutdown order

Stop the sources first and the sinks last:

```cpp
// in main(), before returning
app.Stop();                 // per component: stop timers, Disconnect(), ExitThread()
network.Stop();             // network receive thread: no more inbound publishes
timerLoopRunning = false;   // ProcessTimers() loop
timerThread.join();         // or ExitThread()
// return: static and stack objects are now destroyed with nothing emitting to them
```

The samples follow this order. Pumptron's `System::Shutdown()` and the sender/receiver samples' `Stop()` plus `main` are good references.

### Which binding to use

| Subscriber | Bind with | Why |
|---|---|---|
| Singleton or static, with emitters stopped before `main` returns | raw `this` | Never destroyed while anything emits to it |
| Object in an RTOS task that never ends | raw `this` | Never destroyed |
| Object that owns its thread, with a `Stop()` called before destruction as above | raw `this` | Shutdown order covers both conditions |
| Object created and destroyed **while the system runs** (session, connection handler, UI panel, plugin) | `std::shared_ptr`: `MakeDelegate(sp, &X::F, thread)`, targeting a **long-lived shared thread** | Becomes `DelegateMemberAsyncSp`, which holds a `weak_ptr` and skips the call if the object has gone. The thread must outlive the object (see below) |
| Blocking call (`WAIT_INFINITE` or a timeout) | raw `this` | The caller waits; a timed-out call is abandoned, not run later |

**The `weak_ptr` protects the object, not the thread.** The delegate holds its target thread as a raw `IThread*`, and an emit calls `DispatchDelegate()` on that thread before the `weak_ptr` is checked on dequeue. If the object owns the thread as a member and is destroyed while other threads still emit, a late emit dispatches to a destroyed `Thread`. For objects created and destroyed at runtime, dispatch to a thread that outlives them (a shared worker or the UI thread), not to a `dmq::os::Thread` member of their own.

The `shared_ptr` form doesn't stop a call that's already running, since that call holds a strong reference until it returns. If a handler must not run at all after a stop, gate it with a flag as well. `MakeTimerDelegate` and `DataBus::Subscribe` accept the `shared_ptr` form too: pass `MakeDelegate(sp, &X::F)` to `Subscribe` along with the thread pointer.

Details: [DETAILS: Object Lifetime](DETAILS.md#object-lifetime-and-async-delegates).

## DataBus

Use DataBus to distribute data by topic: sensor values, status, state that whoever is interested should get. For "did this specific call land?", use [Remote RPC](#remote-rpc).

```cpp
m_conn = dmq::databus::DataBus::Subscribe<Temp>("sensor/temp",
             dmq::MakeDelegate(this, &X::OnTemp), &m_thread);   // handler takes const Temp&
dmq::databus::DataBus::Publish<Temp>("sensor/temp", t);
```

- **Pass the thread to `Subscribe`, not to `MakeDelegate`.** `DataBus::Subscribe` takes a synchronous delegate (`MakeDelegate(this, &X::F)`, two arguments) plus the thread as a pointer (`&m_thread`). This differs from `Signal::Connect`, where the thread goes into `MakeDelegate` by reference. Passing the thread to both sends every message through the queue twice.
- **Handler signature is `void(const T&)`.** `void(T)` compiles but misses the fast path and is wrapped in a `std::function`, which may allocate. `void(T&)` doesn't compile.
- **One C++ type per topic.** Publishing a different type on the same topic faults at runtime. Keep topic names and types in one shared header, as the samples' `common/` folders do.
- **Tests:** call `DataBus::ResetForTesting()` at the start of each test.
- **Ordering:** one publisher to one subscriber is in order. With several publishers, or a retried `RELIABLE` message, a newer value can arrive before an older one. If order matters, put a sequence number in the message.

### QoS

Pass a `dmq::databus::QoS` as the fourth `Subscribe` argument:

| Field | Effect |
|---|---|
| `lastValueCache = true` | A new subscriber gets the latest value at once |
| `lifespan` | With the last-value cache: don't deliver a cached value older than this |
| `minSeparation` | Rate-limit this subscriber only; faster publishes are dropped for it |

- **The cached value can arrive late.** With a thread, a subscriber may get a fresh live value and then the older cached one. If that matters, stamp a sequence number at publish and drop stale ones with `dmq::util::MonotonicGuard<uint32_t>::IsNewer(msg.seq)`.
- **Deadline:** `dmq::databus::DeadlineSubscription<T>(topic, deadline, handler, onMissed, &m_thread)` calls `onMissed` when a publisher goes silent. It is built on `Timer`, so it only fires if something calls `ProcessTimers()`. It can't be copied or moved; hold it in a `std::optional` or `unique_ptr` member and destroy it in `Stop()`.

Details: [DATABUS](DATABUS.md), [TUTORIAL](TUTORIAL.md).

## Remote DataBus

The same `Publish`/`Subscribe` code reaches other processes and boards once a `NetworkNode` (UDP-style transports) is added. Local subscribers don't change.

```cpp
dmq::serialization::serializer::Serializer<void(SensorMsg)> m_serSensor;   // outlives the node
dmq::databus::NetworkNode<dmq::transport::LinuxUdpTransport> m_network;

m_network.Start("Controller", 5011);                           // listen port; starts its receive thread
m_failConn = m_network.OnDeliveryFailed.Connect(dmq::MakeDelegate(this, &X::OnDeliveryFailed, m_thread));
m_network.Send<SensorMsg>("sensor/data", RID_SENSOR, m_serSensor, dmq::databus::Reliability::RELIABLE);
m_network.Receive<CmdMsg>("cmd/run", RID_CMD, m_serCmd);
m_network.AddPeer("GUI", "127.0.0.1", 5010);
// shutdown: m_network.Stop() before the subscribers are destroyed
```

- **It needs `ProcessTimers()`.** `NetworkNode` polls its transport from a `Timer` tick on its own thread, so nothing is received and no retries happen unless something calls `Timer::ProcessTimers()` (see [Timers](#timers)).
- **Topics, remote IDs and serializers are shared.** Every node uses the same topic name, `DelegateRemoteId` and message type for a topic. Keep them in one header used by all nodes (the samples' `common/` folders). A missing `Send`/`Receive` registration fails silently: nothing is sent or received.
- **Serializers must outlive the node.** `Send`/`Receive` keep a reference.
- **Reliability is per topic, chosen in `Send`.** `UNRELIABLE` (default) is fire-and-forget. `RELIABLE` adds ACK and retry over UDP or serial. A `TIMEOUT` status (`OnPeerSendStatus`) means a retry is coming; `OnDeliveryFailed` means the retries ran out. Handle them differently.
- **Subscribe to the status signals:** `DataBus::SubscribeError` for serialize and dispatch errors, and the `NetworkNode` signals `OnDeliveryFailed`, `OnPeerSendStatus`, `OnPeerCapExceeded`, `OnPeerPendingExceeded`. They fire on the network thread, so connect them with a thread or keep the handler short.
- **Capacity:** each peer uses two of the process-wide `DMQ_MAX_PARTICIPANTS` (default 8). Raise it for nodes with more than a few peers or links.
- **Relaying:** a node only delivers received topics locally. To forward one out another link (a gateway), receive it with `dmq::databus::Delivery::RELAY` and `Send` it on the other link. Never send it back out the link it came in on. See [Relay Nodes](DATABUS.md#relay-nodes).
- **Serial links:** `NetworkNode` fits UDP-style transports. For a serial port, copy Pumptron's `SerialLink`, which has the same API.
- **Bare `Participant`:** `NetworkNode` wraps `Participant` plus a receive thread. Using `Participant` directly means calling `ProcessIncoming()` in a loop yourself; see [Remote Integration](DATABUS.md#remote-integration).

## Remote RPC

Use `dmq::rpc::RemoteDispatcher` with `dmq::RemoteChannel` for point-to-point calls to one endpoint, when the caller needs to know the call arrived (commands, request/acknowledge). It has no topics and no fan-out.

```cpp
// in a NetworkMgr-style class that owns the transport
m_dispatcher.SetCloseHandler(dmq::MakeDelegate(this, &NetworkMgr::CloseTransports));
m_transport.Create(...);
m_dispatcher.Attach(m_transport, m_transport);
m_cmdChannel.emplace(m_dispatcher.GetSendTransport(), m_cmdSer);
m_cmdChannel->Bind(this, &NetworkMgr::OnCommand, CMD_MSG_ID);   // receive side
m_dispatcher.RegisterEndpoint(CMD_MSG_ID, *m_cmdChannel);
m_dispatcher.Start();

(*m_cmdChannel)(cmd);                                           // fire and forget
bool acked = m_dispatcher.RemoteInvokeWait(*m_cmdChannel, cmd); // blocks until ACK or timeout
```

- **Receive handlers run on the dispatcher's network thread.** Keep them short and pass the message on to your own thread or a `Signal`, as `system-architecture`'s `NetworkMgr` does. The lifetime rules above apply: stop the dispatcher before destroying anything its handlers reach.
- **`RemoteInvokeWait` blocks the caller** until the remote ACKs or the wait limit passes (about 9.4 s with the defaults, covering all retries). `false` means it wasn't acknowledged. Use `RemoteInvokeWaitFor(channel, timeout, args...)` for a shorter limit; then `false` can also mean "not ACKed yet". Don't call it from a thread that must stay responsive (UI, timers, a watchdogged thread with a short timeout).
- **Message IDs, argument types and serializers must match** on both ends. Keep the IDs in one shared header.
- **Reliability is per connection.** For UDP or serial, wrap the transport in `ReliableTransport` with a `RetryMonitor` and call `AttachRetryMonitor()` after `Attach()`. Skip it for transports that are already reliable (TCP, ZeroMQ).
- **Status signals:** `OnError`, `OnStatus` (`TIMEOUT` = retrying) and `OnDeliveryFailed` (gave up). Connect them like the DataBus ones.
- **Shutdown:** `m_dispatcher.Stop()` calls the close handler and joins the receive thread. Call it before the objects its handlers forward to are destroyed.
- **One peer per dispatcher.** For several peers, hold several dispatchers, or use the Remote DataBus.

Details: [extras/rpc](../src/delegate-mq/extras/rpc/README.md), [DETAILS: Remote Delegates](DETAILS.md#remote-delegates).

## Diagnostics and Interop

- **Watch the bus:** link `SpyBridge` (`tools/bridge/spy`) and register a stringifier per topic with `DataBus::RegisterStringifier<T>(topic, func)`, then run `dmq-spy` (build with `DMQ_TOOLS=ON`). `NodeBridge` feeds `dmq-monitor` and `dmq-thread`. These are desktop tools; the bridges send UDP from the node.
- **MQTT and JSON:** declare topics once with `JsonTopics::Expose<T>()` / `Accept<T>()` and link `MqttBridge`. Don't write per-topic bridge code. See `databus-mqtt-gateway`.
- **Python and C#:** peers use `interop/python` or `interop/csharp` over the `DmqInterop` C API. Messages crossing languages use MessagePack (`MSGPACK_DEFINE` on the C++ type), with the same topic names and remote IDs on both sides.

Details: [TOOLS](../tools/TOOLS.md), [INTEROP](INTEROP.md).

## Library Rules That Don't Apply to Application Code

These [CLAUDE.md](../CLAUDE.md) rules exist because the library must build on every target. In application code, follow your own platform's constraints instead:

| Library rule | In application code |
|---|---|
| No `std::vector`, `std::string`, `std::map`; use `xlist`, `xstring`, `xmap` | Use whatever your platform allows. The `x*` types are available if you want fixed-block allocation |
| No capturing lambdas | Lambdas are fine. On constrained targets, keep captures small, since a large one can allocate inside `std::function` |
| No `try`/`catch`; use `BAD_ALLOC()` and `DMQ_ASSERT()` | Handle errors your own way. Exceptions are fine where your build enables them |
| `dmq::Mutex` and `dmq::os::Thread` instead of `std::mutex` and `std::thread` | On desktop, `std::` types are fine. On RTOS targets, the `dmq::` types are the portable option |
| Tunables as `DMQ_*` macros, self-guarding `.cpp` files, `DelegateOpt.h` | Not applicable |

Library *behaviour* does still apply: store connections, follow the shutdown order, keep `ProcessTimers()` out of ISRs on RTOS ports, and never use `dmq::CriticalSection` as a general lock.

## Checklist

- [ ] Every `Connect()`/`Subscribe*()` result is stored.
- [ ] Every thread is created explicitly and exited explicitly in `Stop()` or the destructor, never left to `~Thread()`.
- [ ] `DataBus::Subscribe` gets a two-argument `MakeDelegate` plus `&m_thread`, and its handler takes `const T&`.
- [ ] Objects bound with `shared_ptr` dispatch to a thread that outlives them.
- [ ] No handler loops forever without a stop flag.
- [ ] Something calls `Timer::ProcessTimers()` (and `Thread::WatchdogCheckAll()` if watchdogs are on), from a task, not an RTOS ISR.
- [ ] Timers are connected with `MakeTimerDelegate`.
- [ ] Shutdown stops the emitters (network, timer loop, producers) before destroying subscribers.
- [ ] Objects created and destroyed at runtime are bound with `shared_ptr`.
- [ ] Each queue's `FullPolicy` is chosen on purpose for its source.
- [ ] DataBus topic names, types and remote IDs live in one shared header; error and delivery signals are subscribed.
- [ ] Every remote topic has a `Send` or `Receive` registration with a serializer that outlives the node.
- [ ] `TIMEOUT` (still retrying) and `OnDeliveryFailed` (gave up) are handled differently.
- [ ] RPC receive handlers hand work off to an application thread; `RemoteInvokeWait` isn't called from a thread that must stay responsive.
- [ ] `NetworkNode::Stop()` / `RemoteDispatcher::Stop()` run before the subscribers they feed are destroyed.
- [ ] Blocking calls with a timeout check `IsSuccess()` (or use `AsyncInvoke()`'s `std::optional`).
