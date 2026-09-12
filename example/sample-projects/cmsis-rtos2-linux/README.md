# DelegateMQ CMSIS-RTOS2 Linux Example

This sample project demonstrates **DelegateMQ**'s `DMQ_THREAD_CMSIS_RTOS2` port running against a genuine CMSIS-RTOS2 implementation — Zephyr's own CMSIS-RTOS v2 compatibility layer (`CONFIG_CMSIS_RTOS_V2`, `subsys/portability/cmsis_rtos_v2/`), built for the official `native_sim` simulation port. Like `zephyr-linux`, this compiles with the host's own GCC (no cross-compiler) and runs as an ordinary Linux process — no ARM target hardware, QEMU, or vendor SDK (e.g. Keil RTX5) required.

This is **not** the `ZephyrThread` port (see `zephyr-linux` for that) — it's `CmsisRtos2Thread`/`CmsisRtos2Mutex`/`CmsisRtos2Semaphore`/`CmsisRtos2DelegateQueue`/`CmsisRtos2Clock` calling real `osThreadNew`/`osMutexAcquire`/`osSemaphoreAcquire`/`osMessageQueuePut`/`osKernelGetTickCount`, resolved by Zephyr's compatibility layer down to its native kernel. Zephyr's own `philosophers`/`timer_synchronization` CMSIS-RTOS2 samples build and run on `native_sim` in Zephyr's own CI, which is what makes this possible without any ARM toolchain.

Unlike `threadx-linux`/`freertos-linux`, this sample is **manual-only**: it is not wired into `01_fetch_repos.py`/`03_generate_samples.py`/`04_build_samples.py`. It needs the same `west` workspace as `zephyr-linux` — see that sample's README for setup.

## What this verifies (and what it doesn't)

Before this sample existed, every type in `port/os/cmsis-rtos2/` was written against documented CMSIS-RTOS2/CMSIS-Core API behavior but never built or run — see `CLAUDE.md`'s "ISR-Safe Locking" section. Building and running this sample verifies, for real:

* `CmsisRtos2Thread`: `osThreadNew`, priority get/set, exit/join via a semaphore, the watchdog registry ("Immortal Pattern"), and `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default) queue back-pressure — via `DelegateThreadsTests()` (Test 9), identical to `zephyr-linux`'s.
* `CmsisRtos2Mutex`/`CmsisRtos2RecursiveMutex`: real `osMutexNew`/`osMutexAcquire`/`osMutexRelease`, exercised by `Signal`, `MulticastDelegateSafe`, and this test file's own `dmq::Mutex`.
* `CmsisRtos2DelegateQueue`: real `osMessageQueuePut`/`osMessageQueueGet` with the native `msg_prio` priority-lane argument.
* `CmsisRtos2Clock`: real `osKernelGetTickCount()`-driven timekeeping, including — critically — from genuine **ISR context** (see "A real ISR-safety bug" below).

**Not verified here**: `CmsisRtos2CriticalSection`. That type drops to ARM Cortex-M CMSIS-Core intrinsics (`__get_PRIMASK()`/`__disable_irq()`/`__set_PRIMASK()`, real inline assembly like `cpsid i`/`mrs`) which do not exist for `native_sim`'s x86-64 host process — there is no CMSIS-Core package on this include path for that reason. This sample's `native_sim_stubs/cmsis_compiler.h` provides **no-op** stand-ins for those three functions purely to let the rest of the port compile and run; it does **not** exercise or validate real ISR-masking behavior in any way. That still needs real ARM Cortex-M hardware or QEMU + `arm-none-eabi-gcc`/ATfE with the genuine CMSIS-Core headers (the same gap `bare-metal-arm`/`atfe-armv7m-bare-metal` have). `CmsisRtos2CriticalSection` remains listed as unverified in `CLAUDE.md`.

## A real ISR-safety bug this sample surfaced (now fixed)

`CmsisRtos2Clock::now()` originally called `osKernelLock()`/`osKernelRestoreLock()` to make its rollover-tracking static state update atomic. That's wrong from ISR context: a CMSIS-RTOS2 `osTimer` callback under Zephyr's compatibility layer runs via a `k_timer` `expiry_fn`, which Zephyr's own `kernel.h` documents as running in genuine ISR context — exactly the context this sample's `SystemTimerHandler` (driving `Timer::ProcessTimers()`, which calls `Clock::now()`) runs in. Zephyr's `osKernelLock()` returns `osErrorISR` when called from an ISR (correctly refusing to lock the scheduler there) — but `osKernelRestoreLock()` unconditionally writes that return value into the *interrupted* thread's own scheduler-lock-nesting count **before** it checks for ISR context, corrupting live scheduler state on every single tick.

Symptom: Test 8's one-shot `Timer` fired its `OnExpired` callback every ~10ms forever instead of once. Root-caused by tracing through Zephyr's `subsys/portability/cmsis_rtos_v2/kernel.c`. Fixed by replacing the `osKernelLock()`/`osKernelRestoreLock()` pair in `CmsisRtos2Clock::now()` with `dmq::CriticalSection` (`CmsisRtos2CriticalSection`) — the library's own established ISR-safe primitive, used for exactly this "must work from both thread and ISR context" case everywhere else (see `CLAUDE.md`). Verified stable across repeated runs after the fix.

## Kconfig: CMSIS-RTOS2 object pools are tiny by default, and never reclaimed

Zephyr's CMSIS-RTOS2 compatibility layer hands out threads/mutexes/semaphores/message-queues from small static pools sized by Kconfig (`CMSIS_V2_THREAD_MAX_COUNT`, `CMSIS_V2_MUTEX_MAX_COUNT`, `CMSIS_V2_SEMAPHORE_MAX_COUNT`, `CMSIS_V2_MSGQ_MAX_COUNT` — all default to 5, except thread count which defaults to 15), and none of them are reclaimed when the corresponding `osXxxDelete()` is called — the underlying counters only ever increment. `CmsisRtos2Thread::CreateThread()` also never sets `osThreadAttr_t::stack_mem`, i.e. it always requests a *dynamically* allocated stack — the standard, spec-compliant way to use `osThreadNew()`, but Zephyr's layer refuses **every** such request unless `CMSIS_V2_THREAD_DYNAMIC_MAX_COUNT` (default **0**) is raised. This sample creates 10 distinct `dmq::os::Thread` instances and several `Signal`/`MulticastDelegateSafe` mutexes across its lifetime, so `prj.conf` raises all of these well above their defaults — see the comments there. Any real application using this port needs to size these for its own actual thread/mutex/semaphore/queue count, the same way FreeRTOS heap size or a ThreadX byte pool must be sized.

## Features Demonstrated

Identical test suite to `zephyr-linux`/`freertos-linux`/`threadx-linux` — showing that application code doesn't change across simulator ports, only `DMQ_THREAD` (this sample uses CMSIS-RTOS2 API calls directly — `osDelay`, `osTimerNew`/`osTimerStart`, `osThreadGetName`/`osThreadGetId` — rather than Zephyr's native kernel API, to stay a genuine CMSIS-RTOS2 application):

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (CMSIS-RTOS2 port) to safely send delegate messages from one thread to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple threads.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's `Timer` with a CMSIS-RTOS2 `osTimer`.
7.  **FullPolicy Stress Tests**: `DelegateThreadsTests()` (Test 9) — two concurrently active worker threads, `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default) behavior, and repeated cross-thread `AsyncInvoke()` calls. Every sub-test runs and passes.
8.  **PacedDispatch & TimerDelegate**: `TimerDelegateTests()` (Test 10) covers `dmq::util::PacedDispatch`'s at-most-one-in-flight gating logic and `dmq::util::TimerDelegate` dispatching to a real `dmq::os::Thread`.

