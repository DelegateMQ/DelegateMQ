# DelegateMQ FreeRTOS Bare Metal Example

This sample project demonstrates how to integrate **DelegateMQ** into a **FreeRTOS** environment. It uses the FreeRTOS Windows Simulator (Win32) to provide a "bare metal" feel on a desktop development environment, showcasing the library's ability to handle asynchronous dispatching, thread-safe signals, and timers within an RTOS.

## Features Demonstrated

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (FreeRTOS port) to safely send delegate messages from one task to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple tasks.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's timer system with the FreeRTOS tick/timer daemon.

## Prerequisites

*   **CMake** (3.10 or higher)
*   **Visual Studio** (Windows is required for the FreeRTOS Win32 simulator)
*   **FreeRTOS Source**: This project assumes the FreeRTOS kernel source is available in `external/FreeRTOS` (fetched by `01_fetch_repos.py`).

## Build Instructions

Because FreeRTOS uses a Win32-specific port for the simulator, you **must** build the project for the `Win32` (x86) architecture. 64-bit builds are not supported by this specific FreeRTOS port.

### Using the standard script
From the root `DelegateMQ` directory:
```powershell
python scripts/03_generate_samples.py
python scripts/04_build_samples.py
```

### Manual Build
1. Create a build directory:
   ```powershell
   cmake -A Win32 -B build .
   ```
2. Build the project:
   ```powershell
   cmake --build build --config Release
   ```

## Project Structure

*   `main.c`: The application entry point. Initializes the FreeRTOS heap and starts the scheduler.
*   `main_delegate.cpp`: Contains the DelegateMQ test logic and FreeRTOS task definitions.
*   `FreeRTOSConfig.h`: Standard FreeRTOS configuration file tailored for this demo.
*   `CMakeLists.txt`: Build configuration that pulls in the DelegateMQ library and FreeRTOS kernel sources.

## How it Works

The example starts a "MainTask" which executes a suite of tests. One of the key tests creates a `dmq::os::Thread` object named "WorkerThread". When an asynchronous delegate is invoked on this thread, DelegateMQ wraps the call into a message and posts it to a FreeRTOS queue. The "WorkerThread" (running its own event loop) retrieves the message and executes the delegate in its own context.

This architecture allows for safe, non-blocking communication between tasks without the need for manual semaphore management or complex shared state.
