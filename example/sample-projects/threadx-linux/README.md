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

### A ThreadX-specific gotcha: priming the Timer lock

`Timer::ProcessTimers()` lazily constructs an internal `dmq::RecursiveMutex` on its first call. ThreadX forbids creating synchronization objects (`tx_mutex_create`, etc.) from its own internal timer thread (`_tx_timer_thread`) — it returns `TX_CALLER_ERROR`. Since the periodic software timer above calls `Timer::ProcessTimers()` from exactly that context, `main_delegate()` calls `Timer::ProcessTimers()` once itself — from the safe `tx_application_define()` context — *before* creating the periodic timer, so the lazy mutex construction happens somewhere ThreadX allows it. Any application driving `dmq::util::Timer` from a ThreadX software timer callback needs this same priming call.

### Exiting

ThreadX has no kernel-level "stop scheduler" call, and this is a simulation running as an ordinary Linux process, so after the test suite completes, `MainThreadEntry()` calls `std::exit(0)` — which tears down the whole process (WorkerThread, the ThreadX timer thread, everything) in one step.
