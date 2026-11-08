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
- [DataBus and Remote Nodes](#databus-and-remote-nodes)
- [Library Rules That Don't Apply to Application Code](#library-rules-that-dont-apply-to-application-code)
- [Checklist](#checklist)

## Start From the Nearest Sample

Most applications are a small step from an existing sample. Copy its structure, CMake file and shutdown sequence, then change it.

| You want | Start from |
|---|---|
| Async delegates and signals between threads, one process | `example/sample-code/` (`ActiveObject.cpp`, `SysDataNoLock.cpp`), `delegate_app` |
| DataBus pub/sub between threads and processes on a PC | `example/sample-projects/databus` |
| Two DelegateMQ apps over a specific transport and serializer | `example/sample-projects/<transport>-<serializer>` (e.g. `zeromq-msgpack-cpp`, `linux-udp-serializer`) |
| Client/server with managers, alarms and a network layer | `example/sample-projects/system-architecture` (or `-no-deps`) |
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
- **Handlers must return.** A handler that loops forever (`while (true) { ...; Sleep(); }`) blocks `ExitThread()`, and a `Thread`'s destructor calls `ExitThread()`, so the program hangs at exit. Loop on a stop flag that shutdown clears, or use a `Timer`.
- **Watchdog:** if a thread has a watchdog timeout, something must call `dmq::os::Thread::WatchdogCheckAll()` periodically, from a task, never from an ISR.
- **Don't delete a `Thread` from one of its own handlers.**

## Connections Must Be Stored

`Signal::Connect()` and every `DataBus::Subscribe*()` return a `[[nodiscard]]` `dmq::ScopedConnection`. If you discard it, the subscription ends at once, with no callbacks and no error.

```cpp
m_conn = signal.Connect(dmq::MakeDelegate(this, &X::OnEvent, m_thread));   // member: lives with X
```

Store each connection in a member (or a container of them). Destroying it, or calling `Disconnect()`, ends the subscription.

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

## DataBus and Remote Nodes

```cpp
m_conn = dmq::databus::DataBus::Subscribe<Temp>("sensor/temp",
             dmq::MakeDelegate(this, &X::OnTemp), &m_thread);   // handler takes const Temp&
dmq::databus::DataBus::Publish<Temp>("sensor/temp", t);
```

- **Pass the thread to `Subscribe`, not to `MakeDelegate`.** `DataBus::Subscribe` takes a synchronous delegate (`MakeDelegate(this, &X::F)`, two arguments) plus the thread as a pointer (`&m_thread`). This differs from `Signal::Connect`, where the thread goes into `MakeDelegate` by reference. Passing the thread to both sends every message through the queue twice.
- **Handler signature is `void(const T&)`.** `void(T)` compiles but misses the fast path and is wrapped in a `std::function`, which may allocate. `void(T&)` doesn't compile.
- **One C++ type per topic.** Publishing a different type on the same topic faults at runtime. Keep topic names and types in one shared header, as the samples' `common/` folders do.
- To go remote, add a `NetworkNode` (or `Participant`) with a transport and a serializer, and register each remote topic's serializer. Error and delivery status arrive through `DataBus::SubscribeError`, `OnDeliveryFailed` and the related signals. Subscribe to them.
- **Tests:** call `DataBus::ResetForTesting()` at the start of each test.

Details: [DATABUS](DATABUS.md), [TUTORIAL](TUTORIAL.md).

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
- [ ] DataBus topic types live in one shared header; error and delivery signals are subscribed.
