# DelegateMQ FreeRTOS Linux Example

This sample project demonstrates how to integrate **DelegateMQ** into a **FreeRTOS** environment using FreeRTOS's own Linux/POSIX simulation port (`portable/ThirdParty/GCC/Posix`), where each FreeRTOS task is a real pthread. It runs and debugs FreeRTOS application code as an ordinary Linux process — no cross-compiler or target hardware required.

Unlike `freertos-bare-metal` and `databus-freertos` (both Windows-only, using the FreeRTOS Win32 simulator), this build is Linux-native and self-contained: no `-A Win32`, no IDE-specific project files, just `cmake` + `make`/`ninja`.

## Features Demonstrated

Identical test suite to `freertos-bare-metal` and `threadx-linux` — showing that application code doesn't change across simulator ports, only `DMQ_THREAD` and the build target:

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (FreeRTOS port) to safely send delegate messages from one task to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple tasks.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's `Timer` with a FreeRTOS software timer.
7.  **FullPolicy Stress Tests**: `DelegateThreadsTests()` (Test 9) exercises two concurrently active worker threads, `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default/unlimited-queue) behavior, and repeated cross-thread `AsyncInvoke()` calls under real FreeRTOS preemption.

## Prerequisites

*   **CMake** (3.16 or higher)
*   **Linux** with GCC (the FreeRTOS POSIX port needs `pthread`/`rt`; there is no Windows or macOS build of this sample — use `freertos-bare-metal` there instead)
*   **FreeRTOS kernel source**: fetched by `python 01_fetch_repos.py` from the repo root, which clones `FreeRTOS/FreeRTOS-Kernel` to the workspace root (sibling to `DelegateMQ/`)

## Build Instructions

### Using the standard script
From the root `DelegateMQ` directory:
```bash
python 01_fetch_repos.py      # fetches FreeRTOS (and other deps) if not already present
python 03_generate_samples.py
python 04_build_samples.py
```

### Manual Build
```bash
cd example/sample-projects/freertos-linux
mkdir build && cd build
cmake ..
cmake --build .
./delegate_freertos_linux
```

## Project Structure

*   `main.c`: The application entry point. Initializes the `heap_5` regions and calls `main_delegate()`.
*   `main_delegate.cpp`: Contains the DelegateMQ test logic, the "MainTask" that runs it, and the periodic FreeRTOS software timer that drives `Timer::ProcessTimers()`.
*   `FreeRTOSConfig.h`: Scheduler config for the POSIX port (1 kHz tick, `heap_5`, static allocation).
*   `CMakeLists.txt`: Build configuration that pulls in the DelegateMQ library and FreeRTOS kernel + POSIX port sources (via `External.cmake`, same mechanism `freertos-bare-metal` uses).

## How it Works

`main()` initializes the heap, then `main_delegate()` creates a periodic software timer (drives `Timer::ProcessTimers()` every tick), creates a "MainTask" that runs the test suite, and calls `vTaskStartScheduler()`.

One of the tests creates a `dmq::os::Thread` object named "WorkerThread". When an asynchronous delegate is invoked on it, DelegateMQ wraps the call into a message and posts it to a FreeRTOS queue; the WorkerThread's own event loop dequeues and executes it — the same cross-thread dispatch pattern as every other `dmq::os::Thread` port.

Test 9 (`DelegateThreadsTests()`, in `DelegateThreadsTests.cpp`) goes further: two worker threads alive at once, `AsyncInvoke()` blocking-wait calls, and `dmq::FullPolicy`'s DROP/TIMEOUT/FAULT/default/unlimited-queue behavior under load. Its `FullPolicy_Drop_DropsWhenFull()` sub-test gives its consumer thread a lower priority than the caller (`SetThreadPriority()`) — otherwise FreeRTOS's preemptive scheduler stalls the publisher on every post, since a higher-or-equal-priority consumer preempts it immediately.

### Exercises `dmq::CriticalSection`'s FreeRTOS implementation

`Timer::GetLock()` returns `dmq::CriticalSection` (see `port/os/freertos/FreeRTOSCriticalSection.h`), which — unlike `dmq::Mutex`/`RecursiveMutex` — is safe to acquire from ISR context on real ARM Cortex-M FreeRTOS targets, auto-detecting context via `xPortIsInsideInterrupt()`. The POSIX simulator port used here does **not** provide that detection function (there's no real hardware interrupt to detect on a host-OS process), so on this specific sample `FreeRTOSCriticalSection` always takes the task-context path (`taskENTER_CRITICAL()`/`EXIT()`) — which is exactly what this sample verifies, since the periodic software timer calls `Timer::ProcessTimers()` on every 10ms tick from FreeRTOS's own Timer Service task. The ISR-context path (`taskENTER_CRITICAL_FROM_ISR()`/`EXIT_FROM_ISR()`, selected automatically on a real Cortex-M target) is reasoned through but not exercised by this sample — there's no genuine hardware interrupt to trigger it here.

### Exiting

`vTaskDelete(NULL)` on the calling task never returns control to the caller, so after the test suite completes, `RunTestsTask()` calls `std::exit(0)` directly instead — which tears down the whole process (WorkerThread, the FreeRTOS timer/idle tasks, everything) in one step, the same approach `threadx-linux` uses.
