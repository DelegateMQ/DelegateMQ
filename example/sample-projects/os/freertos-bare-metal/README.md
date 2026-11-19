# DelegateMQ FreeRTOS Bare Metal Example

This sample project demonstrates how to integrate **DelegateMQ** into a **FreeRTOS** environment. It uses the FreeRTOS Windows Simulator (Win32) to provide a "bare metal" feel on a desktop development environment, showcasing the library's ability to handle asynchronous dispatching, thread-safe signals, and timers within an RTOS.

## Features Demonstrated

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (FreeRTOS port) to safely send delegate messages from one task to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple tasks.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's timer system with the FreeRTOS tick/timer daemon.
7.  **Event-Driven Timers** (Test 13): a task runs `Timer::ProcessTimers()` only when a timer is due, waiting for `Timer::GetNextExpiry()` or a notification from `Timer::SetScheduleChangedHandler()`, instead of every 10 ms. Checks `GetNextExpiry()` (no timer, the earliest of two, overdue), that one-shots fire on time (also one started while the driver waits for a later deadline), and that a 100 ms periodic timer wakes the driver about 11 times in a second, where a 10 ms poll wakes it 105 times. With `configUSE_TICKLESS_IDLE`, that's what lets the kernel sleep the CPU between deadlines.

## Prerequisites

*   **CMake** (3.16 or higher)
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

## On a Real Cortex-M: QEMU (`-DBM_TARGET=arm-m3`)

The same tests (`main_delegate.cpp`) also build for a real Cortex-M3 and run on QEMU's `mps2-an385` board, with the FreeRTOS `GCC/ARM_CM3` port instead of the Win32 simulator. That build adds **Test 11**: a hardware timer interrupt (the CMSDK APB timer, IRQ 8, at a priority allowed to use the FromISR API) calls `dmq::util::Timer::Start()`/`Stop()`, which take `Timer::GetLock()`, a `dmq::CriticalSection`, while a task does the same and the FreeRTOS timer task runs `ProcessTimers()`. On FreeRTOS that is `FreeRTOSCriticalSection`, which must detect the interrupt and use `taskENTER_CRITICAL_FROM_ISR()`: with detection forced off, the Cortex-M port's own `configASSERT` (no task-level critical section inside an interrupt) ends the run.

It also adds **Test 12**: the same interrupt posts to a `dmq::util::IsrDelegate` whose target is async, to a worker thread. A drain task runs `IsrDelegates::ProcessAll()`, woken by the post handler, which gives its semaphore with `xSemaphoreGiveFromISR()`. Every post must reach the worker once and in order; with the drain task paused, the posts the ring can't hold must be dropped and counted, never blocking the interrupt.

**Test 14**, last because it ends the run: the watchdog hook (`dmq::os::SetWatchdogHook()`). Two watched threads hang, one blocked forever on a semaphore and one spinning in `SpinForever()`, and a checker task calls `Thread::WatchdogCheckAll()`. The hook must see both (one pass reports every hung thread), recording each one's stall time and the program counter FreeRTOS saved for it; then `WatchdogHandler()` (`targets/arm-m3/fault.cpp`) runs once, checks the records and ends the run. The spinning thread's saved PC must be inside `SpinForever()`; the blocked one's resolves to `xQueueSemaphoreTake` (`arm-none-eabi-addr2line -f -e delegate_app <pc>`).

Needs `arm-none-eabi-gcc` and `qemu-system-arm`; runs on Windows and Linux:

```shell
cmake -B build-arm-m3 -G Ninja -DBM_TARGET=arm-m3 .
cmake --build build-arm-m3
qemu-system-arm -M mps2-an385 -cpu cortex-m3 -kernel build-arm-m3/delegate_app -nographic -semihosting-config enable=on,target=native
```

QEMU exits with status 0 when every test passed. `targets/arm-m3/`:
- `toolchain.cmake`: compilers and flags, and the FreeRTOS port (`GCC/ARM_CM3`) and heap (`heap_4`) for DelegateMQ's `External.cmake`.
- `startup.c`: vector table with the kernel's SVC, PendSV and SysTick handlers and the timer interrupt; fault handlers that report and end the run.
- `main.c`: FreeRTOS hooks, and newlib's malloc lock (tasks allocate concurrently).
- `bsp.c`: the timer interrupt (`bsp.h`), and `BspExit()`.
- `fault.cpp`: DelegateMQ's fault handlers (`DMQ_CUSTOM_FAULT_HANDLER`), which print and exit instead of halting.
- The newlib syscalls over semihosting and the linker script are the [bare-metal/qemu](../../bare-metal/qemu/README.md) sample's (`targets/common/retarget.c`, `targets/arm-m4/flash.ld`).

`FreeRTOSConfig.h` adds the Cortex-M settings (interrupt priorities, newlib reentrancy) under `__ARM_ARCH`. On Windows, QEMU delivers timer interrupts at the host's coarse timer granularity, so Test 11 sees fewer interrupts and takes several seconds; its checks don't assume a rate. `build_all.py` builds and runs this target on both platforms (`full_suite.QEMU_TARGET_SAMPLES`).

## Project Structure

*   `main.c`: The application entry point. Initializes the FreeRTOS heap and starts the scheduler.
*   `main_delegate.cpp`: Contains the DelegateMQ test logic and FreeRTOS task definitions.
*   `FreeRTOSConfig.h`: Standard FreeRTOS configuration file tailored for this demo.
*   `CMakeLists.txt`: Build configuration that pulls in the DelegateMQ library and FreeRTOS kernel sources.

## How it Works

The example starts a "MainTask" which executes a suite of tests. One of the key tests creates a `dmq::os::Thread` object named "WorkerThread". When an asynchronous delegate is invoked on this thread, DelegateMQ wraps the call into a message and posts it to a FreeRTOS queue. The "WorkerThread" (running its own event loop) retrieves the message and executes the delegate in its own context.

This architecture allows for safe, non-blocking communication between tasks without the need for manual semaphore management or complex shared state.
