# Porting Guide

Numerous predefined platforms are already supported — Windows, Linux, FreeRTOS, ARM bare-metal, ThreadX, Zephyr, CMSIS-RTOS2, and Qt. Ready-made plugins for threading and communication interfaces exist, or you can create new ones.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Porting Checklist](#porting-checklist)
- [Embedded Systems](#embedded-systems)
- [Interfaces](#interfaces)
  - [`dmq::IThread`](#ithread)
    - [Send `dmq::DelegateMsg`](#send-delegatemsg)
    - [Receive `dmq::DelegateMsg`](#receive-delegatemsg)
  - [`dmq::ISerializer`](#iserializer)
  - [`dmq::IDispatcher`](#idispatcher)
- [Thread Implementations](#thread-implementations)
  - [Thread Priority and Latency](#thread-priority-and-latency)
  - [Message Queueing](#message-queueing)
  - [Watchdog Integration](#watchdog-integration)

---

## Porting Checklist

1. **Search codebase for `@TODO`** — find specific decision locations tagged in the source files.
2. **Implement `dmq::IThread`** — required to use **Asynchronous** delegates.
3. **Implement `dmq::ISerializer` and `dmq::transport::ITransport`** — required to use **Remote** delegates across processes/processors.
   - *Optional:* Implement `dmq::transport::ITransportMonitor` if your application layer requires command acknowledgments (ACKs).
   - See [Sample Projects](DETAILS.md#sample-projects) for numerous remote delegate examples.
4. **Check System Clock** — ensure `std::chrono::steady_clock` is supported on your target hardware, as it is required for timers and transport timeouts. Otherwise, change `dmq::Clock` in `DelegateOpt.h` to a new clock type.
5. **Call `dmq::util::Timer::ProcessTimers()`** — periodically call `ProcessTimers()` (e.g., from a main loop or hardware timer ISR) to support timers and thread watchdogs.
6. **Configure Build Options** — set CMake DMQ library build options within `CMakeLists.txt`.
   - Example: `DMQ_ASSERTS` for debug assertions.
   - Example: `DMQ_ALLOCATOR` to switch between standard heap (`new`/`delete`) and the deterministic Fixed Block Allocator.
7. **Implement Fault Handling** — customize `Fault.cpp` to route errors to your system's logger or crash handler.

---

## Embedded Systems

Running C++ messaging on embedded targets (like STM32) requires specific attention to resources.

1. **Stack Usage & Debug Mode**

   **Issue:** In Debug mode (`-O0`), C++ templates generate deep call stacks, possibly causing stack overflows.  
   **Fix:** Increase task stack (e.g. 8 KB) or use Release mode (`-O2`), where stacks shrink significantly.

2. **Transport Implementation (if using remote delegates)**

   **Issue:** Blocking UART calls (e.g., `HAL_UART_Receive`) starve high-priority tasks.  
   **Fix:** Use an interrupt-driven ring buffer. The ISR captures data immediately, and the receive task sleeps on a semaphore until data is available.

See the `stm32-freertos` example `README.md` for a complete implementation of static stacks, interrupt-driven UART, and correct FreeRTOS configuration.

---

## Interfaces

DelegateMQ interface classes allow customizing the library's runtime behavior.

### `dmq::IThread`

The `dmq::IThread` interface sends a delegate and its argument data through a message queue. A delegate thread is required to dispatch asynchronous delegates to a specified target thread. The delegate library automatically constructs a `dmq::DelegateMsg` containing everything necessary for the destination thread.

```cpp
class IThread
{
public:
    virtual ~IThread() = default;

    /// Dispatch a DelegateMsg onto this thread. The implementer is responsible
    /// for getting the DelegateMsg into an OS message queue. Once DelegateMsg
    /// is on the correct thread of control, the DelegateInvoker::Invoke() function
    /// must be called to execute the delegate.
    /// @param[in] msg - a pointer to the delegate message that must be created dynamically.
    /// @pre Caller *must* create the DelegateMsg argument dynamically.
    /// @post The destination thread calls Invoke().
    virtual void DispatchDelegate(std::shared_ptr<DelegateMsg> msg) = 0;
};
```

#### Send `dmq::DelegateMsg`

An asynchronous delegate's `operator()(Args... args)` calls `DispatchDelegate()` to send a delegate message to the destination thread.

```cpp
auto thread = this->GetThread();
if (thread) {
    // Dispatch message onto the callback destination thread. Invoke()
    // will be called by the destination thread.
    thread->DispatchDelegate(msg);
}
```

`DispatchDelegate()` inserts a message into the thread's message queue. The `dmq::os::Thread` class uses an underlying `std::thread` on stdlib/Win32 platforms and a FreeRTOS task on embedded. Create a unique `DispatchDelegate()` implementation based on your platform's OS API.

The implementation should handle the queue-full case according to `dmq::os::FullPolicy` before allocating the message. On platforms with a lockable queue (stdlib, Win32), check first then allocate to avoid wasting heap on drops. On RTOS platforms (FreeRTOS, Zephyr, etc.) the OS queue API is atomic, so allocate first and delete on failure:

```cpp
// stdlib / Win32 style — check under lock before allocating
void Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    if (MAX_QUEUE_SIZE > 0 && m_queue.size() >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == dmq::os::FullPolicy::DROP)
            return;  // discard — no allocation wasted

        // BLOCK: wait until consumer drains a slot
        m_cvNotFull.wait(lk, [this]() {
            return m_queue.size() < MAX_QUEUE_SIZE || m_exit.load();
        });
    }

    auto threadMsg = std::make_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
    m_queue.push(threadMsg);
    m_cv.notify_one();
}

// RTOS style (e.g. FreeRTOS) — allocate first, let OS API enforce the limit
void Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return;

    TickType_t timeout = (FULL_POLICY == dmq::os::FullPolicy::DROP) ? 0 : portMAX_DELAY;
    if (xQueueSend(m_queue, &threadMsg, timeout) != pdPASS)
        delete threadMsg;  // queue full, dropped per policy
}
```

#### Receive `dmq::DelegateMsg`

Inherit from `dmq::IDelegateInvoker` and implement the `Invoke()` function. The destination thread calls `Invoke()` once the message is dequeued.

```cpp
/// @brief Abstract base class to support asynchronous delegate function invoke
/// on destination thread of control.
///
/// @details Inherit from this class and implement `Invoke()`. The implementation
/// typically posts a message into the destination thread message queue. The destination
/// thread receives the message and invokes the target bound function.
class IDelegateInvoker
{
public:
    /// Called to invoke the bound target function by the destination thread of control.
    /// @param[in] msg - the incoming delegate message.
    /// @return `true` if function was invoked; `false` if failed.
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) = 0;
};
```

The `dmq::os::Thread::Process()` loop below shows the dispatch pattern. `Invoke()` is called for each incoming `MSG_DISPATCH_DELEGATE` queue message.

```cpp
void Thread::Process()
{
    m_timerExit = false;
    std::thread timerThread(&Thread::TimerThread, this);

    while (1)
    {
        std::shared_ptr<ThreadMsg> msg;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            while (m_queue.empty())
                m_cv.wait(lk);

            if (m_queue.empty())
                continue;

            msg = m_queue.front();
            m_queue.pop();
        }

        switch (msg->GetId())
        {
            case MSG_DISPATCH_DELEGATE:
            {
                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);

                auto invoker = delegateMsg->GetDelegateInvoker();
                ASSERT_TRUE(invoker);

                bool success = invoker->Invoke(delegateMsg);
                ASSERT_TRUE(success);
                break;
            }

            case MSG_TIMER:
                dmq::util::Timer::ProcessTimers();
                break;

            case MSG_EXIT_THREAD:
            {
                m_timerExit = true;
                timerThread.join();
                return;
            }

            default:
                throw std::invalid_argument("Invalid message ID");
        }
    }
}
```

---

### `dmq::ISerializer`

The `dmq::ISerializer` interface serializes argument data for sending to a remote destination endpoint. Each argument is serialized and deserialized according to system requirements. Custom serialization libraries such as [MessagePack](https://msgpack.org/index.html) are supported. The `examples/sample-projects` folder contains working examples.

```cpp
template <class R>
struct ISerializer; // Not defined

/// @brief Delegate serializer interface for serializing and deserializing
/// remote delegate arguments. Implemented by application code if remote
/// delegates are used.
///
/// @details All argument data is serialized into a stream. `Write()` is called
/// by the sender when the delegate is invoked. `Read()` is called by the receiver
/// upon reception of the remote message data bytes.
template<class RetType, class... Args>
class ISerializer<RetType(Args...)>
{
public:
    /// Serialize data for transport.
    /// @param[out] os The output stream
    /// @param[in] args The target function arguments
    /// @return The output stream
    virtual std::ostream& Write(std::ostream& os, Args... args) = 0;

    /// Deserialize data from transport.
    /// @param[in] is The input stream
    /// @param[out] args The target function arguments
    /// @return The input stream
    virtual std::istream& Read(std::istream& is, Args&... args) = 0;
};
```

---

### `dmq::IDispatcher`

The `dmq::IDispatcher` interface dispatches serialized argument data to a remote destination endpoint. Custom dispatchers using sockets, named pipes, ZeroMQ, or any other transport are supported. The `examples/sample-projects` folder contains working examples.

```cpp
/// @brief Delegate interface class to dispatch serialized function argument data
/// to a remote destination. Implemented by the application if using remote delegates.
///
/// @details Incoming data from the remote must call `IDispatcher::Dispatch()` to
/// invoke the target function using argument data. The argument data is serialized
/// for transport using a concrete class implementing the `ISerializer` interface,
/// allowing any serialization method to be used.
/// @post The receiver calls `IRemoteInvoker::Invoke()` when the dispatched message
/// is received.
class IDispatcher
{
public:
    /// Dispatch a stream of bytes to a remote system. The implementer is responsible
    /// for sending the bytes over a communication transport (UDP, TCP, shared memory,
    /// serial, ...).
    /// @param[in] os An outgoing stream to send to the remote destination.
    virtual int Dispatch(std::ostream& os) = 0;
};
```

---

## Thread Implementations

While DelegateMQ provides the `dmq::IThread` interface, the library includes concrete `dmq::os::Thread` class implementations for many OSs (stdlib, Win32, FreeRTOS, etc.). These implementations provide a standard event loop and several advanced features for robustness and flow control.

### Thread Priority and Latency

When a delegate is dispatched to a worker thread, it is posted to that thread's message queue. The thread's OS priority determines how quickly the callback executes relative to other tasks.

- **High Priority**: Use for time-critical delegates (e.g., safety-critical commands, emergency stops).
- **Medium Priority**: Suitable for general application logic.
- **Low Priority**: Best for non-critical tasks like background telemetry or logging.

The end-to-end dispatch latency is primarily dominated by the destination thread's priority and the OS scheduler wake-up time.

### Message Queueing

The `dmq::os::Thread` class uses an underlying `std::priority_queue` to ensure high-priority delegate messages jump to the front of the line.

#### dmq::os::FullPolicy (Back Pressure / Drop)

When a thread's message queue has a fixed size (`maxQueueSize > 0`), `dmq::os::FullPolicy` controls what `DispatchDelegate()` does when the queue is full. The default is `dmq::os::FullPolicy::FAULT`.

```cpp
// Fault on overflow — appropriate for threads that must never silently lose messages
dmq::os::Thread cmdThread("CmdThread", /*maxQueueSize=*/50, dmq::os::FullPolicy::FAULT);

// Wait up to 2 s for the consumer to drain a slot, then log a warning and drop
dmq::os::Thread safetyThread("SafetyThread", /*maxQueueSize=*/50, dmq::os::FullPolicy::TIMEOUT);

// Drop stale samples rather than stall the publisher
dmq::os::Thread sensorThread("SensorThread", /*maxQueueSize=*/10, dmq::os::FullPolicy::DROP);
```

- **`dmq::os::FullPolicy::FAULT`** *(default)*: `DispatchDelegate()` triggers a system fault if the queue is full. This makes overflow immediately visible during development and integration testing rather than silently degrading. Use when a full queue indicates a design error (producer outrunning consumer) that should not be masked.
- **`dmq::os::FullPolicy::TIMEOUT`**: `DispatchDelegate()` waits up to `dispatchTimeout` (default `dmq::DEFAULT_DISPATCH_TIMEOUT` = 2 s) for the consumer to drain a slot, then logs a warning and drops the message. This provides bounded back pressure — the publisher is held briefly during transient bursts but is never stalled indefinitely. Use for critical topics (commands, state transitions) where every message should be delivered if possible but unbounded blocking is unacceptable. Choose a timeout shorter than the watchdog timeout so stalls are detected and reported.
- **`dmq::os::FullPolicy::DROP`**: `DispatchDelegate()` silently discards the message and returns immediately without stalling the caller. Ideal for high-rate best-effort data (sensor telemetry, display updates) where a stale sample is preferable to stalling the publisher.

Setting `maxQueueSize = 0` disables the limit entirely — `dmq::os::FullPolicy` has no effect and all messages are queued regardless of consumer speed.

`dmq::os::FullPolicy` is a thread-level setting. All delegates dispatched to the same `dmq::os::Thread` instance share the policy. If a single thread serves both drop-tolerant and loss-intolerant subscribers, split them across separate threads with different policies.

### Watchdog Integration

All `dmq::os::Thread` port implementations (stdlib, Win32, FreeRTOS, CMSIS-RTOS2, ThreadX, Zephyr, Qt) support an optional watchdog. Enable it by passing a timeout to `CreateThread()`:

```cpp
thread.CreateThread(std::chrono::seconds(2));  // fault if thread stalls > 2s
```

The mechanism uses two `dmq::util::Timer` objects — no additional OS threads are created:

- **`m_threadTimer`** (fires every `timeout/4`): dispatches `ThreadCheck()` to the worker thread via `dmq::MakeDelegate`. When the worker processes it, `m_lastAliveTime` is updated. This also wakes an idle sleeping thread so it does not false-alarm.
- **`m_watchdogTimer`** (fires every `timeout/2`): calls `WatchdogCheck()` synchronously on the `dmq::util::Timer::ProcessTimers()` caller. If `now − m_lastAliveTime > timeout`, a fault is triggered.

```
ProcessTimers() caller (ISR or high-priority task)
   │
   ├─ m_threadTimer fires (timeout/4)
   │    └─ posts ThreadCheck() → worker thread queue
   │                                   └─ worker updates m_lastAliveTime ✓
   │
   └─ m_watchdogTimer fires (timeout/2)
        └─ WatchdogCheck() runs inline — compares now vs m_lastAliveTime
             └─ gap > timeout → @TODO: trigger fault/recovery
```

#### Priority Requirement — Critical on Single-Core RTOS

The watchdog **only catches CPU-spinning runaway threads** if `dmq::util::Timer::ProcessTimers()` runs at a **higher priority** than the threads it watches. On a single-core RTOS (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2), a runaway high-priority task starves all lower-priority tasks including the one calling `ProcessTimers()` — the watchdog timers never fire and the fault is never detected.

| Failure mode | ProcessTimers() priority requirement |
|:---|:---|
| Deadlocked thread (blocked on mutex/semaphore) | Any — blocked threads don't consume CPU |
| Idle thread (waiting for messages) | Any — `ThreadCheck` wakes it when scheduled |
| CPU-spinning runaway (infinite loop in callback) | **Must be higher** than the watched thread |

Two acceptable approaches for embedded targets:

**Option 1 — Hardware timer ISR (recommended):** Call `dmq::util::Timer::ProcessTimers()` from a SysTick or similar periodic ISR. ISRs preempt all tasks regardless of priority, so the watchdog fires even if the highest-priority task runs away.

```cpp
// FreeRTOS / CMSIS-RTOS2 example: SysTick ISR
extern "C" void SysTick_Handler(void)
{
    dmq::util::Timer::ProcessTimers();  // preempts all tasks — watchdog always fires
    // ... other SysTick work
}
```

**Option 2 — Highest-priority task:** Dedicate the system's highest-priority task to calling `dmq::util::Timer::ProcessTimers()` in a tight loop. This task must be strictly higher priority than any thread it is watching — a runaway task at equal or lower priority will starve it and defeat the watchdog.

```cpp
// FreeRTOS example: dedicated watchdog task at highest priority
void WatchdogTask(void*)
{
    while (true)
    {
        dmq::util::Timer::ProcessTimers();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms tick rate
    }
}

// At startup — assign priority above all worker threads
xTaskCreate(WatchdogTask, "Watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
```

Note: if user callbacks in the worker thread legitimately take longer than the watchdog timeout, the watchdog will false-alarm. Set the timeout to comfortably exceed the maximum expected callback duration.
