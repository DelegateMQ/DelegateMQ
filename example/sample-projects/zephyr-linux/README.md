# DelegateMQ Zephyr Linux Example

This sample project demonstrates how to integrate **DelegateMQ** into a **Zephyr** environment using Zephyr's official `native_sim` simulation port, which compiles a Zephyr application with your host's own GCC (no cross-compiler) and runs it as an ordinary Linux process — no target hardware required.

Unlike `threadx-linux`/`freertos-linux`, this sample is **manual-only**: it is not wired into `01_fetch_repos.py`/`03_generate_samples.py`/`04_build_samples.py`. Zephyr applications are built with `west build`, not a plain `cmake -B build`, and setting up a `west` workspace is a heavier, opt-in step — see Prerequisites below.

## Features Demonstrated

Identical test suite to `freertos-bare-metal`/`freertos-linux`/`threadx-linux` — showing that application code doesn't change across simulator ports, only `DMQ_THREAD`:

1.  **Unicast & Multicast Delegates**: Basic function pointer and member function wrapping.
2.  **Lambda Support**: Using C++ lambdas with captures as delegate targets.
3.  **Cross-Thread Dispatch**: Using `dmq::os::Thread` (Zephyr port) to safely send delegate messages from one thread to another.
4.  **Thread-Safe Signals**: Using `MulticastDelegateSafe` to handle connections and emissions across multiple threads.
5.  **RAII Connections**: Using `ScopedConnection` to automatically manage the lifetime of signal-slot connections.
6.  **RTOS Timers**: Integrating DelegateMQ's `Timer` with a Zephyr `k_timer`.
7.  **FullPolicy Stress Tests**: `DelegateThreadsTests()` (Test 9) exercises two concurrently active worker threads, `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default) behavior, and repeated cross-thread `AsyncInvoke()` calls. Unlike `threadx-linux`, every sub-test here runs and passes — Zephyr's port has no equivalent of the ThreadX Linux/GNU kernel deadlock that forces `threadx-linux` to skip its two-worker-thread tests.

## Prerequisites

*   **Linux** with GCC (native_sim compiles natively with the host toolchain; there is no Windows or macOS build of this sample)
*   **A west workspace with Zephyr.** Unlike ThreadX/FreeRTOS (plain sibling clones under the workspace root, handled by `01_fetch_repos.py`), Zephyr is not vendored automatically. Set one up once:
    ```bash
    pip install west
    west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.1.0 zephyrproject
    cd zephyrproject
    # The official manifest pulls dozens of hardware/vendor modules unrelated
    # to native_sim (HAL packages, crypto libs, RTOS bootloaders, etc.) --
    # several GB uncompressed if left unfiltered. Filter what you can:
    west config manifest.group-filter -- -hal,-optional,-babblesim,-crypto,-debug,-fs,-tee,-tools,-bootloader,-testing
    west config manifest.project-filter -- -nrf_hw_models   # depends on the hal group above; unrelated to native_sim
    west update
    pip install -r zephyr/scripts/requirements.txt
    ```
    Even filtered, expect several GB total (mostly Zephyr's own git history) — this is a one-time setup, not something 04_build_samples.py should be doing on every run.
*   **`device-tree-compiler`** (`sudo apt install device-tree-compiler` on Debian/Ubuntu) — Zephyr's build always generates a devicetree, even for `native_sim`, which has no real hardware to describe.
*   A Python version your installed `west` supports for `west build` (west 1.5.0's `build` extension uses a `pathlib.relative_to(walk_up=...)` call that requires Python 3.12+; on an older interpreter, either upgrade Python or check out an older Zephyr release — this sample was verified against Zephyr v4.1.0 with `west` 1.4.0 on Python 3.10).

No 32-bit multilib packages needed: build for `native_sim/native/64` specifically (see below), not plain `native_sim`, which defaults to a 32-bit build and would otherwise require `gcc-multilib`/`g++-multilib`.

## Build Instructions

```bash
export ZEPHYR_BASE=/path/to/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host   # use the host's own gcc, not a Zephyr SDK cross-compiler
cd /path/to/zephyrproject
west build -b native_sim/native/64 /path/to/DelegateMQ/example/sample-projects/zephyr-linux -d build
./build/zephyr/zephyr.exe
```

## Project Structure

*   `main_delegate.cpp`: Contains the DelegateMQ test logic and the periodic Zephyr `k_timer` that drives `Timer::ProcessTimers()`. No separate entry-point shim is needed — Zephyr calls this file's `main()` directly on its own automatically-created main thread once the kernel is initialized, unlike ThreadX (`tx_application_define()`) or FreeRTOS (an explicit `xTaskCreate()`'d task).
*   `prj.conf`: Kconfig settings this sample needs beyond Zephyr's defaults — see the comments in the file for why each one is required (full C++ standard library, RTTI, `k_poll`, a real heap, thread naming).
*   `CMakeLists.txt`: Unlike every other sample here, this does **not** call `add_executable()`. Zephyr's `find_package(Zephyr)` creates the executable target itself (named `app`); this file adds DelegateMQ's sources/includes to that target with `target_sources(app ...)`.

## How it Works

Zephyr's `west build` runs Kconfig and devicetree generation, then CMake, then compiles the kernel and application together into `zephyr.elf`/`zephyr.exe`. Once running, `main()` starts a periodic `k_timer` (drives `Timer::ProcessTimers()` every 10ms) and then runs the same 8-test sequence as `freertos-linux`/`threadx-linux`.

One of the tests creates a `dmq::os::Thread` object named "WorkerThread". When an asynchronous delegate is invoked on it, DelegateMQ wraps the call into a message and posts it to Zephyr message queues (`k_msgq`) via `ZephyrDelegateQueue` — see the file-level comment in `ZephyrDelegateQueue.h` for why it needs two queues (high/normal priority) and `k_poll()` rather than one, unlike FreeRTOS/ThreadX/CMSIS-RTOS2, which all have a native "send to front" primitive Zephyr's `k_msgq` lacks.

Test 9 (`DelegateThreadsTests()`, in `DelegateThreadsTests.cpp`) goes further: two worker threads alive at once, `.AsyncInvoke()` blocking-wait calls, and `dmq::FullPolicy`'s DROP/TIMEOUT/FAULT/default/default-queue-size behavior under load — every sub-test passes. Two real, previously-undetected bugs surfaced while getting this running, both now fixed:

*   **`DelegateAsyncWait.h` was never included for Zephyr or CMSIS-RTOS2** — `DelegateMQ.h`'s `#if` guarding that include listed STDLIB/WIN32/QT/FREERTOS/THREADX but not ZEPHYR/CMSIS_RTOS2, even though `dmq::Semaphore` (needed by `DelegateAsyncWait`) was already wired up for both — `.AsyncInvoke()` (used throughout this test) simply failed to compile before this fix.
*   **`ZephyrThread::ExitThread()` freed thread/stack memory before the kernel finished tearing the thread down** — it waited on a private exit semaphore that `Run()` gives just before returning, which only proves the thread function is *about* to return, not that Zephyr has finished unlinking it from scheduler bookkeeping. Freeing `m_stackMemory` (and reusing the enclosing `Thread` object's stack slot, since these are typically local variables) in that window left native_sim's cooperative scheduler with a dangling reference — observed as a newly created thread at the reused address never getting scheduled at all, hanging its consumer's queue forever. Fixed by adding `k_thread_join(&m_thread, K_FOREVER)`, Zephyr's documented race-free "wait for a thread to actually finish" primitive, after the semaphore wait.

Its `FullPolicy_Drop_DropsWhenFull()` sub-test relies on the consumer thread having a lower priority than the caller so a burst of non-blocking sends never stalls the publisher — Zephyr's default worker-thread priority (5, lower than the main thread's default 0) already satisfies this, but the test sets it explicitly (matching `threadx-linux`'s convention: lower number = higher priority) for clarity. `FullPolicy_UnlimitedQueue_DeliversAll()` also needed its send count reduced from the original (stdlib-port-derived) 50 down to fit `dmq::DEFAULT_QUEUE_SIZE` (20): unlike the desktop stdlib/Win32/Qt ports, `maxQueueSize=0` on every RTOS port (including Zephyr) means "use the 20-message default," not "truly unbounded," and Zephyr's purely cooperative scheduling (nothing yields except a blocking call) sends all 50 messages in one atomic burst with zero chances for the consumer to drain in between — unlike FreeRTOS's POSIX port, which gets incidental real preemption from the host OS during the same loop and happens to pass regardless. See the comment in `DelegateThreadsTests.cpp` for the full explanation.

### Exercises `dmq::CriticalSection`'s Zephyr implementation — including its ISR path

`Timer::GetLock()` returns `dmq::CriticalSection` (`port/os/zephyr/ZephyrCriticalSection.h`, `irq_lock()`/`irq_unlock()`), which — unlike `dmq::Mutex`/`RecursiveMutex` — is safe to acquire from genuine interrupt context. Zephyr's own `kernel.h` documents that a `k_timer` expiry callback "runs in ISR context," and this sample's `SystemTimerHandler` (which calls `Timer::ProcessTimers()`, which takes this lock) is exactly that callback — confirmed by the Test 8 output showing the timer-expired callback firing from whatever thread `native_sim` happened to interrupt (typically `idle`), not a dedicated timer thread/task the way ThreadX's system timer thread or FreeRTOS's Timer Service task run. This is the first time `ZephyrCriticalSection` has actually been exercised from ISR context rather than just reasoned through against Zephyr's documented API — though on `native_sim` that ISR context is itself simulated on top of the host OS, not a genuine hardware interrupt, the same caveat that applies to the other two Linux-hosted samples.

### Exiting

Zephyr has no kernel-level "stop scheduler" call, and `native_sim` runs as an ordinary Linux process, so after the test suite completes, `main()` calls `exit(0)` directly — the same approach `threadx-linux`/`freertos-linux` use. Returning normally from `main()` instead hits an edge case in `native_sim`'s own thread-table teardown unrelated to DelegateMQ.
