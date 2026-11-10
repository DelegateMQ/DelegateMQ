# Porting Guide

Supported out of the box: Windows, Linux, POSIX, FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX, Qt, and bare metal (ARM and RISC-V).

The OS ports (`src/delegate-mq/port/os`), transports (`port/transport`) and serializers (`port/serialize`) form the **DelegateMQ Abstraction Layer** (see the [layer diagram](../README.md#overview)). Porting means implementing an interface in that layer; nothing above it changes.

---

## Table of Contents

- [Porting Checklist](#porting-checklist)
- [Embedded Systems](#embedded-systems)
- [Interfaces](#interfaces)
  - [`dmq::IThread`](#dmqithread)
    - [Send `dmq::DelegateMsg`](#send-dmqdelegatemsg)
    - [Receive `dmq::DelegateMsg`](#receive-dmqdelegatemsg)
  - [`dmq::ISerializer`](#dmqiserializer)
  - [`dmq::IDispatcher`](#dmqidispatcher)
- [Thread Implementations](#thread-implementations)
  - [Thread Priority and Latency](#thread-priority-and-latency)
  - [Message Queueing](#message-queueing)
    - [dmq::FullPolicy (Back Pressure / Drop)](#dmqfullpolicy-back-pressure--drop)
    - [dmq::ExitPolicy (Shutdown)](#dmqexitpolicy-shutdown)
  - [Start, Exit and Idle Handlers](#start-exit-and-idle-handlers)
  - [Start Gate (Synchronized Start)](#start-gate-synchronized-start)
  - [Watchdog Integration](#watchdog-integration)
  - [Performance Monitoring](#performance-monitoring)
  - [Current Thread](#current-thread)

---

## Porting Checklist

1. **Search for `@TODO`** — marks the port-specific decisions in the source.
2. **Implement `dmq::IThread`** — needed for **asynchronous** delegates.
3. **Implement `dmq::transport::ITransport`** — needed for **remote** delegates or a DataBus across processes/processors. `dmq::RemoteChannel` and DataBus `Participant` send through it; `dmq::Dispatcher` already implements `dmq::IDispatcher` on top of it.
   - Implement `dmq::ISerializer` only for a new serialization format, and `dmq::IDispatcher` only to bypass `ITransport`.
   - *Optional:* `dmq::util::TransportMonitor`, `RetryMonitor` and `ReliableTransport` add ACKs and retries over an unreliable transport.
   - See [Sample Projects](../example/sample-projects/README.md).
4. **Provide a clock** — `DelegateOpt.h` selects `dmq::Clock` per port: `std::chrono::steady_clock` on desktop, the port's own clock on an RTOS (e.g. `port/os/freertos/FreeRTOSClock.h`).
5. **Call `dmq::util::Timer::ProcessTimers()` periodically** (main loop or a high-priority task), and `Thread::WatchdogCheckAll()` if you use [watchdogs](#watchdog-integration). On an RTOS call it from a task, not an ISR (it takes OS mutexes); only bare metal (`DMQ_THREAD_NONE`) may call it from a timer ISR.
6. **Set build options** in `CMakeLists.txt`, e.g. `DMQ_ASSERTS`, `DMQ_ALLOCATOR` (fixed-block allocator instead of `new`/`delete`), `DMQ_FORCE_OPTIMIZE_DEBUG` (smaller debug templates). To tune constants (`DMQ_MAX_TIMER_EXPIRED`, `DMQ_DEFAULT_QUEUE_SIZE`, ...), copy `DelegateMQConfig_Template.h` into your project and set `-DDMQ_USER_CONFIG="DelegateMQConfig.h"`; defaults are in `delegate/DelegateMQConfig_Default.h`.
7. **Route faults** — edit `port/fault/Fault.cpp` to call your logger or crash handler.

---

## Embedded Systems

1. **Stack usage in debug builds.** At `-O0`, templates produce deep call stacks. Increase task stacks (e.g. 8 KB), build with `-O2`, or enable `-DDMQ_FORCE_OPTIMIZE_DEBUG=ON`.
2. **Non-blocking transport receive.** Blocking UART calls (e.g. `HAL_UART_Receive`) starve other tasks. Use an interrupt-driven ring buffer and have the receive task wait on a semaphore. The `stm32-freertos` example shows this, plus static stacks and FreeRTOS configuration.
3. **Transport buffer sizes.** Each transport defines its own `BUFFER_SIZE` in its header (NetX UDP uses a bare literal). Some are sized to the Ethernet MTU and can't grow without fragmentation; others can be lowered to save RAM or raised for larger messages. Check the header for the current value.

---

## Interfaces

### `dmq::IThread`

An asynchronous delegate sends its call to a target thread through `dmq::IThread`. Use a built-in `dmq::os::Thread`, or implement `IThread` on your project's own thread or event loop:

```cpp
class IThread
{
public:
    virtual ~IThread() = default;

    /// Called on the sender's thread. Queue msg to this thread; return false if not queued.
    virtual bool DispatchDelegate(std::shared_ptr<DelegateMsg> msg) = 0;

    /// Return true if the calling thread is this thread.
    virtual bool IsCurrentThread() = 0;
};
```

A custom implementation needs:

1. **`DispatchDelegate()`** — post `msg` to your thread's queue (thread-safe).
2. **Invoke on your thread** — when your loop dequeues it, call `msg->GetInvoker()->Invoke(msg)` ([below](#receive-dmqdelegatemsg)).
3. **`IsCurrentThread()`** — compare the caller to your thread's ID.
4. **Cancel what you drop** — call `msg->Cancel()` on a queued message you'll never invoke, or a blocked sender waits out its timeout ([details](#dmqexitpolicy-shutdown)).
5. *Optional:* create a `dmq::CurrentThreadScope` at the top of your loop so `dmq::ThisThread::GetCurrent()` finds it ([Current Thread](#current-thread)).

Pass your object anywhere a thread is accepted, e.g. `dmq::MakeDelegate(&func, myThread)`.

#### Send `dmq::DelegateMsg`

Calling an asynchronous delegate builds a `dmq::DelegateMsg` and hands it to the target thread:

```cpp
auto thread = this->GetThread();
if (thread)
    thread->DispatchDelegate(msg);  // target thread later calls Invoke()
```

**Adding a new OS port.** Name the class `<Platform>Thread` in `port/os/<platform>/<Platform>Thread.h/.cpp` and end the header with `using Thread = <Platform>Thread;` in `namespace dmq::os`, so all code keeps using `dmq::os::Thread`. Use the shared `port/os/common/ThreadMsg.h` for queue messages (all ports but Qt); don't copy it.

Handle a full queue per `FullPolicy` (aliased into `dmq::os`). With a lockable queue (stdlib, Win32), check space before allocating; with an atomic RTOS queue (FreeRTOS, Zephyr, ...), allocate, send, and delete on failure.

Wrap a native queue API that takes more than a couple of calls in a small RAII class exposing `Create()`/`IsCreated()`/`Send(msg, highPriority, timeout)`/`Receive(timeout)`/`Size()`/`DrainAndDelete()`/`Destroy()`. It holds no policy: `FullPolicy`, timeouts, watchdog and stats stay in `<Platform>Thread.cpp`. Models: `FreeRTOSDelegateQueue.h`, `ThreadXDelegateQueue.h`, `CmsisRtos2DelegateQueue.h`; `ZephyrDelegateQueue.h` for a queue with no priority send.

```cpp
// stdlib / Win32 style — check under lock before allocating
bool StdlibThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    if (MAX_QUEUE_SIZE > 0 && m_queue.size() >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == FullPolicy::DROP)
            return false;  // discard — no allocation wasted

        if (FULL_POLICY == FullPolicy::FAULT)
        {
            DMQ_ASSERT_TRUE(false);
            return false;
        }

        // TIMEOUT: wait up to m_dispatchTimeout for the consumer to drain a slot
        bool hasSpace = m_cvNotFull.wait_for(lk, m_dispatchTimeout, [this]() {
            return m_queue.size() < MAX_QUEUE_SIZE || m_exit.load();
        });
        if (!hasSpace)
            return false;  // timed out — dropped
    }

    auto threadMsg = xmake_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
    m_queue.push(threadMsg);
    m_cv.notify_one();
    return true;
}

// RTOS style (e.g. FreeRTOS) — allocate first, let the queue wrapper enforce the limit
bool FreeRTOSThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return false;

    // TIMEOUT waits up to m_dispatchTimeout; DROP and FAULT don't wait
    TickType_t timeout = (FULL_POLICY == FullPolicy::TIMEOUT)
        ? pdMS_TO_TICKS(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count())
        : 0;
    if (!m_queue.Send(threadMsg, msg->GetPriority() == dmq::Priority::HIGH, timeout))
    {
        delete threadMsg;  // queue full, dropped per policy
        return false;
    }
    return true;
}
```

#### Receive `dmq::DelegateMsg`

The target thread runs the call through the message's `dmq::IThreadInvoker` (implemented by the library):

```cpp
class IThreadInvoker
{
public:
    virtual ~IThreadInvoker() = default;

    /// Invoke the bound target function. Called on the destination thread.
    /// @return true if the function was invoked.
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) = 0;
};
```

Your thread loop calls it for each dequeued message:

```cpp
void Thread::Process()
{
    // ...
            case MSG_DISPATCH_DELEGATE:
            {
                auto delegateMsg = msg->GetData();
                DMQ_ASSERT_TRUE(delegateMsg);

                auto invoker = delegateMsg->GetInvoker();
                DMQ_ASSERT_TRUE(invoker);

                bool success = invoker->Invoke(delegateMsg);
                DMQ_ASSERT_TRUE(success);
                break;
            }
    // ...
}
```

---

### `dmq::ISerializer`

Serializes remote delegate arguments into a stream (`Write()`, sender) and back (`Read()`, receiver). Any format works, e.g. [MessagePack](https://msgpack.org/index.html); see `example/sample-projects`.

```cpp
template <class R>
struct ISerializer; // Not defined

template<class RetType, class... Args>
class ISerializer<RetType(Args...)>
{
public:
    /// Serialize args into os. Called by the sender.
    virtual std::ostream& Write(std::ostream& os, const Args&... args) = 0;

    /// Deserialize args from is. Called by the receiver.
    virtual std::istream& Read(std::istream& is, Args&... args) = 0;
};
```

---

### `dmq::IDispatcher`

Sends serialized argument bytes to a remote endpoint. `dmq::Dispatcher` implements it over any `ITransport`; implement it directly only to bypass `ITransport`. The receiver passes incoming bytes to `IRemoteInvoker::Invoke()`, which deserializes them and calls the target.

```cpp
class IDispatcher
{
public:
    virtual ~IDispatcher() = default;

    /// Send os to the remote system over any transport (UDP, TCP, serial, shared memory, ...).
    /// @param[in] id The delegate identifier shared by sender and receiver.
    virtual int Dispatch(dmq::xostringstream& os, DelegateRemoteId id, uint16_t* outSeqNum = nullptr) = 0;
};
```

---

## Thread Implementations

Each OS port provides a `dmq::os::Thread` (`StdlibThread`, `Win32Thread`, `PosixThread`, `FreeRTOSThread`, `ThreadXThread`, `ZephyrThread`, `CmsisRtos2Thread`, `NuttXThread`, `QtThread`) with an event loop and the features below.

### Thread Priority and Latency

A dispatched delegate waits in the target thread's queue, so dispatch latency depends mostly on that thread's OS priority and scheduler wake-up time. Give time-critical work (commands, emergency stops) high-priority threads, general logic medium, and telemetry or logging low.

### Message Queueing

`dmq::Priority::HIGH` messages run ahead of normal ones; each priority stays FIFO. Every port uses two queues (high and normal), always appends, and drains the high queue first:

- stdlib/Win32/Qt: two `std::deque`s and one condition variable.
- Zephyr: two `k_msgq`s drained with `k_poll` (`ZephyrDelegateQueue.h`).
- FreeRTOS, ThreadX, CMSIS-RTOS2: two native queues and one counting semaphore (`FreeRTOSDelegateQueue.h`).

Don't push high-priority messages to the front of one queue (consecutive ones become LIFO), and don't rely on CMSIS-RTOS2's `msg_prio` (implementations, including Zephyr's, may ignore it).

#### dmq::FullPolicy (Back Pressure / Drop)

`dmq::FullPolicy` sets what `DispatchDelegate()` does when the queue is full. Default: `FAULT`. (Also available as `dmq::os::FullPolicy`.)

```cpp
// Fault on overflow — the thread must never lose messages
dmq::os::Thread cmdThread("CmdThread", /*maxQueueSize=*/50, dmq::FullPolicy::FAULT);

// Wait up to 2 s for a free slot, then warn and drop
dmq::os::Thread safetyThread("SafetyThread", /*maxQueueSize=*/50, dmq::FullPolicy::TIMEOUT);

// Drop stale samples rather than stall the publisher
dmq::os::Thread sensorThread("SensorThread", /*maxQueueSize=*/10, dmq::FullPolicy::DROP);
```

| Policy | Behavior | Use for |
| --- | --- | --- |
| `FAULT` *(default)* | Fault immediately | Overflow is a design error that must surface in testing |
| `TIMEOUT` | Wait up to `dispatchTimeout` (default `dmq::DEFAULT_DISPATCH_TIMEOUT`, 2 s), then warn and drop | Commands, state changes: deliver if possible, never block forever. Keep the timeout below the watchdog timeout |
| `DROP` | Discard and return at once | High-rate best-effort data (telemetry, display updates) |

`maxQueueSize = 0` (default) means the port's default size, not unbounded: `DMQ_DEFAULT_QUEUE_SIZE` (20) on RTOS ports and Qt, `DMQ_THREAD_DESKTOP_QUEUE_SIZE` (1000) on stdlib, Win32 and POSIX. No queue is unbounded.

The policy applies to the whole thread. To mix drop-tolerant and loss-intolerant subscribers, use separate threads.

#### dmq::ExitPolicy (Shutdown)

`dmq::ExitPolicy` sets what `ExitThread()` does with queued messages. It's passed per call, so a thread can drain on normal shutdown and discard on an emergency stop. (Also available as `dmq::os::ExitPolicy`.)

```cpp
workerThread.ExitThread();                          // DRAIN (default)
workerThread.ExitThread(dmq::ExitPolicy::DISCARD);  // fast shutdown
```

- **`DRAIN`** *(default)*: run every queued message, then exit. `ExitThread()` returns when they've run.
- **`DISCARD`**: finish the running message; discard the rest.

Discarded messages are **canceled** (`DelegateMsg::Cancel()`): a sender blocked on one (e.g. `MakeDelegate(func, thread, WAIT_INFINITE)`) is released and sees the call fail. This covers every message dropped at exit.

A thread that exits itself (`ExitThread()` from its own handler) always discards, since its owner may already be gone.

With `DRAIN`, queued messages still run during shutdown, so call `ExitThread()` **before** destroying the objects they target, or use `DISCARD`.

**Custom `IThread`:** call `msg->Cancel()` on any queued message you drop without invoking (at exit, or on overflow after accepting it). `port/os/common/ThreadMsg.h` provides `CancelThreadMsg()` and `CancelAll()`.

### Start, Exit and Idle Handlers

Run application code on the worker thread at start, exit and idle:

```cpp
dmq::os::Thread worker("Worker");
worker.SetStartHandler(dmq::MakeDelegate([] { /* per-thread setup */ }));
worker.SetExitHandler(dmq::MakeDelegate([] { /* undo the setup */ }));
worker.SetIdleHandler(dmq::MakeDelegate([] { /* background work */ }), std::chrono::milliseconds(50));
worker.CreateThread();
```

- **Start**: runs before any message; `CreateThread()` returns after it. For per-thread setup (COM init, runtime attach, thread-locals, CPU affinity). On an RTOS, a thread created before the scheduler starts runs it when the scheduler starts, still before any message.
- **Exit**: runs after the last message on every exit path; `ExitThread()` returns after it. On a self-exit the `Thread` object may already be destroyed, so don't touch it.
- **Idle**: runs once the queue has been empty for the interval, then every interval while it stays empty ("quiet for N ms", not a fixed rate; use `dmq::util::Timer` for that). Default interval `DMQ_THREAD_IDLE_INTERVAL` (100 ms). Stops once `ExitThread()` is requested.

Set handlers only while the thread isn't running (setting one on a running thread faults). They run again on each `CreateThread()`.

**Porting:** use `port/os/common/ThreadHooks.h`. `ThreadExitGuard` runs the exit handler on every return path (`Fire()` runs it early, e.g. before signalling `ExitThread()`); `ThreadIdleTimer` bounds each queue wait and tracks when idle is due. Reference: `port/os/stdlib/StdlibThread.cpp`.

### Start Gate (Synchronized Start)

A thread runs as soon as `CreateThread()` returns, so messages sent before other threads or subscriptions exist can be lost or reordered. An optional `dmq::os::StartGate` holds threads until one "go":

```cpp
dmq::os::StartGate gate;
sensors.SetStartGate(&gate);    // before CreateThread()
network.SetStartGate(&gate);
sensors.CreateThread();         // ready, holding at the gate
network.CreateThread();
// ... subscribe, connect; messages dispatched now wait in the queues ...
gate.Open();                    // every held thread starts together
```

- **Ready but held:** `CreateThread()` runs the start handler and returns, so others can dispatch to the thread; it runs nothing (not even high priority) until `Open()`. Keep start handlers to local setup.
- **No blocking calls to a held thread** before `Open()`: an async-wait delegate times out (or hangs with `WAIT_INFINITE`).
- **`Open()` is permanent** until `Close()`; threads created after it pass straight through. A gate never opened holds its threads until they exit.
- **Exit while held:** queued messages are canceled, as with `DISCARD`.
- **Watchdog** starts when the gate opens.
- **Lifetime:** the gate may be destroyed right after `Open()` (e.g. a local); destroying it while a thread is held faults. Call `SetStartGate(nullptr)` before re-creating a thread whose gate is gone. On an RTOS, construct it after the RTOS heap is available (not as a global) and don't destroy it before the scheduler starts.

On FreeRTOS, creating every thread before `vTaskStartScheduler()` gives the same effect at boot without a gate.

**Supported** on every thread port; not bare metal. `DMQ_HAS_START_GATE` (`delegate/DelegateSelect.h`) is defined where it exists. Example: `System::Initialize()` in `example/cellutron/controller/system/System.cpp`.

**Porting:** hold a `dmq::os::StartGateClient m_startGate` (`port/os/common/StartGate.h`):

- `SetStartGate()` → `m_startGate.Set(gate)`.
- `CreateThread()`: call `OnCreate()` before creating the thread, `OnCreateFailed()` if creation fails.
- Worker entry, after signalling `CreateThread()` and before the loop: if `Enabled()`, call `Wait(m_exit)`, refresh the watchdog heartbeat, then `Passed()`. `Wait()` returns `true` if opened (even with an exit pending, so `DRAIN` still runs), `false` if exiting while held; then return without running messages.
- `ExitThread()`: call `Wake()` after setting the exit flag.
- `WatchdogCheck()`: skip while `IsHeld()`.

References: `StdlibThread.cpp`; `FreeRTOSThread.cpp` for a port whose exit send can block on a full queue. Portable tests: `test/unit-tests/StartGatePortableTest.h`.

### Watchdog Integration

Every `dmq::os::Thread` port supports an optional watchdog, enabled by passing a timeout:

```cpp
thread.CreateThread(std::chrono::seconds(2));  // fault if thread stalls > 2s
```

There's no timer or extra thread. The application calls the static `Thread::WatchdogCheckAll()` periodically (e.g. every 100 ms); `ProcessTimers()` doesn't. It checks each watched thread's `m_lastAliveTime` and calls `WatchdogHandler()` (fault by default) if it's older than the timeout.

The worker refreshes `m_lastAliveTime` at the top of each loop. With a watchdog set, its queue wait is finite (timeout / `DMQ_WATCHDOG_HEARTBEAT_DIVISOR`, default 4; Qt uses a timer), so an idle thread still refreshes without any messages.

```
Application watchdog task (highest priority)
   │
   └─ Thread::WatchdogCheckAll() every ~100 ms
        └─ WatchdogCheck() per registered thread — reads m_lastAliveTime atomically
             └─ gap > timeout → WatchdogHandler() (fault/recovery)

worker loop
   top-of-loop: m_lastAliveTime = now           ← refreshed every iteration
   queue wait (finite when a watchdog is set)    ← loops even when idle
   process message (or wake on timeout, loop back)
```

#### Priority Requirement — Critical on Single-Core RTOS

On a single-core RTOS, a CPU-spinning thread starves lower-priority tasks, including the one calling `WatchdogCheckAll()`:

| Failure mode | `WatchdogCheckAll()` priority |
|:---|:---|
| Deadlocked thread | Any (it doesn't use CPU) |
| Idle thread | Any (its loop still cycles) |
| CPU-spinning runaway | **Higher** than the watched thread |

`WatchdogCheckAll()` takes an OS mutex, so it can't run in an ISR. Call it from the system's highest-priority task, which can also call `Timer::ProcessTimers()`:

```cpp
// FreeRTOS example: dedicated watchdog task at highest priority
void WatchdogTask(void*)
{
    while (true)
    {
        dmq::os::Thread::WatchdogCheckAll();
        vTaskDelay(pdMS_TO_TICKS(100));  // check every 100 ms
    }
}

// At startup — assign priority above all worker threads
xTaskCreate(WatchdogTask, "Watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
```

`example/cellutron` calls it from each node's main loop (stdlib, FreeRTOS and ThreadX).

#### Watchdog Limitations

1. **Long handlers trip it.** A handler running longer than the timeout fires the watchdog (the thread really is unresponsive). For legitimately long work, call `ThreadCheck()` inside the handler or raise the timeout.
2. **The checker must stay alive.** If the `WatchdogCheckAll()` task stalls, nothing is checked. Run it at highest priority, and kick a hardware watchdog (e.g. STM32 IWDG) right after it returns to catch a frozen system.
3. **Priority** — see the table above.

### Performance Monitoring

Under `DMQ_DATABUS_TOOLS`, each port reports queue and latency stats. A port needs:

1. **Enqueue time:** in `DispatchDelegate()`, call `SetEnqueueTime(Timer::GetNow())` on each new `ThreadMsg`.
2. **Latency:** in the loop, compute dispatch time minus enqueue time just before invoking.
3. **`SnapshotStats()`:** return windowed and all-time stats, then reset the window:

```cpp
#if defined(DMQ_DATABUS_TOOLS)
Thread::ThreadStats Thread::SnapshotStats()
{
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;

    std::unique_lock<std::mutex> lk(m_mutex);
    stats.queue_depth = m_queue.size();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = MAX_QUEUE_SIZE;

    // Windowed average and max latency
    if (m_latencyCountWindow > 0)
        stats.latency_avg_ms = (float)m_latencyTotalWindow.count() / m_latencyCountWindow;
    else
        stats.latency_avg_ms = 0;

    stats.latency_max_window_ms = (float)m_latencyMaxWindow.count();
    stats.latency_max_all_ms = (float)m_latencyMaxAll.count();
    stats.dispatch_count = m_dispatchCountAll;

    // Reset windowed counters
    m_queueDepthMaxWindow = stats.queue_depth;
    m_latencyTotalWindow = dmq::Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = dmq::Duration(0);

    return stats;
}
#endif
```

Update the window counters on every dispatch and message under the same lock `SnapshotStats()` uses.

### Current Thread

`dmq::ThisThread::GetCurrent()` returns the `dmq::IThread` running the caller, or `nullptr` (main thread, ISR, or unregistered thread). Use it to capture the caller's thread and reply to it later.

Every `dmq::os::Thread` registers itself. A custom `IThread` creates a `dmq::CurrentThreadScope` at the top of its loop:

```cpp
void MyThread::Process()
{
    dmq::CurrentThreadScope currentScope(this);  // GetCurrent() == this until scope exit
    // ... message loop ...
}
```

Each port's `ThisThread` (`port/os/<os>/<Os>ThisThread.h`) derives its storage from `port/os/common/CurrentThreadStorage.h`:

| Port | Storage | ISR behavior |
|---|---|---|
| stdlib, Win32, Qt, POSIX | `ThreadLocalCurrentThread` (C++ `thread_local`) | N/A |
| FreeRTOS | `RegistryCurrentThread`, keyed by task handle | `nullptr` on ARM Cortex-M; see TODOs below |
| ThreadX | `RegistryCurrentThread`, keyed by `tx_thread_identify()` | See TODOs below |
| Zephyr | `RegistryCurrentThread`, keyed by `k_current_get()` | `nullptr` (`k_is_in_isr()`) |
| CMSIS-RTOS2 | `RegistryCurrentThread`, keyed by `osThreadGetId()` | See TODOs below |
| NuttX | `RegistryCurrentThread`, keyed by `pthread_self()` | `nullptr` (`up_interrupt_context()`) |
| Bare metal | `NoCurrentThread` (always `nullptr`) | N/A |

`RegistryCurrentThread` is a fixed table (no heap) with one slot per live registered thread, released on exit. More than `DMQ_MAX_CURRENT_THREADS` (default 16) at once faults; raise it in your `DelegateMQConfig.h`.

A new RTOS port supplies a key function (nonzero and unique per thread, 0 in an ISR) and derives `ThisThread` from `RegistryCurrentThread<KeyFunction, PortMutex>`.

**Porting TODOs** (tagged `@TODO`):

- **ThreadX** (`ThreadXThisThread.h`): in an ISR `tx_thread_identify()` returns the interrupted thread, so `GetCurrent()` returns its `IThread`. Return 0 from `ThreadXCurrentThreadKey()` in an ISR if you can detect it (e.g. IPSR on Cortex-M).
- **CMSIS-RTOS2** (`CmsisRtos2ThisThread.h`): same issue. Return 0 from `CmsisRtos2CurrentThreadKey()` in an ISR (e.g. `__get_IPSR() != 0`).
- **FreeRTOS on non-ARM** (`FreeRTOSThisThread.h`): ISR detection uses `xPortIsInsideInterrupt()` (Cortex-M only). Extend `detail::IsInsideFreeRTOSInterrupt()` in `FreeRTOSCriticalSection.h` for RISC-V, Xtensa, etc.
