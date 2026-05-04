![License MIT](https://img.shields.io/github/license/BehaviorTree/BehaviorTree.CPP?color=blue)
[![conan Ubuntu](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_ubuntu.yml/badge.svg)](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_ubuntu.yml)
[![conan Clang](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_clang.yml/badge.svg)](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_clang.yml)
[![conan Windows](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_windows.yml/badge.svg)](https://github.com/DelegateMQ/DelegateMQ/actions/workflows/cmake_windows.yml)

# Delegates in C++

The DelegateMQ C++ library enables function invocations on any callable, either synchronously, asynchronously, or on a remote endpoint.

# Table of Contents

- [Delegates in C++](#delegates-in-c)
- [Table of Contents](#table-of-contents)
- [Introduction](#introduction)
- [Build](#build)
  - [Configuration and Overrides](#configuration-and-overrides)
    - [1. Command Line (Highest Precedence)](#1-command-line-highest-precedence)
    - [2. CMakeLists.txt](#2-cmakeliststxt)
    - [3. Auto-Detection (Default)](#3-auto-detection-default)
  - [Example Projects](#example-projects)
    - [Examples Setup](#examples-setup)
    - [Examples Build](#examples-build)
  - [Build Integration](#build-integration)
    - [CMake](#cmake)
    - [Generic (Make/IDE)](#generic-makeide)
- [Quick Start](#quick-start)
  - [Mental Model](#mental-model)
  - [Invocation Modes](#invocation-modes)
    - [Synchronous](#synchronous)
    - [Asynchronous — Non-Blocking (Fire-and-Forget)](#asynchronous--non-blocking-fire-and-forget)
    - [Asynchronous — Blocking (Wait for Result)](#asynchronous--blocking-wait-for-result)
    - [Lambdas](#lambdas)
  - [Publish / Subscribe with Signal](#publish--subscribe-with-signal)
  - [Remote Delegate](#remote-delegate)
  - [DataBus](#databus)
  - [Performance Monitoring](#performance-monitoring)
    - [Key Metrics](#key-metrics)
    - [dmq-thread Dashboard](#dmq-thread-dashboard)
    - [How it Works](#how-it-works)
  - [Async Public API Pattern](#async-public-api-pattern)
  - [Delegate Invocation Semantics](#delegate-invocation-semantics)
- [Porting Guide](#porting-guide)
- [Background](#background)
- [Usage](#usage)
  - [Delegates](#delegates)
  - [Delegate Containers](#delegate-containers)
  - [Synchronous Delegates](#synchronous-delegates)
  - [Asynchronous Delegates](#asynchronous-delegates)
    - [Non-Blocking](#non-blocking)
    - [Blocking](#blocking)
    - [Message Priority](#message-priority)
  - [Remote Delegates](#remote-delegates)
  - [Error Handling](#error-handling)
  - [Debug Logging](#debug-logging)
  - [Object Lifetime and Async Delegates](#object-lifetime-and-async-delegates)
    - [The Risk: Raw Pointers](#the-risk-raw-pointers)
    - [The Solution: Shared Pointers](#the-solution-shared-pointers)
  - [Register and Unregister](#register-and-unregister)
    - [Init/Term Pattern](#initterm-pattern)
    - [RAII Pattern](#raii-pattern)
    - [Object Lifetime Usage Guide](#object-lifetime-usage-guide)
- [Design Details](#design-details)
  - [Library Dependencies](#library-dependencies)
  - [Fixed-Block Memory Allocator](#fixed-block-memory-allocator)
  - [Function Argument Copy](#function-argument-copy)
  - [Caution Using `std::bind`](#caution-using-stdbind)
  - [Alternatives Considered](#alternatives-considered)
    - [`std::function`](#stdfunction)
    - [`std::async` and `std::future`](#stdasync-and-stdfuture)
    - [`DelegateMQ` vs. `std::async` Feature Comparisons](#delegatemq-vs-stdasync-feature-comparisons)
- [Library](#library)
  - [Heap Template Parameter Pack](#heap-template-parameter-pack)
    - [Argument Heap Copy](#argument-heap-copy)
    - [Bypassing Argument Heap Copy](#bypassing-argument-heap-copy)
    - [Array Argument Heap Copy](#array-argument-heap-copy)
- [Interfaces](#interfaces)
- [Cross-Language Interoperability](#cross-language-interoperability)
    - [Key Features:](#key-features)
    - [Synchronization](#synchronization)
- [Examples](#examples)
  - [Callback Example](#callback-example)
  - [Register Callback Example](#register-callback-example)
  - [Asynchronous API Examples](#asynchronous-api-examples)
    - [No Locks](#no-locks)
    - [Reinvoke](#reinvoke)
    - [Blocking Reinvoke](#blocking-reinvoke)
  - [Timer Example](#timer-example)
    - [Safe Timer (RAII)](#safe-timer-raii)
    - [Paced Timer Delegate](#paced-timer-delegate)
  - [`std::async` Thread Targeting Example](#stdasync-thread-targeting-example)
  - [More Examples](#more-examples)
  - [Sample Projects](#sample-projects)
    - [External Dependencies](#external-dependencies)
    - [system-architecture](#system-architecture)
    - [Interop Libraries](#interop-libraries)
    - [Sample Projects Comparison](#sample-projects-comparison)
      - [Feature \& Toolchain Demos](#feature--toolchain-demos)
      - [Remote Delegate Examples](#remote-delegate-examples)
- [Tests](#tests)
  - [Unit Tests](#unit-tests)
  - [Valgrind Memory Tests](#valgrind-memory-tests)
    - [Results Summary](#results-summary)
    - [Heap Memory Test Results](#heap-memory-test-results)
    - [Fixed-Block Memory Allocator Test Results](#fixed-block-memory-allocator-test-results)
- [Library Comparison](#library-comparison)
- [References](#references)

# Introduction

DelegateMQ is a C++ header-only library for invoking any callable (e.g., function, method, lambda):

- Synchronously
- Asynchronously
- Remotely across processes or processors

It unifies function calls across threads or systems via a simple delegate interface. DelegateMQ is thread-safe, unit-tested, and easy to port to any platform.

1. **Header-Only Library** – implemented using a small number of header files
2. **Any Compiler** – standard C++17 code for any compiler
3. **Any Function** – invoke any callable function: member, static, free, or lambda
4. **Any Argument Type** – supports any argument type: value, reference, pointer, pointer to pointer
5. **Multiple Arguments** – supports N number of function arguments for the bound function
6. **Synchronous Invocation** – call the bound function synchronously
7. **Asynchronous Invocation** – call the bound function asynchronously on a client specified thread
8. **Blocking Asynchronous Invocation** - invoke asynchronously using blocking or non-blocking delegates
9. **Remote Invocation** - call a function located on a remote endpoint.
11. **Smart Pointer Support** - bind an instance function using a raw object pointer or `std::shared_ptr`
12. **Lambda Support** - bind and invoke lambda functions asynchronously using delegates
13. **Automatic Heap Handling** – automatically copy argument data to the heap for safe transport through a message queue
14. **Memory Management** - support for global heap or fixed-block memory allocator
15. **Any OS** – easy porting to any OS. Windows, Linux, FreeRTOS, ThreadX, and other port included
16. **32/64-bit** - Support for 32 and 64-bit projects
17. **Dynamic Storage Allocation** - optional fixed-block memory allocator
18. **CMake Build** - CMake supports most toolchains including Windows, Linux, and others
19. **Unit Tests** - extensive unit testing of the delegate library included

# Build

To build and run DelegateMQ, follow these simple steps. The library uses <a href="https://www.cmake.org">CMake</a> to generate build files and supports Visual Studio, GCC, and Clang toolchains.
1. Clone the repository.
2. From the repository root, run the following CMake command:   
   `cmake -B build .`
3. Build and run the project within the `build` directory. 

## Configuration and Overrides

DelegateMQ is designed for "zero-config" out of the box. When you include `DelegateMQ.cmake`, it automatically detects your platform and selects sensible defaults for threading, transport, and serialization.

You can customize these behaviors using three methods (in order of precedence):

### 1. Command Line (Highest Precedence)
Override any setting directly when generating the build files:
`cmake -B build -DDMQ_THREAD=DMQ_THREAD_NONE -DDMQ_ALLOCATOR=ON .`

### 2. CMakeLists.txt
Set variables **before** including `DelegateMQ.cmake`:
```cmake
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
include("path/to/delegate-mq/DelegateMQ.cmake")
```

### 3. Auto-Detection (Default)
If no variables are set, DelegateMQ uses `Defaults.cmake` to guess the best settings:
- **Windows/Linux**: `STDLIB` threading, `UDP` transport.
- **RTOS (FreeRTOS/ThreadX/Zephyr)**: Native threading, `NONE` transport.
- **All Platforms**: `SERIALIZE` serialization, `dmq::databus::DataBus` enabled, `Allocator` disabled.

## Example Projects

Remote delegate example projects are located within the `example/sample-projects` directory and explained in [Sample Projects](#sample-projects). 

The [system-architecture](#system-architecture) project located within the `example/sample-projects/system-architecture-no-deps` directory is a client-server application with no external library dependencies. The app runs on Windows and Linux, showcasing various delegate-based design techniques, including remote communication, asynchronous callbacks/APIs, and more.

### Examples Setup

Most remote delegate example projects depend on external libraries. Scripts are used to automate setting up a sandbox test environment for remote delegate testing. 

1. Create a new workspace directory named `DelegateMQWorkspace`.
2. Clone the DelegateMQ repo to the `DelegateMQWorkspace\DelegateMQ` directory.  
   `git clone https://github.com/DelegateMQ/DelegateMQ.git`
3. Run `01_fetch_repos.py`. Fetches all dependent libraries from GitHub.
4. Run `02_build_libs.py`. Builds dependent libraries.
5. Run `03_generate_samples.py`. Creates project build files using CMake.
6. Run `04_build_samples.py`. Compiles all sample projects (Windows or Linux, platform-appropriate).
7. Run `05_run_samples.py`. Executes all built samples and reports pass/fail.

All dependent libraries—along with **DelegateMQ**—are now organized within a single parent directory. 

```text
DelegateMQWorkspace/
├── bitsery/
├── cereal/
├── DelegateMQ/
├── FreeRTOS/
├── ftxui/
├── libserialport/
├── msgpack-c/
├── mqtt/
├── nng/
├── rapidjson/
├── spdlog/
└── zeromq/
```

### Examples Build

Build and execute any remote delegate example within the `\DelegateMQ\example\sample-projects` directory. Each projects build files are located within a `build` subdirectory.

`DelegateMQ\example\sample-projects\zeromq-msgpack-cpp\build`

Client/server samples require running two applications.

`\DelegateMQ\example\sample-projects\system-architecture\client\build`  
`\DelegateMQ\example\sample-projects\system-architecture\server\build`

See [Sample Projects](#sample-projects) for details regarding each sample project.

## Build Integration

Follow these steps to integrate DelegateMQ into a project.

### CMake

DelegateMQ auto-selects sensible defaults for your platform. Simply include `DelegateMQ.cmake` to get started. See `Defaults.cmake` for the auto-selection logic and `Predef.cmake` for all available configuration variables.

```cmake
# Optional: Set DMQ build options to override defaults
set(DMQ_ASSERTS "ON")                      # ON for assert faults
set(DMQ_THREAD "DMQ_THREAD_STDLIB")        # Explicitly set thread library

# Include master delegate cmake build options
include("${CMAKE_SOURCE_DIR}/path/to/delegate-mq/DelegateMQ.cmake")
```

Update `External.cmake` external library paths if necessary.

Add `DMQ_PORT_SOURCES` to your sources if using the predefined supporting DelegateMQ classes (e.g. `dmq::os::Thread`, `dmq::Serializer`, ...).

```
# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})
```

Add external library include paths defined within `External.cmake` as necessary.

```
include_directories(    
    ${DMQ_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
)
```

Include `DelegateMQ.h` to use the delegate library features. Build and execute the project.

### Generic (Make/IDE)

Include `DelegateMQ.h` and select the DMQ build options via preprocessor definitions.

```cpp
// Define options globally in compiler settings (see DelegateMQ.h for all options)
// DMQ_THREAD_NONE
// DMQ_SERIALIZE_NONE
// DMQ_TRANSPORT_NONE
// DMQ_ASSERTS

#include "DelegateMQ.h"
using namespace dmq;

// Your DelegateMQ code...
```

**Note:** If using utility features (like `dmq::os::Thread` or `dmq::util::Timer`), ensure you compile and link the corresponding `.cpp` files found in the `delegate-mq/port` and `delegate-mq/extras` directories.

# Quick Start

DelegateMQ has **three invocation modes**. The right one depends entirely on where the target function runs:

| Mode | When to use | How to create |
| --- | --- | --- |
| **Synchronous** | Target is on the same thread as the caller | `dmq::MakeDelegate(&obj, &Class::Func)` |
| **Asynchronous** | Target must run on a specific worker thread | `dmq::MakeDelegate(&obj, &Class::Func, thread)` |
| **Remote** | Target is on a different process or processor | `dmq::RemoteChannel<Sig>` + `Bind()` |

The key insight: **adding a `thread` argument to `dmq::MakeDelegate()` is the only difference between synchronous and asynchronous**. Everything else — the call syntax, argument types, return values — is identical.

See [Sample Projects](#sample-projects) for complete working examples.

---

## Mental Model

Three questions drive every DelegateMQ design decision.

**1. Where does the target function run?**

```
Same thread    → dmq::Delegate<>           sync, direct call
Worker thread  → dmq::DelegateAsync<>      fire-and-forget
               → dmq::DelegateAsyncWait<>  blocking, returns value
Remote system  → dmq::DelegateRemote<>     cross-process / cross-processor
```

`dmq::MakeDelegate()` creates all of the above — only the arguments change. There is no separate API to learn for each mode.

**2. How many targets receive the call?**

| Targets | Type | Notes |
| --- | --- | --- |
| One | `dmq::Delegate<>` / `dmq::UnicastDelegate<>` | Direct call or single-slot event |
| Many | `dmq::Signal<Sig>` | Preferred — RAII lifetime via `dmq::ScopedConnection` |
| Many | `dmq::MulticastDelegateSafe<Sig>` | Manual add/remove; no RAII |

See [SIGNALS.md](SIGNALS.md) for the full decision matrix between signals and multicast delegates.

**3. Does the target need to outlive the caller?**

Async delegates optionally hold a `weak_ptr` to the target object. If the object is destroyed before the queued call executes, the call is silently dropped — no crash, no dangling pointer. Use `std::shared_ptr` / `std::enable_shared_from_this` for this protection. See [Object Lifetime and Async Delegates](#object-lifetime-and-async-delegates).

---

## Invocation Modes

### Synchronous

Invoke a function directly on the calling thread. No threading infrastructure needed.

```cpp
void Log(const std::string& msg) {
    std::cout << msg << std::endl;
}

class Sensor {
public:
    float Read() { return 42.0f; }
};

// Bind to a free function and invoke
auto logDelegate = dmq::MakeDelegate(&Log);
logDelegate("System started");

// Bind to a member function and invoke
Sensor sensor;
auto readDelegate = dmq::MakeDelegate(&sensor, &Sensor::Read);
float value = readDelegate();
```

### Asynchronous — Non-Blocking (Fire-and-Forget)

Add a `thread` argument. The function is queued onto that thread and the caller returns immediately. No return value is available.

```cpp
dmq::os::Thread workerThread("Worker");
workerThread.CreateThread();

// Invoke Log() on workerThread — caller does not wait
auto asyncLog = dmq::MakeDelegate(&Log, workerThread);
asyncLog("Processing started");  // returns immediately
```

### Asynchronous — Blocking (Wait for Result)

Add a thread and a timeout. The caller blocks until the target function completes and the return value is available.

```cpp
// Block until Read() completes on workerThread, then get the return value
auto syncRead = dmq::MakeDelegate(&sensor, &Sensor::Read, workerThread, dmq::WAIT_INFINITE);
float value = syncRead();  // blocks until workerThread executes Read()

// With a timeout — use AsyncInvoke to get an optional return value
auto timedRead = dmq::MakeDelegate(&sensor, &Sensor::Read, workerThread, std::chrono::milliseconds(100));
auto result = timedRead.AsyncInvoke();
if (result.has_value())
    float v = result.value();  // completed within 100ms
```

### Lambdas

Lambdas work the same way. Pass any lambda directly to `dmq::MakeDelegate()` — no `std::function` wrapper or `+` conversion needed.

```cpp
// Stateless lambda — synchronous
auto echo = dmq::MakeDelegate([](const std::string& s) { std::cout << s; });
echo("hello");

// Capturing lambda — asynchronous, fire-and-forget
int threshold = 10;
auto check = dmq::MakeDelegate(
    [threshold](int v) {
        if (v > threshold) std::cout << "Over threshold\n";
    },
    workerThread
);
check(15);
```

---

## Publish / Subscribe with Signal

> Full documentation: **[SIGNALS.md](SIGNALS.md)**

`dmq::Signal<Sig>` is the recommended way to implement publish/subscribe within a process. It returns a `dmq::ScopedConnection` from `Connect()` that automatically disconnects when it goes out of scope. Each subscriber independently chooses its execution context — synchronous or on a specific worker thread — without any changes to the publisher.

See [SIGNALS.md](SIGNALS.md) for complete examples, lambda slots, and a comparison with `dmq::MulticastDelegateSafe`.

---

## Remote Delegate

Remote delegates send a function call across a process or network boundary. The receiver doesn't need to know a call is coming over a transport — it just implements a normal member function.

**Three concepts to understand:**
- **`dmq::RemoteChannel<Sig>`** — one instance per message signature; owns the transport wiring.
- **`Bind(obj, func, id)`** — connects a member function to a remote ID on the channel.
- **`dmq::RegisterEndpoint(id, channel.GetEndpoint())`** — tells the receive side which function to call when a message with that ID arrives.

```cpp
// --- Shared message ID (known to both sender and receiver) ---
constexpr dmq::DelegateRemoteId TEMPERATURE_ID = 1;

// --- Receiver side ---
class DataLogger
{
public:
    DataLogger(dmq::transport::ITransport& transport, dmq::ISerializer<void(float)>& ser)
    {
        m_channel.emplace(transport, ser);
        m_channel->Bind(this, &DataLogger::OnTemperature, TEMPERATURE_ID);
        dmq::RegisterEndpoint(TEMPERATURE_ID, m_channel->GetEndpoint());
    }

private:
    void OnTemperature(float value)
    {
        std::cout << "Received temperature: " << value << "\n";
    }

    std::optional<dmq::RemoteChannel<void(float)>> m_channel;
};

// --- Sender side ---
class Thermometer
{
public:
    Thermometer(dmq::transport::ITransport& transport, dmq::ISerializer<void(float)>& ser)
    {
        m_channel.emplace(transport, ser);
        // Bind() wires up the remote ID, dispatcher, serializer, and stream.
        // The bound function is never called on the sender side; it is a
        // required placeholder so Bind() can configure the channel.
        m_channel->Bind(this, &Thermometer::Unused, TEMPERATURE_ID);
    }

    // Fire-and-forget send (operator() on the optional<dmq::RemoteChannel>)
    void Send(float value) { (*m_channel)(value); }

    // Blocking send — waits for ACK or timeout
    bool SendWait(float value) { return dmq::RemoteInvokeWait(*m_channel, value); }

private:
    void Unused(float) {}  // required by Bind(); never invoked on the sender side
    std::optional<dmq::RemoteChannel<void(float)>> m_channel;
};
```

The sender calls `(*m_channel)(value)` or `dmq::RemoteInvokeWait(*m_channel, value)`. The transport serializes the argument, sends it, and on the receiver side `OnTemperature()` is called — just like a normal function. See [Sample Projects](#sample-projects) for complete working examples with real transports.

---

## DataBus

> Full documentation: **[DATABUS.md](DATABUS.md)**

`dmq::databus::DataBus` is a high-level middleware built on top of DelegateMQ's core delegates. It provides a topic-based data distribution system (DDS Lite) that works transparently across threads and remote nodes.

See [DATABUS.md](DATABUS.md) for the full API reference, threading model, QoS options (Last Value Cache, Lifespan, Min Separation), remote distribution patterns, and multi-node topology examples.

---

## Performance Monitoring

DelegateMQ includes high-resolution performance monitoring built directly into the `dmq::os::Thread` port layer. This instrumentation tracks how long every delegate message waits in a thread's queue before being processed, enabling real-time detection of thread contention, jitter, and system bottlenecks.

### Key Metrics

*   **LAvg (Latency Average)**: The average time (in milliseconds) messages spent in the queue during the last 1-second window. This reflects the thread's current load.
*   **LWin (Latency Window Max)**: The worst-case latency recorded during the last 1-second window. Ideal for catching momentary "stutters" or long-running handlers.
*   **LMax (Latency All-Time Max)**: The absolute worst-case latency seen since the application started. This value never resets and represents the system's lifetime "Worst Case Execution Time" (WCET) overhead.

### dmq-thread Dashboard

The metrics are visualized in the `dmq-thread` dashboard, which aggregates telemetry from all nodes on the network.

<img src="../tools/dmq-thread-screenshot.png" alt="DelegateMQ Thread Monitor Screenshot" style="max-width: 800px; width: 100%;">

### How it Works

1.  **Source Instrumentation**: Every `dmq::os::Thread` port (Win32, FreeRTOS, etc.) captures an enqueue timestamp and calculates wait time during dispatch.
2.  **Telemetry Service** (`dmq::util::ThreadMonitor`): A service within the application that polls registered threads at 1Hz, captures atomic snapshots of their metrics, and publishes them as a `ThreadStatsPacket` to the local DataBus.
3.  **Distributed Visualization**: The standard `NodeBridge` automatically picks up these packets and broadcasts them to any listening `dmq-thread` console on the network.

See [tools/TOOLS.md](../tools/TOOLS.md) for detailed integration steps.

---

## Async Public API Pattern

A common pattern in active-object classes: a public method that is safe to call from any thread by re-invoking itself on the object's internal thread if needed.

```cpp
class DataStore
{
public:
    DataStore() : m_thread("DataStoreThread") { m_thread.CreateThread(); }

    void Save(const Data& data)
    {
        // If called from the wrong thread, re-invoke on m_thread (non-blocking)
        if (dmq::os::Thread::GetCurrentThreadId() != m_thread.GetThreadId()) {
            dmq::MakeDelegate(this, &DataStore::Save, m_thread)(data);
            return;
        }
        // Now guaranteed to run on m_thread
        m_data = data;
    }

private:
    Data m_data;
    dmq::os::Thread m_thread;
};
```

Callers invoke `Save()` without knowing or caring which thread they're on — thread safety is enforced inside the class.

## Delegate Invocation Semantics

Target callable invocation and argument handling based on the delegate type.

| Feature | Synchronous | Asynchronous (Non-Blocking) | Asynchronous (Blocking) | Remote (Network) |
| --- | --- | --- | --- | --- |
| Overview | Invokes bound callable directly on caller's thread. | Target thread invokes bound callable; caller continues immediately. | Target thread invokes bound callable; caller blocks until complete or timeout. | Remote system invokes bound callable; caller continues immediately. |
| Dispatch Mechanism | Direct Call. | Message Queue. | Message Queue. | Network Transport. |
| Synchronization | Caller is blocked by synchronous execution. | None. Fire-and-forget. | Semaphore + Mutex. Caller blocks on semaphore; mutex protects return value. | None. Fire-and-forget (library level). Blocking depends on transport implementation. | 
| Execution Thread | Caller Thread. | Target Thread. | Target Thread. | Remote Process. |
| Blocking Behavior¹ | Yes. | No. | Yes. | No. |
| Arguments (In) | Stack Access. Passed directly via registers/stack. | Deep Heap Copy. Arguments cloned to heap. | Stack Reference. References point to caller's stack (safe because caller blocks). | Serialization. Arguments marshaled to a stream and sent via external transport.|
| Arguments (Out) | Supported. | Not Supported.	| Supported. Target writes directly to caller's stack reference variables. | Not supported. |
| Return Value | Yes. Immediate. | No. Ignored. | Yes. Returned after target callable completes. | No. Not supported. |

¹ Yes means caller blocks until the bound target callable completes.

# Porting Guide

> Full documentation: **[PORTING.md](PORTING.md)**

Numerous predefined platforms are already supported — Windows, Linux, FreeRTOS, ARM bare-metal, ThreadX, Zephyr, CMSIS-RTOS2, and Qt. See [PORTING.md](PORTING.md) for the full porting checklist, embedded systems notes, and interface implementation guides (`dmq::IThread`, `dmq::ISerializer`, `dmq::IDispatcher`).

# Background

If you're not familiar with a delegate, the concept is quite simple. A delegate can be thought of as a super function pointer. In C++, there 's no pointer type capable of pointing to all the possible function variations: instance member, virtual, const, static, free (global), and lambda. A function pointer can't point to instance member functions, and pointers to member functions have all sorts of limitations. However, delegate classes can, in a type-safe way, point to any function provided the function signature matches. In short, a delegate points to any function with a matching signature to support anonymous function invocation.

In practice, while a delegate is useful, a multicast version significantly expands its utility. The ability to bind more than one function pointer and sequentially invoke all registrars' makes for an effective publisher/subscriber mechanism. Publisher code exposes a delegate container and one or more anonymous subscribers register with the publisher for callback notifications.

The problem with callbacks on a multithreaded system, whether it be a delegate-based or function pointer based, is that the callback occurs synchronously. Care must be taken that a callback from another thread of control is not invoked on code that isn't thread-safe. Multithreaded application development is hard. It 's hard for the original designer; it 's hard because engineers of various skill levels must maintain the code; it 's hard because bugs manifest themselves in difficult ways. Ideally, an architectural solution helps to minimize errors and eases application development.

A remote delegate takes the concept further and allows invoking an endpoint function located in a separate process or processor. Extremely configurable using custom serializer and dispatcher interfaces to store callable argument data and send to a remote system.

# Usage

The DelegateMQ library is comprised of delegates and delegate containers. 

## Delegates 

A delegate binds to a single callable function. The delegate classes are:

```cpp
// Delegates
dmq::DelegateBase
    dmq::Delegate<>
        dmq::DelegateFree<>
            dmq::DelegateFreeAsync<>
            dmq::DelegateFreeAsyncWait<>
            dmq::DelegateFreeRemote<>
        dmq::DelegateMember<>
            dmq::DelegateMemberAsync<>
            dmq::DelegateMemberAsyncWait<>
            dmq::DelegateMemberRemote<>
        dmq::DelegateMemberSp<>
            dmq::DelegateMemberAsyncSp<>
            dmq::DelegateMemberAsyncWaitSp<>
        dmq::DelegateFunction<>
            dmq::DelegateFunctionAsync<>
            dmq::DelegateFunctionAsyncWait<>
            dmq::DelegateFunctionRemote<>

// Interfaces
dmq::IDispatcher
dmq::ISerializer
dmq::IThread
dmq::IThreadInvoker
dmq::IRemoteInvoker
``` 

`dmq::DelegateFree<>` binds to a free or static member function. `dmq::DelegateMember<>` binds to a class instance member function. `dmq::DelegateFunction<>` binds to a `std::function` target. All versions offer synchronous function invocation.

`dmq::DelegateFreeAsync<>`, `dmq::DelegateMemberAsync<>` and `dmq::DelegateFunctionAsync<>` operate in the same way as their synchronous counterparts; except these versions offer non-blocking asynchronous function execution on a specified thread of control. `dmq::IThread` and `dmq::IThreadInvoker` interfaces to send messages integrates with any OS.

`dmq::DelegateFreeAsyncWait<>`, `dmq::DelegateMemberAsyncWait<>` and `dmq::DelegateFunctionAsyncWait<>` provides blocking asynchronous function execution on a target thread with a caller supplied maximum wait timeout. The destination thread will not invoke the target function if the timeout expires.

`dmq::DelegateFreeRemote<>`, `dmq::DelegateMemberRemote<>` and `dmq::DelegateFunctionRemote<>` provides non-blocking remote function execution. `dmq::ISerializer` and `dmq::IRemoteInvoker` interfaces support integration with any system. 

The template-overloaded `dmq::MakeDelegate()` helper function eases delegate creation.

## Delegate Containers

A delegate container stores one or more delegates. A delegate container is callable and invokes all stored delegates sequentially. A unicast delegate container holds at most one delegate.

```cpp
// Delegate Containers
dmq::UnicastDelegate<>
    dmq::UnicastDelegateSafe<>
dmq::MulticastDelegate<>
    dmq::MulticastDelegateSafe<>
dmq::Signal<>          // thread-safe multicast with RAII ScopedConnection
```

`dmq::UnicastDelegate<>` is a delegate container accepting a single delegate.

`dmq::MulticastDelegate<>` is a delegate container accepting multiple delegates.

`dmq::MulticastDelegateSafe<>` is a thread-safe container accepting multiple delegates. Always use the thread-safe version if multiple threads access the container instance.

`dmq::Signal<>` is a thread-safe multicast container with RAII connection management. `Connect()` returns a `dmq::ScopedConnection` handle that automatically unsubscribes the delegate when it goes out of scope, preventing callbacks to destroyed objects. `dmq::Signal<>` may be used as a plain stack variable or class member — no `shared_ptr` required.

## Synchronous Delegates

Delegates can be created with the overloaded `dmq::MakeDelegate()` template function. For example, a simple free function.

```cpp
void FreeFuncInt(int value) {
      cout << "FreeCallback " << value << endl;
}
```

Bind a free function to a delegate and invoke.

```cpp
// Create a delegate bound to a free function then invoke
auto delegateFree = dmq::MakeDelegate(&FreeFuncInt);
delegateFree(123);
```

Bind a member function with two arguments to `dmq::MakeDelegate()`: the class instance and member function pointer. 

```cpp
// Create a delegate bound to a member function then invoke    
auto delegateMember = dmq::MakeDelegate(&testClass, &TestClass::MemberFunc);    
delegateMember(&testStruct);
```

Bind a lambda directly — no `std::function` wrapper needed.

```cpp
auto rangeLambda = dmq::MakeDelegate([](int v) { return v > 2 && v <= 6; });
bool inRange = rangeLambda(6);
```

A delegate container holds one or more delegates. 

```cpp
dmq::MulticastDelegate<void(int)> delegateA;
dmq::MulticastDelegate<void(int, int)> delegateD;
dmq::MulticastDelegate<void(float, int, char)> delegateE;
dmq::MulticastDelegate<void(const MyClass&, MyStruct*, Data**)> delegateF;
```

Creating a delegate instance and adding it to the multicast delegate container.

```cpp
delegateA += dmq::MakeDelegate(&FreeFuncInt);
```

An instance member function can also be added to any delegate container.

```cpp
delegateA += dmq::MakeDelegate(&testClass, &TestClass::MemberFunc);
```

Invoke callbacks for all registered delegates. If multiple delegates are stored, each one is called sequentially.

```cpp
// Invoke the delegate target functions
delegateA(123);
```

Removing a delegate instance from the delegate container.

```cpp
delegateA -= dmq::MakeDelegate(&FreeFuncInt);
```

Alternatively, `Clear()` is used to remove all delegates within the container.

```cpp
delegateA.Clear();
```

A delegate is added to the unicast container `operator=`.

```cpp
dmq::UnicastDelegate<int(int)> delegateF;
delegateF = dmq::MakeDelegate(&FreeFuncIntRetInt);
```

Removal is with `Clear()` or assign `nullptr`.

```cpp
delegateF.Clear();
delegateF = nullptr;
```
## Asynchronous Delegates

An asynchronous delegate invokes a callable on a user-specified thread of control. Each invoked asynchronous delegate is copied into the destination message queue with an optional message priority.

### Non-Blocking

Create an asynchronous delegate by adding an extra thread argument to `dmq::MakeDelegate()`.

```cpp
dmq::os::Thread workerThread1("WorkerThread1");
workerThread.CreateThread();

// Create delegate and invoke FreeFuncInt() on workerThread
// Does not wait for function call to complete
auto delegateFree = dmq::MakeDelegate(&FreeFuncInt, workerThread);
delegateFree(123);
```

If the target function has a return value, it is not valid after invoke. The calling function does not wait for the target function to be called.

An asynchronous delegate instance can also be inserted into a delegate container. 

```cpp
dmq::MulticastDelegateSafe<void(TestStruct*)> delegateC;
delegateC += dmq::MakeDelegate(&testClass, &TestClass::MemberFunc, workerThread1);
delegateC(&testStruct);
```

Another example of an asynchronous delegate being invoked on `workerThread1` with `std::string` and `int` arguments.

```cpp
// Create delegate with std::string and int arguments then asynchronously    
// invoke on a member function
dmq::MulticastDelegateSafe<void(const std::string&, int)> delegateH;
delegateH += dmq::MakeDelegate(&testClass, &TestClass::MemberFuncStdString, workerThread1);
delegateH("Hello world", 2020);
```

### Blocking

Create an asynchronous blocking delegate by adding an thread and timeout arguments to `dmq::MakeDelegate()`.

```cpp
dmq::os::Thread workerThread1("WorkerThread1");
workerThread.CreateThread();

// Create delegate and invoke FreeFuncInt() on workerThread 
// Waits for the function call to complete
auto delegateFree = dmq::MakeDelegate(&FreeFuncInt, workerThread, dmq::WAIT_INFINITE);
delegateFree(123);
```

A blocking delegate waits until the target thread executes the bound delegate function. The function executes just as you'd expect a synchronous version including incoming/outgoing pointers and references.

Stack arguments passed by pointer/reference do not be thread-safe. The reason is that the calling thread blocks waiting for the destination thread to complete. The delegate implementation guarantees only one thread is able to access stack allocated argument data.

A blocking delegate must specify a timeout in milliseconds or `dmq::WAIT_INFINITE`. Unlike a non-blocking asynchronous delegate, which is guaranteed to be invoked, if the timeout expires on a blocking delegate, the function is not invoked. Use `IsSuccess()` to determine if the delegate succeeded or not.

```cpp
// Asynchronously invoke lambda on workerThread1 and wait for the return value
auto lambdaDelegate1 = dmq::MakeDelegate(
    [](int i) -> int {
        cout << "Called LambdaFunc1 " << i << std::endl;
        return ++i;
    },
    workerThread1, dmq::WAIT_INFINITE
);
int lambdaRetVal2 = lambdaDelegate1(123);
```

Delegates are callable and therefore may be passed to the standard library. The example below shows `CountLambda` executed asynchronously on `workerThread1` by `std::count_if`.

```cpp
std::vector<int> v{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };

auto CountLambda = [](int v) -> int
{
    return v > 2 && v <= 6;
};
auto countLambdaDelegate = dmq::MakeDelegate(CountLambda, workerThread1, dmq::WAIT_INFINITE);

const auto valAsyncResult = std::count_if(v.begin(), v.end(),
    countLambdaDelegate);
cout << "Asynchronous lambda result: " << valAsyncResult << endl;
```

The delegate library supports binding with a object pointer and a `std::shared_ptr` smart pointer. Use a `std::shared_ptr` in place of the raw object pointer in the call to `dmq::MakeDelegate()`.

```cpp
// Create a shared_ptr, create a delegate, then synchronously invoke delegate function
std::shared_ptr<TestClass> spObject(new TestClass());
auto delegateMemberSp = dmq::MakeDelegate(spObject, &TestClass::MemberFuncStdString);
delegateMemberSp("Hello world using shared_ptr", 2020);
```

### Message Priority

Asynchronous delegate priority determines the dispatch order when multiple asynchronous delegates are pending in the message queue.

```cpp
// Async delegate message priority
enum class Priority
{
	LOW,
	NORMAL,
	HIGH
};
```

Use `SetPriority()` to adjust the delegate priority level.

```cpp
auto alarmDel = dmq::MakeDelegate(this, &AlarmMgr::RecvAlarmMsg, m_thread);
alarmDel.SetPriority(dmq::Priority::HIGH);  // Alarm messages high priority
NetworkMgr::AlarmMsgCb += alarmDel;
```

## Remote Delegates

A remote delegate asynchronously invokes a remote target function. The sender must implement the `dmq::ISerializer` and `dmq::IDispatcher` interfaces:

```cpp
template <class R>
struct ISerializer; // Not defined

/// @brief Delegate serializer interface for serializing and deserializing
/// remote delegate arguments. 
template<class RetType, class... Args>
class ISerializer<RetType(Args...)>
{
public:
    /// Inheriting class implements the write function to serialize
    /// data for transport. 
    /// @param[out] os The output stream
    /// @param[in] args The target function arguments 
    /// @return The input stream
    virtual std::ostream& Write(std::ostream& os, Args... args) = 0;

    /// Inheriting class implements the read function to unserialize data
    /// from transport. 
    /// @param[in] is The input stream
    /// @param[out] args The target function arguments 
    /// @return The input stream
    virtual std::istream& Read(std::istream& is, Args&... args) = 0;
};

/// @brief Delegate interface class to dispatch serialized function argument data
/// to a remote destination. 
class IDispatcher
{
public:
    /// Dispatch a stream of bytes to a remote system. The implementer is responsible
    /// for sending the bytes over a communication link. 
    /// @param[in] os An outgoing stream to send to the remote destination.
    virtual int Dispatch(std::ostream& os) = 0;
};
```

The sender sends the remote delegate. All function argument data is serialized and sent using the dispatcher.

```cpp
// Dispatcher and serializer instances
dmq::util::Dispatcher dispatcher;
dmq::serialization::serializer::Serializer<void(int)> serializer;

// Send delegate and registers dispatcher and serializer
dmq::DelegateFreeRemote<void(int)> delegateRemote(REMOTE_ID);
delegateRemote.SetDispatcher(&dispatcher);
delegateRemote.SetSerializer(&serializer);

// Send invokes the remote function using the serializer and dispatcher interfaces
delegateRemote(123);
```

The receiver receives the serialized function argument data bytes and invokes the target function.

```cpp
// Remote target function
void FreeFuncInt(int i) { std::cout << i; }

// Serializer used to deserialize incoming data
dmq::serialization::serializer::Serializer<void(int)> serializer;

// Receiver delegate and registers serializer
dmq::DelegateFreeRemote<void(int)> delegateRemote(FreeFuncInt, REMOTE_ID);
delegateRemote.SetSerializer(&serializer);

// TODO: Receiver code obtains the serializers incoming argument data
// from your application-specific reception code and stored within a stream.
std::istream& recv_stream;

// Receiver invokes the remote target function
delegateRemote.Invoke(recv_stream);
```
## Error Handling

The DelegateMQ library uses dynamic memory to send asynchronous delegate messages to the target thread. By default, out-of-memory failures throw a `std::bad_alloc` exception. Optionally, if `DMQ_ASSERTS` is defined, exceptions are not thrown, and an assert is triggered instead. See `DelegateOpt.h` for more details.

Remote delegate error handling is captured by registering a callback with  `SetErrorHandler()`. A transport monitor (`dmq::transport::ITransportMonitor`) is optional and provides message timeout callbacks using message sequence numbers and acknowledgments.

## Debug Logging 

Optionally enable debug log output using [spdlog](https://github.com/gabime/spdlog) C++ logging library. Enabled within CMake:

```text
set(DMQ_LOG "ON")
```

Runtime log macro output:

```cpp
LOG_INFO("Dispatcher::Dispatch id={} seqNum={} err={}", header.GetId(), header.GetSeqNum(), err);
```

Log output can be directed to multiple sinks, such as files, consoles, or custom handlers.

```text
[2025-05-29 12:47:13.066] [msvc_logger] [info] dmq::os::Thread::CreateThread dmq::transport::Win32UdpTransport
[2025-05-29 12:47:13.066] [msvc_logger] [info] dmq::os::Thread::Process NetworkMgr
[2025-05-29 12:47:13.066] [msvc_logger] [info] dmq::os::Thread::CreateThread NetworkMgr
[2025-05-29 12:47:13.067] [msvc_logger] [info] dmq::os::Thread::DispatchDelegate
   thread=NetworkMgr
   target=class dmq::DelegateMemberAsyncWait<class NetworkMgr,int __cdecl(void)>
[2025-05-29 12:47:13.067] [msvc_logger] [info] dmq::os::Thread::DispatchDelegate
   thread=dmq::transport::Win32UdpTransport
   target=class dmq::DelegateMemberAsyncWait<class dmq::transport::Win32UdpTransport,int __cdecl(enum dmq::transport::Win32UdpTransport::Type,char const * __ptr64,unsigned short)>
'delegate_client_app.exe' (Win32): Loaded 'C:\Windows\System32\mswsock.dll'. Symbol loading disabled by Include/Exclude setting.
[2025-05-29 12:47:13.069] [msvc_logger] [info] dmq::os::Thread::DispatchDelegate
   thread=dmq::transport::Win32UdpTransport
   target=class dmq::DelegateMemberAsyncWait<class dmq::transport::Win32UdpTransport,int __cdecl(enum dmq::transport::Win32UdpTransport::Type,char const * __ptr64,unsigned short)>
```
## Object Lifetime and Async Delegates

### The Risk: Raw Pointers

Invoking an asynchronous delegate on a raw pointer (`T*`) is dangerous. If the target object is deleted before the worker thread processes the message, the application will crash (Use-After-Free).

```cpp
// DANGER: If workerThread is slow, the object dies before the callback runs.
TestClass* rawObj = new TestClass();
auto unsafeDel = dmq::MakeDelegate(rawObj, &TestClass::Func, workerThread);
unsafeDel("Crash waiting to happen!"); 
delete rawObj; // Object destroyed immediately
```

### The Solution: Shared Pointers

When you bind a delegate using `std::shared_ptr`, the delegate stores a `std::weak_ptr` internally. This provides automatic safety without forcibly extending the object's lifetime:

1. No Reference Cycles: The delegate does not keep the object alive artificially.
2. Thread Safety: When the worker thread picks up the message, it attempts to lock the weak pointer.  
a. If the object is Alive: The function executes normally.  
b. If the object is Dead: The callback is safely ignored (No-Op).

```cpp
// SAFE: The delegate holds a weak reference.
auto safeObj = std::make_shared<TestClass>();
auto safeDel = dmq::MakeDelegate(safeObj, &TestClass::Func, workerThread);

safeDel("This works!");

// If we destroy the object while the message is still in the queue...
safeObj.reset(); 

// ...the worker thread detects the dead object later and skips execution.
// Result: No crash, no leak.
```

## Register and Unregister

When using `std::shared_ptr` and `std::enable_shared_from_this`, you must follow a specific pattern to manually register and unregister delegates.

Critical Rule: You cannot call `shared_from_this()` inside a destructor. Doing so causes a `std::bad_weak_ptr` crash because the ownership of the object has already expired. Therefore, if you require manual unregistration (to stop receiving events immediately), you must use an explicit `Init`/`Term` or RAII pattern. Alternatively, use `dmq::Signal` (with `dmq::ScopedConnection`) for automatic RAII connection management.

### Init/Term Pattern

`Init()`: Called after the object is fully constructed and owned by a `shared_ptr`. Registers the delegate.

`Term()`: Called explicitly before the object goes out of scope. Unregisters the delegate while `shared_from_this()` is still valid.

```cpp
class SysDataClient : public std::enable_shared_from_this<SysDataClient>
{
public:
    // 1. Registration
    // Call this after creating the shared_ptr<SysDataClient>
    void Init()
    {
        // Register for callbacks using shared_from_this()
        SysData::GetInstance().SystemModeChangedDelegate += 
            dmq::MakeDelegate(shared_from_this(), &SysDataClient::CallbackFunction, workerThread1);
    }

    // 2. Unregistration
    // Call this explicitly BEFORE the object is destroyed
    void Term() 
    {
        // Unsubscribe safely while "this" is still valid
        SysData::GetInstance().SystemModeChangedDelegate -= 
            dmq::MakeDelegate(shared_from_this(), &SysDataClient::CallbackFunction, workerThread1);
    }

    // 3. Destructor
    ~SysDataClient()
    {
        // WARNING: Do NOT attempt to unregister here using shared_from_this().
        // It will cause a crash (std::bad_weak_ptr).
    }

private:
    void CallbackFunction(const SystemModeChanged& data) { /* ... */ }
};

// Usage Example
void RunClient()
{
    // Create the client
    auto client = std::make_shared<SysDataClient>();
    
    // Register
    client->Init();

    // ... Application runs ...

    // Unregister (Clean up)
    client->Term();

    // Destroy
    client.reset(); 
}
```

### RAII Pattern 

Alternative solution does not require calling `Term()`. 

```cpp
class Subscriber : public std::enable_shared_from_this<Subscriber>
{
public:
    Subscriber() : m_thread("SubscriberThread") {
        m_thread.CreateThread();
    }

    // 1. Init: Call this immediately after std::make_shared<Subscriber>
    void Init() {
        // Create the delegate once and STORE it.
        // We pass a shared_ptr, but the delegate internally converts and 
        // stores it as a weak_ptr to prevent circular reference cycles.
        m_delegate = dmq::MakeDelegate(
            shared_from_this(),
            &Subscriber::HandleMsgCb,
            m_thread
        );

        // Register using the member variable
        Publisher::Instance().MsgCb += m_delegate;
    }

    ~Subscriber() {
        // 2. Unregister using the stored member.
        // This works safely even in the destructor because the stored delegate
        // preserves the identity required for removal.
        Publisher::Instance().MsgCb -= m_delegate;
    }

private:
    void HandleMsgCb(const std::string& msg) { 
        std::cout << msg << std::endl; 
    }

    dmq::os::Thread m_thread;

    // Store the delegate to allow safe unregistration in the destructor.
    // 'Sp' suffix indicates this delegate is specialized for smart pointers.
    dmq::DelegateMemberAsyncSp<Subscriber, void(const std::string&)> m_delegate;
};
```

### Object Lifetime Usage Guide

This table outlines when it is safe to use raw pointers (`this`) during delegate registration versus when you must use `std::shared_ptr` (`shared_from_this()`) to prevent crashes.

| Delegate Type | Behavior | Safe to use `this`? | Explanation |
| --- | --- | --- | --- |
| Synchronous | Function is called immediately on the current thread.| YES | The caller waits for the callback to complete. It is impossible for the object to be destroyed while the callback is running. Must unregister in destructor. |
| Async (Non-Blocking) | "Fire and Forget." Message posted to target thread queue. Caller continues immediately. | NO | Unsafe. Even if you unregister in the destructor, a message may already be pending in the queue. If the object dies before the queue is processed, the target thread will access freed memory. Use `shared_from_this()`. |
| Async Blocking (`dmq::WAIT_INFINITE`) | Caller thread blocks until the target thread executes the function. | YES | Because the caller is blocked, the object (owned by the caller) cannot go out of scope or be destroyed until the callback finishes. |
| Async Blocking (Timeout) | Caller thread blocks until success OR timeout expires. | NO | Unsafe. If the timeout expires, the caller proceeds and may destroy the object. However, the message is still in the queue. When the target thread eventually processes it, it will crash. Use `shared_from_this()`. |
| Async (Singleton / Global) | Object lifetime exceeds the thread lifetime (e.g., Singleton, Static, or Global). | YES | Safe. Since the object is guaranteed to exist for the entire duration of the application (or until after the worker thread is destroyed), the pointer will never be invalid. |

# Design Details

## Library Dependencies

The `DelegateMQ` library external dependencies are based upon on the intended use. Interfaces provide the delegate library with platform-specific features to ease porting to a target system. Complete example code offer ready-made solutions or create your own.

| Class | Interface | Usage | Notes
| --- | --- | --- | --- |
| `dmq::Delegate` | n/a | Synchronous Delegates | No interfaces; use as-is without external dependencies.
| `dmq::DelegateAsync`<br>`dmq::DelegateAsyncWait` | `dmq::IThread` | Asynchronous Delegates | `dmq::IThread` used to send a delegate and argument data through an OS message queue.
| `dmq::DelegateRemote` | `dmq::ISerializer`<br>`dmq::IDispatcher`<br>`dmq::IDispatchMonitor` | Remote Delegates | `dmq::ISerializer` used to serialize callable argument data.<br>`dmq::IDispatcher` used to send serialized argument data to a remote endpoint.<br>`dmq::IDispatchMonitor` used to monitor message timeouts (optional). 

## Fixed-Block Memory Allocator

The `DelegateMQ` library includes an optional fixed-block memory allocator designed to improve performance and reduce heap fragmentation in mission-critical systems. This feature is enabled by defining `DMQ_ALLOCATOR`. See `DelegateOpt.h` for configuration details. The underlying allocator implementation is based on the [stl_allocator](https://github.com/endurodave/stl_allocator) repository.

When `DMQ_ALLOCATOR` is defined, the `XALLOCATOR` macro is used to override `new` and `delete` operators for internal library objects. However, enabling this macro does not guarantee zero usage of the global heap.

Even if total isolation from the global heap isn't achieved, employing `xallocator` with DelegateMQ can be helpful to reduce heap fragmentation and guarantees deterministic, constant-time O(1) memory operations for the system's most repetitive tasks.

The  [Valgrind Memory Tests](#valgrind-memory-tests) shows global heap vs. fixed-block allocator. Notice the fixed-block allocator reduces reliance on the heap and overall uses less total heap memory (less heap fragmentation). 

## Function Argument Copy

The behavior of the DelegateMQ library when invoking asynchronous non-blocking delegates (e.g. `dmq::DelegateAsyncFree<>`) is to copy arguments into heap memory for safe transport to the destination thread. All arguments (if any) are duplicated. If your data is not plain old data (POD) and cannot be bitwise copied, ensure you implement an appropriate copy constructor to handle the copying.

Since argument data is duplicated, an outgoing pointer argument passed to a function invoked using an asynchronous non-blocking delegate is not updated. A copy of the pointed to data is sent to the destination target thread, and the source thread continues without waiting for the target to be invoked.

Synchronous and asynchronous blocking delegates, on the other hand, do not copy the target function's arguments when invoked. Outgoing pointer arguments passed through an asynchronous blocking delegate (e.g., `dmq::DelegateAsyncFreeWait<>`) behave exactly as if the native target function were called directly.

## Caution Using `std::bind`

`std::function` compares the function signature, not the underlying callable instance. The example below demonstrates this limitation. 

```cpp
// Example shows std::function target limitations. Not a normal usage case.
// Use dmq::MakeDelegate() to create delegates works correctly with delegate 
// containers.
Test t1, t2;
std::function<void(int)> f1 = std::bind(&Test::Func, &t1, std::placeholders::_1);
std::function<void(int)> f2 = std::bind(&Test::Func2, &t2, std::placeholders::_1);
dmq::MulticastDelegateSafe<void(int)> safe;
safe += dmq::MakeDelegate(f1);
safe += dmq::MakeDelegate(f2);
safe -= dmq::MakeDelegate(f2);   // Should remove f2, not f1!
```

To avoid this issue, use `dmq::MakeDelegate()`. In this case, `dmq::MakeDelegate()` binds to `Test::Func` using `dmq::DelegateMember<>`, not `dmq::DelegateFunction<>`, which prevents the error above.

```cpp
// Corrected example
Test t1, t2;
dmq::MulticastDelegateSafe<void(int)> safe;
safe += dmq::MakeDelegate(&t1, &Test::Func);
safe += dmq::MakeDelegate(&t2, &Test::Func2);
safe -= dmq::MakeDelegate(&t2, &Test::Func2);   // Works correctly!
```

## Alternatives Considered

For a broader comparison of DelegateMQ against signal/slot libraries (Qt, Boost.Signals2, sigslot), remote communication frameworks (DDS, gRPC, ZeroMQ), and async patterns (`std::async`, OS queues, Boost.Asio), see [Technology Comparison](COMPARISON.md).

### `std::function`

`std::function` was not chosen as the delegate storage type because it compares function *signatures*, not callable *instances*. Two `std::bind` wrappers for different objects with the same signature are indistinguishable at runtime, making correct add/remove from a multicast container impossible.

DelegateMQ delegates compare by both callable type and target identity, so `dmq::MulticastDelegateSafe::operator-=` always removes the intended subscriber. See [Caution Using `std::bind`](#caution-using-stdbind) for a concrete example.

### `std::async` and `std::future`

The DelegateMQ library's asynchronous features differ from `std::async` in that the caller-specified thread of control is used to invoke the target function bound to the delegate. The asynchronous variants copy the argument data into the event queue, ensuring safe transport to the destination thread, regardless of the argument type. This approach provides 'fire and forget' functionality, allowing the caller to avoid waiting or worrying about out-of-scope stack variables being accessed by the target thread.

In short, the DelegateMQ library offers features that are not natively available in the C++ standard library to ease multi-threaded application development.

### `DelegateMQ` vs. `std::async` Feature Comparisons

| Feature | `dmq::Delegate` | `dmq::DelegateAsync` | `dmq::DelegateAsyncWait` | `std::async` |
| ---- | ----| ---- | ---- | ---- |
| Callable | Yes | Yes | Yes | Yes |
| Synchronous Call | Yes | No | No | No |
| Asynchronous Call | No | Yes | Yes | Yes |
| Asynchronous Blocking Call | No | No | Yes | Yes |
| Asynchronous Wait Timeout | No | No | Yes | No |
| Specify Target Thread | n/a | Yes | Yes | No |
| Copyable | Yes | Yes | Yes | Yes |
| Equality | Yes | Yes | Yes | Yes |
| Compare with `nullptr` | Yes | Yes | Yes | No |
| Callable Argument Copy | No | Yes<sup>1</sup> | No | No |
| Dangling `Arg&`/`Arg*` Possible | No | No | No | Yes<sup>2</sup> |
| Callable Ambiguity | No | No | No | Yes<sup>3</sup> |
| Thread-Safe Container <sup>4</sup> | Yes | Yes | Yes | No |

<sup>1</sup> `dmq::DelegateAsync<>` function call operator copies all function data arguments using a copy constructor for safe transport to the target thread.   
<sup>2</sup> `std::async` could fail if dangling reference or pointer function argument is accessed. Delegates copy argument data when needed to prevent this failure mode.  
<sup>3</sup> `std::function` cannot resolve difference between functions with matching signature `std::function` instances (e.g `void Class:One(int)` and `void Class::Two(int)` are equal).  
<sup>4</sup> `dmq::MulticastDelegateSafe<>` a thread-safe container used to hold and invoke a collection of delegates.

# Library

A single include `DelegateMQ.h` provides access to all delegate library features. The library is wrapped within a `dmq` namespace. The table below shows the delegate class hierarchy.

```cpp
// Delegates
dmq::DelegateBase
    dmq::Delegate<>
        dmq::DelegateFree<>
            dmq::DelegateFreeAsync<>
            dmq::DelegateFreeAsyncWait<>
            dmq::DelegateFreeRemote<>
        dmq::DelegateMember<>
            dmq::DelegateMemberAsync<>
            dmq::DelegateMemberAsyncWait<>
            dmq::DelegateMemberRemote<>
        dmq::DelegateMemberSp<>
            dmq::DelegateMemberAsyncSp<>
            dmq::DelegateMemberAsyncWaitSp<>
        dmq::DelegateFunction<>
            dmq::DelegateFunctionAsync<>
            dmq::DelegateFunctionAsyncWait<>
            dmq::DelegateFunctionRemote<>

// Delegate Containers
dmq::UnicastDelegate<>
    dmq::UnicastDelegateSafe<>
dmq::MulticastDelegate<>
    dmq::MulticastDelegateSafe<>
dmq::Signal<>          // thread-safe multicast with RAII ScopedConnection
```

Some degree of code duplication exists within the delegate inheritance hierarchy. This arises because the `Free`, `Member`, and `Function` classes support different target function types, making code sharing via inheritance difficult. Alternative solutions to share code either compromised type safety, caused non-intuitive user syntax, or significantly increased implementation complexity and code readability. Extensive unit tests ensure a reliable implementation.

The Python script `src_dup.py` helps mitigate some of the maintenance overhead. See the script source for details.

## Heap Template Parameter Pack

Non-blocking asynchronous invocations means that all argument data must be copied into the heap for transport to the destination thread. Arguments come in different styles: by value, by reference, pointer and pointer to pointer. For non-blocking delegates, the data is copied to the heap to ensure the data is valid on the destination thread. The key to being able to save each parameter into `dmq::DelegateMsgHeapArgs<>` is the `make_tuple_heap()` function. This template metaprogramming function creates a `tuple` of arguments where each tuple element is created on the heap.

```cpp
/// @brief Terminate the template metaprogramming argument loop
template<typename... Ts>
auto make_tuple_heap(dmq::xlist<std::shared_ptr<dmq::heap_arg_deleter_base>>& heapArgs, std::tuple<Ts...> tup)
{
    return tup;
}

/// @brief Creates a tuple with all tuple elements created on the heap using
/// operator new. Call with an empty list and empty tuple. The empty tuple is concatenated
/// with each heap element. The list contains heap_arg_deleter_base objects for each 
/// argument heap memory block that will be automatically deleted after the bound
/// function is invoked on the target thread. 
template<typename Arg1, typename... Args, typename... Ts>
auto make_tuple_heap(dmq::xlist<std::shared_ptr<dmq::heap_arg_deleter_base>>& heapArgs, std::tuple<Ts...> tup, Arg1 arg1, Args... args)
{
    static_assert(!(
        (dmq::is_shared_ptr<Arg1>::value && (std::is_lvalue_reference_v<Arg1> || std::is_pointer_v<Arg1>))),
        "std::shared_ptr reference argument not allowed");
    static_assert(!std::is_same<Arg1, void*>::value, "void* argument not allowed");

    auto new_tup = dmq::tuple_append(heapArgs, tup, arg1);
    return dmq::make_tuple_heap(heapArgs, new_tup, args...);
}
```

Template metaprogramming uses the C++ template system to perform compile-time computations within the code. Notice the recursive compiler call of `dmq::make_tuple_heap()` as the `Arg1` template parameter gets consumed by the function until no arguments remain and the recursion is terminated. The snippet above shows the concatenation of heap allocated tuple function arguments. This allows for the arguments to be copied into dynamic memory for transport to the target thread through a message queue.</p>

This bit of code inside `make_tuple_heap.h` was tricky to create in that each argument must have memory allocated, data copied, appended to the tuple, then subsequently deallocated all based on its type. To further complicate things, this all has to be done generically with N number of disparate template argument parameters. This was the key to getting a template parameter pack of arguments through a message queue. `dmq::DelegateMsgHeapArgs` then stores the tuple parameters for easy usage by the target thread. The target thread uses `std::apply()` to invoke the bound function with the heap allocated tuple argument(s).

The pointer argument `dmq::tuple_append()` implementation is shown below. It creates dynamic memory for the argument, argument data copied, adds to a deleter list for subsequent later cleanup after the target function is invoked, and finally returns the appended tuple.

```cpp
/// @brief Append a pointer argument to the tuple
template <typename Arg, typename... TupleElem>
auto tuple_append(dmq::xlist<std::shared_ptr<dmq::heap_arg_deleter_base>>& heapArgs, const std::tuple<TupleElem...> &tup, Arg* arg)
{
    Arg* heap_arg = nullptr;
    if (arg != nullptr) {
        heap_arg = new(std::nothrow) Arg(*arg);  // Only create a new Arg if arg is not nullptr
        if (!heap_arg) {
            dmq::BAD_ALLOC();
        }
    }
    std::shared_ptr<dmq::heap_arg_deleter_base> deleter(new(std::nothrow) dmq::heap_arg_deleter<Arg*>(heap_arg));
    if (!deleter) {
        dmq::BAD_ALLOC();
    }
    try {
        heapArgs.push_back(deleter);
        return std::tuple_cat(tup, std::make_tuple(heap_arg));
    }
    catch (...) {
        dmq::BAD_ALLOC();
        throw;
    }
}
```

The pointer argument deleter is implemented below. When the target function invocation is complete, the `dmq::heap_arg_deleter` destructor will `delete` the heap argument memory. The heap argument cannot be a changed to a smart pointer because it would change the argument type used in the target function signature. Therefore, the `dmq::heap_arg_deleter` is used as a smart pointer wrapper around the (potentially) non-smart heap argument.

```cpp
/// @brief Frees heap memory for pointer heap argument
template<typename T>
class heap_arg_deleter<T*> : public dmq::heap_arg_deleter_base
{
public:
    heap_arg_deleter(T* arg) : m_arg(arg) { }
    virtual ~heap_arg_deleter()
    {
        delete m_arg;
    }
private:
    T* m_arg;
};
```

### Argument Heap Copy

Asynchronous non-blocking delegate invocations mean that all argument data must be copied to the heap for transport to the destination thread. All arguments, regardless of their type, will be duplicated, including: value, pointer, pointer to pointer, and reference. If your data is not plain old data (POD) and cannot be bitwise copied, be sure to implement an appropriate copy constructor to handle the copying yourself.

For instance, invoking this function asynchronously the argument `TestStruct` will be copied.

```cpp
void TestFunc(TestStruct* data);
```

### Bypassing Argument Heap Copy

Occasionally, you may not want the delegate library to copy your arguments. Instead, you just want the destination thread to have a pointer to the original copy. A `std::shared_ptr` function argument does not copy the object pointed to when using an asynchronous delegate target function. 

```cpp
auto del = dmq::MakeDelegate([](std::shared_ptr<TestStruct> s) {}, workerThread1);
std::shared_ptr<TestStruct> sp = std::make_shared<TestStruct>();

// Invoke lambda and TestStruct instance is not copied
del(sp);
```

### Array Argument Heap Copy

Array function arguments are adjusted to a pointer per the C standard. In short, any function parameter declared as `T a[]` or `T a[N]` is treated as though it were declared as `T *a`. Since the array size is not known, the library cannot copy the entire array. For instance, the function below:

```cpp
void ArrayFunc(char a[]) {}
```

Requires a delegate argument `char*` because the `char a[]` was "adjusted" to `char *a`.

```cpp
auto delegateArrayFunc = dmq::MakeDelegate(&ArrayFunc, workerThread1);
delegateArrayFunc(cArray);
```

There is no way to asynchronously pass a C-style array by value. Avoid C-style arrays if possible when using asynchronous delegates to avoid confusion and mistakes.

# Interfaces

> Full documentation: **[PORTING.md](PORTING.md#interfaces)**

The `dmq::IThread`, `dmq::ISerializer`, and `dmq::IDispatcher` interface classes allow customizing the library's runtime behavior for new platforms or transports. See [PORTING.md](PORTING.md#interfaces) for full interface definitions and implementation examples.

# Cross-Language Interoperability

DelegateMQ supports first-class interoperability between C++, **C#**, and **Python** using a **Shared Native Core** architecture. This ensures that scripting-language clients benefit from the same high-performance networking and reliability logic (ACKs/timeouts) as the C++ core.

> Full documentation: **[INTEROP.md](INTEROP.md)**

### Key Features:
- **Shared Native Core**: A C++ DLL (`DmqInterop`) handles UDP sockets, protocol framing, and reliability.
- **Language Wrappers**: Thin, language-friendly libraries for C# (`P/Invoke`) and Python (`ctypes`).
- **Binary Serialization**: Efficient data exchange using MessagePack.
- **Location Transparency**: Remote topics look the same to the application whether they are local or on a remote node.

### Synchronization
Data structures stay in sync between languages using a **Field Order Convention**. C++ uses the `MSGPACK_DEFINE` macro, while C# uses `[Key(n)]` attributes to ensure fields match exactly.

```csharp
// C# example matching C++ struct
[MessagePackObject]
public class SensorData {
    [Key(0)] public int Id { get; set; }
    [Key(1)] public float Value { get; set; }
}
```

See [INTEROP.md](INTEROP.md) for architecture details, C-API definitions, and setup instructions. A complete 3-language demonstration is available in the **[sample-interop](../example/sample-interop/README.md)** example.

# Examples

## Callback Example

Here are a few real-world examples that demonstrate common delegate usage patterns. First, `SysData` is a simple class that exposes an outgoing asynchronous interface. It stores system data and provides asynchronous notifications to subscribers when the mode changes. The class interface is shown below:

```cpp
class SysData
{
public:
    /// Clients register with dmq::MulticastDelegateSafe1 to get callbacks when system mode changes
    dmq::MulticastDelegateSafe<void(const SystemModeChanged&)> SystemModeChangedDelegate;

    /// Get singleton instance of this class
    static SysData& GetInstance();

    /// Sets the system mode and notify registered clients via SystemModeChangedDelegate.
    /// @param[in] systemMode - the new system mode. 
    void SetSystemMode(SystemMode::Type systemMode);    

private:
    SysData();
    ~SysData();

    /// The current system mode data
    SystemMode::Type m_systemMode;

    /// Lock to make the class thread-safe
    dmq::RecursiveMutex m_lock;
};
```

The subscriber interface for receiving callbacks is `SystemModeChangedDelegate`. Calling `SetSystemMode()` updates `m_systemMode` with the new value and notifies all registered subscribers.

```cpp
void SysData::SetSystemMode(SystemMode::Type systemMode)
{
    std::lock_guard<dmq::RecursiveMutex> lockGuard(m_lock);

    // Create the callback data
    SystemModeChanged callbackData;
    callbackData.PreviousSystemMode = m_systemMode;
    callbackData.CurrentSystemMode = systemMode;

    // Update the system mode
    m_systemMode = systemMode;

    // Callback all registered subscribers
    SystemModeChangedDelegate(callbackData);
}
```

## Register Callback Example

`SysDataClient` is a delegate subscriber and registers for `SysData::SystemModeChangedDelegate` notifications within the constructor.

```cpp
// Constructor
SysDataClient() :
    m_numberOfCallbacks(0)
{
    // Register for async delegate callbacks
    SysData::GetInstance().SystemModeChangedDelegate += 
             dmq::MakeDelegate(this, &SysDataClient::CallbackFunction, workerThread1);
    SysDataNoLock::GetInstance().SystemModeChangedDelegate += 
                   dmq::MakeDelegate(this, &SysDataClient::CallbackFunction, workerThread1);
}
```

`SysDataClient::CallbackFunction()` is now called on `workerThread1` when the system mode changes.

```cpp
void CallbackFunction(const SystemModeChanged& data)
{
    m_numberOfCallbacks++;
    cout << "CallbackFunction " << data.CurrentSystemMode << endl;
}
```

When `SetSystemMode()` is called, anyone interested in the mode changes are notified synchronously or asynchronously depending on the delegate type registered.

```cpp
// Set new SystemMode values. Each call will invoke callbacks to all
// registered client subscribers.
SysData::GetInstance().SetSystemMode(SystemMode::STARTING);
SysData::GetInstance().SetSystemMode(SystemMode::NORMAL);
```

## Asynchronous API Examples

An async-API look like a normal function call to a caller. However, an async delegate invokes the callable on a specific thread of control either blocking or non-blocking.

### No Locks

`SysDataNoLock` is an alternate implementation that uses a private `dmq::MulticastDelegateSafe<>` for setting the system mode asynchronously and without locks.

```cpp
class SysDataNoLock
{
public:
    /// Clients register with dmq::MulticastDelegateSafe to get callbacks when system mode changes
    dmq::MulticastDelegateSafe<void(const SystemModeChanged&)> SystemModeChangedDelegate;

    /// Get singleton instance of this class
    static SysDataNoLock& GetInstance();

    /// Sets the system mode and notify registered clients via SystemModeChangedDelegate.
    /// @param[in] systemMode - the new system mode. 
    void SetSystemMode(SystemMode::Type systemMode);    

    /// Sets the system mode and notify registered clients via a temporary stack created
    /// asynchronous delegate. 
    /// @param[in] systemMode - The new system mode. 
    void SetSystemModeAsyncAPI(SystemMode::Type systemMode);    

    /// Sets the system mode and notify registered clients via a temporary stack created
    /// asynchronous delegate. This version blocks (waits) until the delegate callback
    /// is invoked and returns the previous system mode value. 
    /// @param[in] systemMode - The new system mode. 
    /// @return The previous system mode. 
    SystemMode::Type SetSystemModeAsyncWaitAPI(SystemMode::Type systemMode);

private:
    SysDataNoLock();
    ~SysDataNoLock();

    /// Private callback to get the SetSystemMode call onto a common thread
    dmq::MulticastDelegateSafe<void(SystemMode::Type)> SetSystemModeDelegate; 

    /// Sets the system mode and notify registered clients via SystemModeChangedDelegate.
    /// @param[in] systemMode - the new system mode. 
    void SetSystemModePrivate(SystemMode::Type);    

    /// The current system mode data
    SystemMode::Type m_systemMode;
};
```

The constructor registers `SetSystemModePrivate()` with the private `SetSystemModeDelegate`.

```cpp
SysDataNoLock::SysDataNoLock() :
    m_systemMode(SystemMode::STARTING)
{
    SetSystemModeDelegate += dmq::MakeDelegate
                 (this, &SysDataNoLock::SetSystemModePrivate, workerThread2);
    workerThread2.CreateThread();
}
```

The `SetSystemMode()` function below is an example of an asynchronous incoming interface. To the caller, it looks like a normal function, but under the hood, a private member call is invoked asynchronously using a delegate. In this case, invoking `SetSystemModeDelegate` causes `SetSystemModePrivate()` to be called on `workerThread2`.

```cpp
void SysDataNoLock::SetSystemMode(SystemMode::Type systemMode)
{
    // Invoke the private callback. SetSystemModePrivate() will be called on workerThread2.
    SetSystemModeDelegate(systemMode);
}
```

Since this private function is always invoked asynchronously on `workerThread2`, it doesn't require locks.

```cpp
void SysDataNoLock::SetSystemModePrivate(SystemMode::Type systemMode)
{
      // Create the callback data
      SystemModeChanged callbackData;

      callbackData.PreviousSystemMode = m_systemMode;
      callbackData.CurrentSystemMode = systemMode;

      // Update the system mode
      m_systemMode = systemMode;

      // Callback all registered subscribers
      SystemModeChangedDelegate(callbackData);
}
```

### Reinvoke

While creating a separate private function for an asynchronous API works, delegates allow you to simply reinvoke the same function on a different thread. A quick check determines if the caller is executing on the desired thread. If not, a temporary asynchronous delegate is created on the stack and invoked. The delegate and all original function arguments are duplicated on the heap, and the function is then reinvoked on `workerThread2`.

```cpp
void SysDataNoLock::SetSystemModeAsyncAPI(SystemMode::Type systemMode)
{
    // Is the caller executing on workerThread2?
    if (!workerThread2.IsCurrentThread())
    {
        // Create an asynchronous delegate and re-invoke the function call on workerThread2
        auto delegate = 
             dmq::MakeDelegate(this, &SysDataNoLock::SetSystemModeAsyncAPI, workerThread2);
        delegate(systemMode);
        return;
    }

    // Create the callback data
    SystemModeChanged callbackData;
    callbackData.PreviousSystemMode = m_systemMode;
    callbackData.CurrentSystemMode = systemMode;

    // Update the system mode
    m_systemMode = systemMode;

    // Callback all registered subscribers
    SystemModeChangedDelegate(callbackData);
}
```

### Blocking Reinvoke

A blocking asynchronous API can be encapsulated within a class member function. The following function sets the current mode on `workerThread2` and returns the previous mode. If the caller is not executing on `workerThread2`, a blocking delegate is created and invoked on that thread. To the caller, the function appears synchronous, but the delegate ensures the function is executed on the correct thread before returning.

```cpp
SystemMode::Type SysDataNoLock::SetSystemModeAsyncWaitAPI(SystemMode::Type systemMode)
{
    // Is the caller executing on workerThread2?
    if (!workerThread2.IsCurrentThread())
    {
        // Create an asynchronous delegate and re-invoke the function call on workerThread2
        auto delegate = dmq::MakeDelegate(this, &SysDataNoLock::SetSystemModeAsyncWaitAPI, workerThread2, dmq::WAIT_INFINITE);
        return delegate(systemMode);
    }

    // Create the callback data
    SystemModeChanged callbackData;
    callbackData.PreviousSystemMode = m_systemMode;
    callbackData.CurrentSystemMode = systemMode;

    // Update the system mode
    m_systemMode = systemMode;

    // Callback all registered subscribers
    SystemModeChangedDelegate(callbackData);

    return callbackData.PreviousSystemMode;
}
```

## Timer Example

Creating a timer callback service is trivial. `dmq::util::Timer` exposes a `dmq::Signal<void(void)>` member that clients connect to.

```cpp
/// @brief A timer class provides periodic timer callbacks on the client's
/// thread of control. Timer is thread safe.
class Timer
{
public:
    /// Clients connect to OnExpired to receive timer callbacks.
    /// dmq::Signal<> is thread-safe and requires no shared_ptr management.
    dmq::Signal<void(void)> OnExpired;

    /// Starts a timer for callbacks on the specified timeout interval.
    /// @param[in] timeout - the timeout in milliseconds.
    void Start(std::chrono::milliseconds timeout);

    /// Stops a timer.
    void Stop();

    ///...
};
```

### Safe Timer (RAII)
The library provides a thread-safe `dmq::util::Timer` that uses `dmq::Signal` and `dmq::ScopedConnection` to prevent callbacks on destroyed objects.

```cpp
// 1. Store a dmq::ScopedConnection
dmq::ScopedConnection m_conn;

void Init() {
    // 2. Connect using the RAII pattern with Pacing
    // If 'this' is destroyed, m_conn destructor automatically disconnects the timer.
    // MakeTimerDelegate ensures at most one message is in the thread queue.
    m_conn = m_timer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &MyClass::OnTimer, m_thread));
    m_timer.Start(std::chrono::milliseconds(250));
}
```

See example `SafeTimer.cpp` to prevent a latent callback on a dead object and [Object Lifetime and Async Delegates](#object-lifetime-and-async-delegates) for an explanation.

### Paced Timer Delegate
Standard async delegates accumulate in the target thread's queue if the thread is busy. For high-frequency timers, this can cause "queue flooding" where thousands of stale timer messages pile up.

`dmq::util::TimerDelegate` provides built-in backpressure. It ensures that **at most one** timer message is in the target thread's queue at any time.

*   **Busy Thread:** If the target thread is still processing a previous tick, new ticks are deferred.
*   **Self-Healing:** If a tick is missed because the thread was busy, it will fire as soon as possible (on the next `ProcessTimers()` poll after the thread becomes free) rather than waiting for the next full period.
*   **No Flooding:** The queue depth never grows beyond 1, regardless of how long the thread is blocked.

```cpp
#include "extras/util/TimerDelegate.h"

// 1. Create a paced delegate using MakeTimerDelegate
// 2. Connect to the timer
m_conn = m_timer.OnExpired.Connect(
    dmq::util::MakeTimerDelegate(this, &MyClass::OnTimer, m_thread)
);
m_timer.Start(std::chrono::milliseconds(10)); // High frequency
```

Use `TimerDelegate` for periodic tasks like sensor polling, UI updates, or heartbeats where stale data is better skipped than backlogged.

## `std::async` Thread Targeting Example

An example combining `std::async`/`std::future` and an asynchronous delegate to target a specific worker thread during communication transmission.

```cpp
dmq::os::Thread commThread("CommunicationThread");

// Assume SendMsg() is not thread-safe and may only be called on commThread context.
// A random std::async thread from the pool is unacceptable and causes cross-threading.
size_t SendMsg(const std::string& data)
{
    // Simulate sending data takes a long time
    std::this_thread::sleep_for(std::chrono::seconds(2));  

    // Return the "bytes sent" result
    return data.size();  
}

// Send a message asynchronously. Function does other work while SendMsg() 
// is executing on commThread.
size_t SendMsgAsync(const std::string& msg)
{
    // Create an async blocking delegate targeted at SendMsg()
    auto sendMsgDelegate = dmq::MakeDelegate(&SendMsg, commThread, dmq::WAIT_INFINITE);

    // Start the asynchronous task using std::async. SendMsg() will be called on 
    // commThread context.
    std::future<size_t> result = std::async(std::launch::async, sendMsgDelegate, msg);

    // Do other work while SendMsg() is executing on commThread
    std::cout << "Doing other work in main thread while SendMsg() completes...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Get bytes sent. Blocks until SendMsg() completes.
    size_t bytesSent = result.get();
    return bytesSent;
}

int main(void)
{
    // Send message asynchronously
    commThread.CreateThread();
    size_t bytesSent = SendMsgAsync("Hello world!");
    return 0;
}
```

## More Examples

See the `examples/sample-code` directory for additional examples.

## Sample Projects

Each project focuses on a transport and serialization pair, but you can freely mix and match any transport with any serializer. See the `examples/sample-projects` directory for example projects.

### External Dependencies

The following remote delegate sample projects have no external library dependencies:

* [bare-metal-remote](../example/sample-projects/bare-metal-remote/) - simple remote delegate app on Windows and Linux.
* [system-architecture-no-deps](../example/sample-projects/system-architecture-no-deps/) - complex remote delegate client/server apps using UDP on Windows or Linux.

All other projects require external 3rd party library support. See [Examples Setup](#examples-setup) for external library installation setup.

### system-architecture

The System Architecture example demonstrates a complex client-server DelegateMQ application. This example implements a (simulated) sensor and actuator data acquisition across two applications. It showcases communication and collaboration between subsystems, threads, and processes or processors. Delegate communication, callbacks, asynchronous APIs, and error handling are also highlighted. Notice how easily DelegateMQ transfers event data between threads and processes with minimal application code. The application layer is completely isolated from message passing details.

`NetworkMgr` has three types of remote delegate API examples:

1. **Non-Blocking** - message is sent without waiting. 
2. **Blocking** - message is sent and blocks waiting for the remote to acknowledge or timeout.
3. **Future** - message is sent without waiting and a `std::future` is used to capture the return value later.

Three System Architecture build projects exist:

* [system-architecture](../example/sample-projects/system-architecture/) - builds on Windows and Linux. Requires MessagePack and ZeroMQ external libraries. See [Examples Setup](#examples-setup).
* [system-architecture-no-deps](../example/sample-projects/system-architecture-no-deps/) - builds on Windows or Linux. No external libraries required.
* [databus](../example/sample-projects/databus/) - builds on Windows or Linux. Abstracted using the Data Bus. No external libraries required.
* [databus-multicast](../example/sample-projects/databus-multicast/) - builds on Windows or Linux. Demonstrates one-to-many distribution using the Data Bus and Multicast transport. No external libraries required.
* [databus-shapes](../example/sample-projects/databus-shapes/) - builds on Windows or Linux. Graphical TUI demonstration of DataBus features including Multicast and location transparency.
* [system-architecture-python](../example/sample-projects/system-architecture-python/) - Python client communicates with C++ server using MessagePack and ZeroMQ external libraries.
* [databus-interop](../example/sample-projects/databus-interop/) - Python and C# clients communicate with a dmq::databus::DataBus server over UDP using MessagePack. No ZeroMQ required. Uses the generic interop libraries in `interop/`.

Follow the steps below to execute the first two projects. See `README.txt` within [system-architecture-python](../example/sample-projects/system-architecture-python/) for the Python example.

1. Setup ZeroMQ and MessagePack external library dependencies, if necessary ([Examples Setup](#examples-setup))
2. Execute CMake command in `client` and `server` directories.  
   `cmake -B build .`
3. Build client and server applications within the `build` directory.
4. Start `delegate_server_app` first.
5. Start `delegate_client_app` second.
6. Client and server communicate and output debug data to the console.

| Class | Location | Details |
| --- | --- | --- |
| `ClientApp` | Client | Main client application. Handles local and remote sensors and actuators. |
| `ServerApp` | Server | Main server application. Slave sensors and actuators application. |
| `NetworkMgr` | Client<br>Server | Centralized, shared client and server communication interface using remote delegates. |
| `DataMgr` | Client | Collects data from client (local) and server (remote) data sources |
| `AlarmMgr` | Client<br>Server | Handles system alarms. Server sends alarms to client for processing. |
| `Sensor` | Client<br>Server | Reads sensor data |
| `Actuator` | Client<br>Server | Controls actuator |
| `dmq::RemoteChannel` | Client<br>Server | Aggregates a `dmq::util::Dispatcher`, serialization stream, and `dmq::Serializer` for a single message signature; used to configure `dmq::DelegateMemberRemote` endpoints. |

Interface implementation details. 

| Interface | Implementation | Details |
| --- | --- | --- |
| `dmq::IThread` | `dmq::os::Thread` | Implemented using `std::thread` | 
| `dmq::ISerializer` | `dmq::Serializer` | Implemented using MessagePack library | 
| `dmq::IDispatcher` | `dmq::util::Dispatcher` | Universal delegate dispatcher |
| `dmq::transport::ITransport` | `dmq::transport::ZeroMqTransport` | ZeroMQ message transport |
| `dmq::transport::ITransportMonitor` | `dmq::util::TransportMonitor` | Monitor message timeouts |

### Interop Libraries

The `interop/` directory at the repository root contains reusable client libraries that let non-C++ applications communicate with any DelegateMQ dmq::databus::DataBus application over UDP. They implement the `dmq::transport::DmqHeader` wire protocol and MessagePack serialization — no DelegateMQ C++ code is needed on the client side.

| Library | Location | Requirements |
| --- | --- | --- |
| Python | [`interop/python/`](../interop/python/) | `pip install msgpack` |
| C# | [`interop/csharp/`](../interop/csharp/) | .NET 8+, `MessagePack` NuGet package |

Both libraries expose the same conceptual API:

- **`DmqDataBus`** — UDP socket manager. Call `start()`/`stop()`, register callbacks per remote ID with `register_callback()` / `RegisterCallback()`, and send messages with `send()` / `Send()`.
- **`Serializer` / `pack` + `unpack`** — Encode and decode MessagePack arrays whose element order matches the C++ `MSGPACK_DEFINE` field order.

The `databus-interop` sample project (`example/sample-projects/databus-interop/`) is a complete working demonstration: a C++ dmq::databus::DataBus server publishes sensor/actuator data (`DataMsg`) on port 8000 and receives a polling-rate command (`CommandMsg`) on port 8001. The Python and C# clients each subscribe to the data and send the command using their respective `DmqDataBus` libraries.

See [`interop/README.md`](../interop/README.md) for the full wire protocol specification, and [`example/sample-projects/databus-interop/README.md`](../example/sample-projects/databus-interop/README.md) for build and run instructions.

### Sample Projects Comparison

#### Feature & Toolchain Demos

Demonstrate DelegateMQ delegate types (sync, async, asyncwait, multicast, signal) on specific platforms or compilers. No remote transport involved.

| Project Name | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **clang-native** | All-features demo: sync, async, asyncwait, multicast, signal. Windows or Linux. | `std::thread` | Any C++17 compiler (Clang, GCC, MSVC) |
| **atfe-armv7m-bare-metal** | ATfE (Clang/picolibc) bare-metal example for Armv7-M, runs on QEMU. | None | ATfE Clang, picolibc |
| **bare-metal-arm** | ARM GCC bare-metal example for Cortex-M4, runs on QEMU. | None | ARM GCC |
| **keil-bare-metal** | Bare-metal example for ARM Cortex-M4. | None | Keil MDK (ARMCLANG) |
| **stm32-freertos** | Embedded FreeRTOS example for STM32F4 Discovery. | FreeRTOS | STM32Cube / ARM GCC |

#### Remote Delegate Examples

Invoke a target function running in a separate process or processor. Each project focuses on a transport-serialization pair.

| Project Name | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **bare-metal-remote** | Simple remote delegate example with no external libraries. | `std::thread` | `operator<<` / `operator>>` | `std::stringstream` |
| **freertos-bare-metal** | FreeRTOS Windows port example (32-bit build). | FreeRTOS | `operator<<` / `operator>>` | `std::stringstream` |
| **linux-tcp-serializer** | Simple TCP remote delegate app on Linux. | `std::thread` | `dmq::Serializer` class | Linux TCP Socket |
| **linux-udp-serializer** | Simple UDP remote delegate app on Linux. | `std::thread` | `dmq::Serializer` class | Linux UDP Socket |
| **mqtt-rapidjson** | Remote delegate using MQTT and RapidJSON (Client/Server). | `std::thread` | RapidJSON | MQTT |
| **nng-bitsery** | Remote delegate using NNG and Bitsery. | `std::thread` | Bitsery | NNG |
| **serialport-serializer** | Remote delegate using libserialport. | `std::thread` | `dmq::Serializer` class | `libserialport` |
| **system-architecture** | System architecture example with dependencies. | `std::thread` | Various | Various |
| **system-architecture-no-deps** | System architecture example (UDP) with no deps. | `std::thread` | `operator<<` / `operator>>` | UDP Socket |
| **databus** | System architecture example using the Data Bus. | `std::thread` | `dmq::Serializer` class | UDP Socket |
| **databus-multicast** | One-to-many distribution using the Data Bus. | `std::thread` | `dmq::Serializer` class | UDP Multicast |
| **system-architecture-python** | Python binding example (ZeroMQ). | `std::thread` | N/A | Python / ZeroMQ |
| **databus-interop** | Python and C# clients interop with a C++ dmq::databus::DataBus server over UDP. | `std::thread` | MessagePack | UDP Socket |
| **win32-pipe-serializer** | Windows Named Pipe example. | `std::thread` | `dmq::Serializer` class | Windows Pipe |
| **win32-tcp-serializer** | Windows TCP Socket example. | `std::thread` | `dmq::Serializer` class | Windows TCP Socket |
| **win32-udp-serializer** | Windows UDP Socket example. | `std::thread` | `dmq::Serializer` class | Windows UDP Socket |
| **zeromq-bitsery** | ZeroMQ transport with Bitsery serialization. | `std::thread` | Bitsery | ZeroMQ |
| **zeromq-cereal** | ZeroMQ transport with Cereal serialization. | `std::thread` | Cereal | ZeroMQ |
| **zeromq-msgpack-cpp** | ZeroMQ transport with MessagePack. | `std::thread` | MessagePack | ZeroMQ |
| **zeromq-rapidjson** | ZeroMQ transport with RapidJSON. | `std::thread` | RapidJSON | ZeroMQ |
| **zeromq-serializer** | ZeroMQ transport with custom `dmq::Serializer` class. | `std::thread` | `dmq::Serializer` class | ZeroMQ |
# Tests

The current master branch build passes all unit tests and Valgrind memory tests.

## Unit Tests

Build and execute runs all the unit tests contained within the `tests\unit-tests` directory.

## Valgrind Memory Tests

[Valgrind](https://valgrind.org/) dynamic storage allocation tests were performed using the global heap and fixed-block allocator builds. Valgrind is a programming tool for detecting memory leaks, memory errors, and profiling performance in applications, primarily for Linux-based systems. All tests run on Linux.

### Results Summary

**The Code is Memory-Safe (No Leaks)**: Both runs show `ERROR SUMMARY: 0 errors` and `definitely lost: 0 bytes`.

**Reduction in System Overhead**: 50% Reduction in alloc calls and 78% Reduction in memory churn. Most uncaptured fixed-block allocatations from unit tests using the global heap, not DelegateMQ library itself. 

| Metric | Heap Enabled (Standard `new`) | Fixed Block Allocator | Improvement |
| :--- | :--- | :--- | :--- |
| **Allocations** | 114,487,427 | 57,130,929 | **~50% Reduction** |
| **Frees** | 114,487,425 | 57,130,927 | **~50% Reduction** |
| **Total Bytes** | 5,858,329,010 (~5.8 GB) | 1,303,924,718 (~1.3 GB) | **~78% Reduction** |

Note on "Still Reachable": The 64 bytes in 2 blocks reported in both runs are benign. The stack trace confirms these are **Static Singletons** (`dmq::util::Timer::GetTimers` and `dmq::util::Timer::GetLock`). Since these exist for the lifetime of the program and are reclaimed by the OS at exit, they are not leaks.

### Heap Memory Test Results

The DelegateMQ library Valgrind test results using the heap.

```
==140605== HEAP SUMMARY:
==140605==     in use at exit: 64 bytes in 2 blocks
==140605==   total heap usage: 114,487,427 allocs, 114,487,425 frees, 5,858,329,010 bytes allocated
==140605== 
==140605== 24 bytes in 1 blocks are still reachable in loss record 1 of 2
==140605==    at 0x4849013: operator new(unsigned long) (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==140605==    by 0x59D59B: dmq::util::Timer::GetTimers() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x59CC27: dmq::util::Timer::Start(std::chrono::duration<long, std::ratio<1l, 1000l> >, bool) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x590298: dmq::os::Thread::CreateThread(std::optional<std::chrono::duration<long, std::ratio<1l, 1000l> > >) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x4FAC44: main (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605== 
==140605== 40 bytes in 1 blocks are still reachable in loss record 2 of 2
==140605==    at 0x4849013: operator new(unsigned long) (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==140605==    by 0x59D69D: dmq::util::Timer::GetLock() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x59C7E3: dmq::util::Timer::Timer() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x593F5B: std::__detail::_MakeUniq<dmq::util::Timer>::__single_object std::make_unique<dmq::util::Timer>() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x59014C: dmq::os::Thread::CreateThread(std::optional<std::chrono::duration<long, std::ratio<1l, 1000l> > >) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605==    by 0x4FAC44: main (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==140605== 
==140605== LEAK SUMMARY:
==140605==    definitely lost: 0 bytes in 0 blocks
==140605==    indirectly lost: 0 bytes in 0 blocks
==140605==      possibly lost: 0 bytes in 0 blocks
==140605==    still reachable: 64 bytes in 2 blocks
==140605==         suppressed: 0 bytes in 0 blocks
==140605== 
==140605== For lists of detected and suppressed errors, rerun with: -s
==140605== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

### Fixed-Block Memory Allocator Test Results

Test results with the fixed-block allocator. See [stl_allocator](https://github.com/endurodave/stl_allocator) and [xallocator](https://github.com/endurodave/xallocator) for information about the memory allocators.

```
==144812== 
==144812== HEAP SUMMARY:
==144812==     in use at exit: 64 bytes in 2 blocks
==144812==   total heap usage: 57,130,929 allocs, 57,130,927 frees, 1,303,924,718 bytes allocated
==144812== 
==144812== 24 bytes in 1 blocks are still reachable in loss record 1 of 2
==144812==    at 0x4849013: operator new(unsigned long) (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==144812==    by 0x5B2535: dmq::util::Timer::GetTimers[abi:cxx11]() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x5B1B77: dmq::util::Timer::Start(std::chrono::duration<long, std::ratio<1l, 1000l> >, bool) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x5A3FD4: dmq::os::Thread::CreateThread(std::optional<std::chrono::duration<long, std::ratio<1l, 1000l> > >) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x50DEC4: main (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812== 
==144812== 40 bytes in 1 blocks are still reachable in loss record 2 of 2
==144812==    at 0x4849013: operator new(unsigned long) (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==144812==    by 0x5B267A: dmq::util::Timer::GetLock() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x5B1733: dmq::util::Timer::Timer() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x5A7D61: std::__detail::_MakeUniq<dmq::util::Timer>::__single_object std::make_unique<dmq::util::Timer>() (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x5A3E88: dmq::os::Thread::CreateThread(std::optional<std::chrono::duration<long, std::ratio<1l, 1000l> > >) (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812==    by 0x50DEC4: main (in /home/david/Documents/DelegateMQWorkspace/DelegateMQ/build/delegate_app/delegate_app)
==144812== 
==144812== LEAK SUMMARY:
==144812==    definitely lost: 0 bytes in 0 blocks
==144812==    indirectly lost: 0 bytes in 0 blocks
==144812==      possibly lost: 0 bytes in 0 blocks
==144812==    still reachable: 64 bytes in 2 blocks
==144812==         suppressed: 0 bytes in 0 blocks
==144812== 
==144812== For lists of detected and suppressed errors, rerun with: -s
==144812== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)

```

# Library Comparison

The table below summarizes the various asynchronous function invocation implementations available in C and C++.

| Repository                                                                                            | Language | Key Delegate Features                                                                                                                                                                                                               | Notes                                                                                                                                                                                                      |
|-------------------------------------------------------------------------------------------------------|----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <a href="https://github.com/DelegateMQ/DelegateMQ">DelegateMQ</a> | C++17    | * Function-like template syntax<br> * Any delegate target function type (member, static, free, lambda)<br>  * N target function arguments<br> * N delegate subscribers<br>  * Optional fixed-block allocator     | * Modern C++ implementation<br> * Extensive unit tests<br> * Metaprogramming and variadic templates used for library implementation<br> * Any C++17 and higher compiler |
<a href="https://github.com/endurodave/AsyncCallback">AsyncCallback</a>                               | C++      | * Traditional template syntax<br> * Callback target function type (static, free)<br> * 1 target function argument<br> * N callback subscribers                                                                                      | * Low lines of source code<br> * Compact C++ implementation<br> * Any C++ compiler                                                                                                                    |
| <a href="https://github.com/endurodave/C_AsyncCallback">C_AsyncCallback</a>                           | C        | * Macros provide type-safety<br> * Callback target function type (static, free)<br> * 1 target function argument<br> * Fixed callback subscribers (set at compile time)<br> * Optional fixed-block allocator                        | * Low lines of source code<br> * Compact C implementation<br> * Any C compiler                                            

# References

Repositories utilizing the DelegateMQ library within different multithreaded applications.

* <a href="https://github.com/endurodave/StateMachineWithModernDelegates">C++ State Machine with Asynchronous Delegates</a> - a framework combining a C++ state machine with the asynchronous delegate library.
* <a href="https://github.com/endurodave/AsyncStateMachine">Asynchronous State Machine Design in C++</a> - an asynchronous C++ state machine implemented using an asynchronous delegate library.
* <a href="https://github.com/DelegateMQ/IntegrationTestFramework">Integration Test Framework using Google Test and Delegates</a> - a multi-threaded C++ software integration test framework using Google Test and Delegate libraries.
* <a href="https://github.com/DelegateMQ/Async-SQLite">Asynchronous SQLite API using C++ Delegates</a> - an asynchronous SQLite wrapper implemented using an asynchronous delegate library.