## Prerequisites

Same `west` workspace as `zephyr-linux` — see that sample's README for full setup instructions (Python version note, `device-tree-compiler`, manifest filtering, etc.).

## Build Instructions

```bash
export ZEPHYR_BASE=/path/to/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host   # use the host's own gcc, not a Zephyr SDK cross-compiler
cd /path/to/zephyrproject
west build -b native_sim/native/64 /path/to/DelegateMQ/example/sample-projects/cmsis-rtos2-linux -d build
./build/zephyr/zephyr.exe
```

## Project Structure

* `main_delegate.cpp`: DelegateMQ test logic, using CMSIS-RTOS2 API calls directly (`osTimerNew`/`osTimerStart` drives `Timer::ProcessTimers()`; `osDelay` for pacing; `osThreadGetName`/`osThreadGetId` for the current-thread-name helper).
* `DelegateThreadsTests.cpp` / `TimerDelegateTests.cpp`: ported from `zephyr-linux`'s copies; only one line differs (`SetThreadPriority(osPriorityLow)` instead of a plain int, since CMSIS-RTOS2's `osPriority_t` is a real enum, not an int typedef).
* `prj.conf`: `CONFIG_CMSIS_RTOS_V2=y` plus its dependencies, the DelegateMQ C++ requirements shared with `zephyr-linux` (full libc++, RTTI, a real heap), and the raised object-pool limits explained above.
* `native_sim_stubs/cmsis_compiler.h`: sample-local, no-op verification stub — see "What this verifies" above. Never used by a real embedded build.
* `CMakeLists.txt`: same `find_package(Zephyr)`/`target_sources(app ...)` pattern as `zephyr-linux`, with `DMQ_THREAD_CMSIS_RTOS2` instead of `DMQ_THREAD_ZEPHYR`, plus the Zephyr `zephyr/portability` include path (for `cmsis_os2.h`) and the stub include path (listed first).

## Exiting

Same as `zephyr-linux`: `native_sim` runs as an ordinary Linux process with no kernel-level "stop scheduler" call, so `main()` calls `exit(0)` directly after the test suite completes.
