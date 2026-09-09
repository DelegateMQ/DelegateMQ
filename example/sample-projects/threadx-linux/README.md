# DelegateMQ ThreadX Linux Example

This sample project demonstrates how to integrate **DelegateMQ** into a **ThreadX** environment. It uses ThreadX's official Linux/GNU simulation port (`ports/linux/gnu`), which implements ThreadX scheduling on top of POSIX threads and signals, to run and debug ThreadX application code as an ordinary Linux process — no cross-compiler or target hardware required.

Unlike the FreeRTOS Windows-only samples (`freertos-bare-metal`, `databus-freertos`), this build is Linux-native and self-contained: no `-A Win32`, no IDE-specific project files, just `cmake` + `make`/`ninja`.

## Features Demonstrated

Identical test suite to `freertos-bare-metal`, on a different RTOS port — showing that application code doesn't change, only `DMQ_THREAD`:

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (ThreadX port) to safely send delegate messages from one thread to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple threads.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's `Timer` with a ThreadX software timer.
7.  **FullPolicy Stress Tests**: `DelegateThreadsTests()` (Test 9) exercises `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default/unlimited-queue) behavior — see Known Limitation below for what's currently disabled here.
8.  **PacedDispatch & TimerDelegate**: `TimerDelegateTests()` (Test 10) covers `dmq::util::PacedDispatch`'s at-most-one-in-flight gating logic and `dmq::util::TimerDelegate` dispatching to a real `dmq::os::Thread`, including a `dmq::util::Timer` wired straight to a `TimerDelegate` and driven by this sample's own periodic system timer. Unaffected by the Known Limitation below — it only ever has one worker thread alive.

## Prerequisites

*   **CMake** (3.16 or higher)
*   **Linux** with GCC (the ThreadX Linux/GNU port needs a native assembler and POSIX threads/signals — there is no Windows or macOS build of this sample)
*   **ThreadX source**: fetched by `python 01_fetch_repos.py` from the repo root, which clones `eclipse-threadx/threadx` to the workspace root (sibling to `DelegateMQ/`)

## Build Instructions

### Using the standard script
From the root `DelegateMQ` directory:
```bash
python 01_fetch_repos.py      # fetches ThreadX (and other deps) if not already present
python 03_generate_samples.py
python 04_build_samples.py
```

### Manual Build
```bash
cd example/sample-projects/threadx-linux
mkdir build && cd build
cmake ..
cmake --build .
./delegate_threadx_linux
```

## Project Structure

*   `main.c`: The application entry point. Calls `tx_kernel_enter()`, which invokes `tx_application_define()` once before starting the scheduler.
*   `main_delegate.cpp`: Contains the DelegateMQ test logic, the "MainThread" `TX_THREAD` that runs it, and the periodic ThreadX software timer that drives `Timer::ProcessTimers()`.
*   `CMakeLists.txt`: Build configuration that pulls in the DelegateMQ library and links against ThreadX's own `azrtos::threadx` CMake target.

## How it Works

`tx_kernel_enter()` performs low-level ThreadX initialization and then calls `tx_application_define()` — the ThreadX equivalent of "create initial tasks, then start the scheduler." There, `main_delegate()` creates a "MainThread" that runs the test suite and a periodic software timer that calls `Timer::ProcessTimers()` every tick.

One of the tests creates a `dmq::os::Thread` object named "WorkerThread". When an asynchronous delegate is invoked on it, DelegateMQ wraps the call into a message and posts it to a ThreadX queue (`TX_QUEUE`); the WorkerThread's own event loop dequeues and executes it — the same cross-thread dispatch pattern as every other `dmq::os::Thread` port.

### `Timer`'s lock is ISR-safe on this port

`Timer::ProcessTimers()` takes an internal lock (`Timer::GetLock()`) on every call. On most RTOS ports that lock is `dmq::RecursiveMutex` — a real OS mutex, which ThreadX (like every other RTOS here) forbids acquiring or creating from a genuine hardware ISR (`tx_mutex_get()`/`tx_mutex_create()` both return `TX_CALLER_ERROR` for an ISR caller). ThreadX is the one port where `dmq::CriticalSection` (what `Timer::GetLock()` actually uses) is implemented for real: `port/os/threadx/ThreadXCriticalSection.h` disables/restores interrupts directly instead of touching a `TX_MUTEX`, which is valid from both thread and ISR context. That's why this sample needs no special priming step even though the periodic timer above calls `Timer::ProcessTimers()` from ThreadX's internal timer thread — and it would work identically if called from a real `SysTick_Handler`-style ISR instead. Every other RTOS port (FreeRTOS, Zephyr, CMSIS-RTOS2) still aliases `dmq::CriticalSection` to `dmq::RecursiveMutex` and is *not* ISR-safe yet — see the comments next to each port's `using CriticalSection = ...;` in `DelegateOpt.h`.

### Known Limitation: two concurrent worker threads deadlock

`DelegateThreadsTests()` (Test 9) only runs `ThreadFullPolicyTests()`, which creates one worker thread at a time, uses it, and exits it before the next is created. `FreeTests()`/`MemberTests()`/`MemberSpTests()`/`FunctionTests()` are commented out: they all need two worker threads (`workerThread1`/`workerThread2`) concurrently alive, and that specifically deadlocks ThreadX's own Linux/GNU port kernel — a worker thread's own startup blocks forever on ThreadX's internal `_tx_linux_mutex`. Root-caused via `gdb`; appears to be a genuine bug in vendored ThreadX itself, not in DelegateMQ (confirmed independent of DelegateMQ's `Timer`/`CriticalSection` code). See the header comment in `DelegateThreadsTests.cpp` for the full investigation.

### Exiting

ThreadX has no kernel-level "stop scheduler" call, and this is a simulation running as an ordinary Linux process, so after the test suite completes, `MainThreadEntry()` calls `std::exit(0)` — which tears down the whole process (WorkerThread, the ThreadX timer thread, everything) in one step.

Test 10 (`TimerDelegateTests()`, in `TimerDelegateTests.cpp`) ports `test/unit-tests/TimerDelegateTests.cpp`'s `PacedDispatch`/`TimerDelegate` coverage against this same ThreadX `dmq::os::Thread` port. `TimerDelegate_WithTimer_DispatchesToThread()` doesn't spin up its own thread to drive `Timer::ProcessTimers()` the way the desktop version does — it reuses the periodic software timer `main_delegate()` already started before `ExecuteAllTests()` ran, the same one Test 8 relies on.
