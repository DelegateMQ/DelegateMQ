# Changelog

All notable changes to DelegateMQ are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags. Changes are from the perspective of library users and integrators.

---

## [Unreleased]

### Added
- **`<Platform>DelegateQueue` wrapper classes** — `FreeRTOSDelegateQueue.h`, `ThreadXDelegateQueue.h`, `CmsisRtos2DelegateQueue.h`, `ZephyrDelegateQueue.h`. Each isolates a port's native OS queue calls (`xQueueSend`/`tx_queue_send`/`osMessageQueuePut`/`k_msgq_put`, buffer sizing, create/destroy) behind a small RAII type (`Create`/`Send`/`Receive`/`Size`/`DrainAndDelete`/`Destroy`), so `<Platform>Thread.cpp`'s `DispatchDelegate()`/`Run()` read as policy and dispatch logic rather than OS API plumbing. `FullPolicy`, timeout computation, watchdog, and stats stay in `<Platform>Thread.cpp` — the wrapper owns no policy decisions. FreeRTOS, ThreadX, and Zephyr verified via `freertos-linux`/`threadx-linux`/`zephyr-linux` (built and ran full test suite, both before and after this change, at every step). CMSIS-RTOS2 unverified — no SDK available here (see Fixed, below, for the Zephyr priority behavior change this introduced).
- New `example/sample-projects/threadx-linux/` sample: DelegateMQ delegate/Signal/Timer feature demo running on ThreadX's official Linux/GNU simulation port (`ports/linux/gnu`) — builds and runs as an ordinary Linux process, no cross-compiler or target hardware required. `01_fetch_repos.py` now clones `eclipse-threadx/threadx`; `External.cmake` links against ThreadX's own `azrtos::threadx` CMake target instead of hand-globbing sources (the pattern used for FreeRTOS).
- New `example/sample-projects/freertos-linux/` sample: the same DelegateMQ test suite running on FreeRTOS's official POSIX/Linux simulation port (`portable/ThirdParty/GCC/Posix`, each FreeRTOS task a real pthread) — also builds and runs as an ordinary Linux process. `03_generate_samples.py`'s FreeRTOS-Win32-simulator detection (`"freertos" in project_name`) is now scoped to exclude `*-linux` project names, so it doesn't try to configure this one with `-A Win32`.
- **`dmq::CriticalSection`** — a new portable lock type, distinct from `dmq::Mutex`/`RecursiveMutex`, for the narrow set of call sites (currently just `dmq::util::Timer::GetLock()`) that must work correctly from both thread and genuine ISR context, where an OS mutex fundamentally cannot (see Fixed, below). Implemented for every port: ThreadX (`ThreadXCriticalSection.h`, interrupt masking instead of a `TX_MUTEX` — built and run via `threadx-linux`); FreeRTOS (`FreeRTOSCriticalSection.h`, auto-detects thread vs. ISR context via `xPortIsInsideInterrupt()` on real ARM Cortex-M targets, `__arm__`/`__ARM_ARCH`-gated against a task-context-only fallback on the desktop simulator ports — task-context path built and run via `freertos-linux`); Zephyr (`ZephyrCriticalSection.h`, `irq_lock()`/`irq_unlock()` — built and run via `zephyr-linux` (`native_sim`), including its ISR path: a `k_timer` expiry callback runs in genuine ISR context per Zephyr's own documentation, and this sample drives `Timer::ProcessTimers()` from exactly that callback); CMSIS-RTOS2 (`CmsisRtos2CriticalSection.h`, `__disable_irq()`/`__enable_irq()` bypassing `osMutex` — reasoned through, **unverified**, no CMSIS-RTOS2 SDK/Cortex-M target available here); bare metal (`BareMetalCriticalSection.h`, mirrors the existing `BareMetalClock.h` PRIMASK save/disable/restore technique). Desktop (stdlib/Win32/Qt) stays aliased to `RecursiveMutex` — correct as-is, no ISR concept there. See the new "ISR-Safe Locking" section in `CLAUDE.md` for usage rules and the per-port verification table — it is narrowly scoped on purpose and must not be used as a general-purpose lock.
- New `example/sample-projects/zephyr-linux/` sample: the same DelegateMQ delegate/Signal/Timer test suite running on Zephyr's official `native_sim` simulation port (host GCC, no cross-compiler) — builds and runs as an ordinary Linux process via a `west` workspace. Manual-only: needs `west build`, not a plain `cmake -B build`, so it's excluded from `03_generate_samples.py`/`04_build_samples.py` rather than wired in like `freertos-linux`/`threadx-linux`. Includes Test 9 (`DelegateThreadsTests()`): two concurrently active worker threads and full `dmq::FullPolicy` coverage, ported from `test/unit-tests/` — unlike `threadx-linux`, every sub-test here passes (Zephyr's port has no equivalent of the ThreadX Linux/GNU kernel deadlock).

### Fixed
- **Zephyr port silently ignored `dmq::Priority::HIGH`** — `k_msgq_put()` has no priority/front-of-queue argument (unlike FreeRTOS's `xQueueSendToFront`, ThreadX's `tx_queue_front_send`, or CMSIS-RTOS2's `osMessageQueuePut` `msg_prio`), so a HIGH-priority message was previously enqueued FIFO like any other. `ZephyrDelegateQueue` now keeps two `k_msgq` instances (mirroring the desktop stdlib/Win32/Qt two-queue model) and always drains the high-priority one first, using `k_poll()` to block on both efficiently. Doubles queue memory relative to the old single-queue design. Now built and run via `zephyr-linux`.
- **`ZephyrThread.h` missing `#include <optional>`** — `CreateThread()`'s signature uses `std::optional<dmq::Duration>` without including its header; only ever compiled by luck via transitive inclusion on other ports, never on Zephyr itself until `zephyr-linux` was built.
- **`External.cmake` asserted a sibling `zephyr/` directory for `DMQ_THREAD_ZEPHYR`** — `set_and_check(ZEPHYR_ROOT_DIR "${DMQ_ROOT_DIR}/../../../zephyr")` hard-failed unless Zephyr was vendored as a plain sibling clone, matching the FreeRTOS/ThreadX pattern but inapplicable to Zephyr's `west`-workspace workflow (no such sibling directory is ever created). Replaced with a soft warning when `ZEPHYR_BASE` isn't set, since `find_package(Zephyr)` (run by `west build` before including `DelegateMQ.cmake`) is what actually needs to succeed.
- **`DelegateAsyncWait.h` (`.AsyncInvoke()`) was never wired up for Zephyr or CMSIS-RTOS2** — `DelegateMQ.h`'s `#if` guarding that include listed STDLIB/WIN32/QT/FREERTOS/THREADX only, even though both ports' `dmq::Semaphore` backend (added specifically to support it — see `DMQ_HAS_SEMAPHORE` in `DelegateOpt.h`) was already in place. `.AsyncInvoke()` simply failed to compile on either port until now; found while porting `DelegateThreadsTests.cpp` (which uses it) to `zephyr-linux`.
- **`ZephyrThread::ExitThread()` could free thread/stack memory before the kernel finished tearing the thread down** — it waited on a private exit semaphore that `Run()` gives just before returning, which only proves the thread function is *about* to return, not that Zephyr has unlinked it from scheduler bookkeeping yet. Freeing `m_stackMemory` in that window (and reusing the enclosing, often stack-allocated `Thread` object's memory for a new thread) left native_sim's cooperative scheduler with a dangling reference — reproduced as a newly created thread at the reused address never getting scheduled, hanging its consumer's queue forever. Fixed by adding `k_thread_join(&m_thread, K_FOREVER)`, Zephyr's documented race-free "wait for a thread to actually finish" primitive, after the semaphore wait; found and root-caused via `gdb` while bringing up `zephyr-linux`'s Test 9.

### Changed
- **`dmq::FullPolicy` consolidated** — was a byte-for-byte duplicated `enum class FullPolicy { DROP, FAULT, TIMEOUT }` in seven port headers (`freertos`, `threadx`, `zephyr`, `cmsis-rtos2`, `stdlib`, `win32`, `qt`). Now defined once in `DelegateOpt.h` as `dmq::FullPolicy`; every port header instead has `using FullPolicy = dmq::FullPolicy;`, so existing `dmq::os::FullPolicy::DROP` and unqualified `FullPolicy::DROP` call sites keep compiling unchanged.
- **`dmq::os::ThreadMsg` consolidated** — was a near-identical copy in six port directories (Qt's copy was unused dead code — Qt dispatches via its own signal/slot queued-connection mechanism, not `ThreadMsg`). Now a single `port/os/common/ThreadMsg.h`, included by every port except Qt.
- **Thread port files renamed to match each platform's existing prefixed siblings** — `port/os/<platform>/Thread.{h,cpp}` → `<Platform>Thread.{h,cpp}` (`FreeRTOSThread`, `ThreadXThread`, `ZephyrThread`, `CmsisRtos2Thread`, `StdlibThread`, `Win32Thread`, `QtThread`), matching the `<Platform>ThisThread.h`/`<Platform>CriticalSection.h`/`<Platform>DelegateQueue.h` naming already used per port. Each header adds `using Thread = <Platform>Thread;` inside `namespace dmq::os`, so every existing `dmq::os::Thread` reference across the library, tests, tools, and sample apps keeps compiling unchanged — confirmed by rebuilding and re-running `freertos-linux`, `threadx-linux`, the full `test/` suite, and `tools/`. `DelegateMQ.h`, `extras/util/NetworkEngine.h`, and `extras/databus/NetworkNode.h` (each carries its own copy of the platform-select `#if`/`#include` chain, since each embeds `dmq::os::Thread` by value and can be included directly without going through `DelegateMQ.h` first) were updated to the new filenames; `.github/workflows/embedded_ports_compile.yml` (compiles FreeRTOS/ThreadX/CMSIS-RTOS2 against real vendor headers) was updated too, though not re-run here (no local ARM toolchain).

### Fixed
- **ThreadX port 64-bit pointer truncation** — `port/os/threadx/Thread.cpp` passed `this` to the worker thread entry point via `reinterpret_cast<ULONG>(this)` in `tx_thread_create`'s `entry_input` parameter. `ULONG` is 32 bits on the Linux/GNU simulation port, silently truncating the pointer on 64-bit hosts. `Process()` now looks up the owning `Thread*` via `tx_thread_identify()` against a registry (`dmq::xmap<TX_THREAD*, Thread*>`) populated before `tx_thread_resume()`, which works regardless of pointer width.
- **`Timer::ProcessTimers()` could not safely run from a genuine ThreadX ISR** — its internal lock (`Timer::GetLock()`) was `dmq::RecursiveMutex`, a real `TX_MUTEX`. ThreadX forbids both creating (`tx_mutex_create`) and acquiring (`tx_mutex_get`) a mutex from ISR context (`TX_CALLER_ERROR`, checked on every call, not just creation) — so driving `ProcessTimers()` from a ThreadX software-timer callback faulted on the very first tick, and would fault identically from a true hardware ISR. `Timer::GetLock()` now returns `dmq::CriticalSection` (new, see Added), which on ThreadX disables/restores interrupts directly instead of touching a `TX_MUTEX` — safe from both thread and ISR context, no workaround needed.
- **`ThreadXClock` tick/millisecond mismatch** — `now()` returned raw `tx_time_get()` ticks labeled as milliseconds, without scaling by `TX_TIMER_TICKS_PER_SECOND`. At the default 100Hz tick rate this made every duration measured against the clock (including `dmq::util::Timer` expirations) run 10x slower than requested.
- **Incorrect `ProcessTimers()` "call from a hardware ISR" guidance under an RTOS** — `llms.txt` recommended a hardware ISR as the preferred `Timer::ProcessTimers()` call site on RTOS builds generally. `Timer::GetLock()` was a real OS mutex on every RTOS port, and OS mutexes cannot be acquired from a genuine hardware ISR on any of them. The hardware-ISR recommendation is now scoped to bare-metal/no-RTOS builds and ThreadX (both genuinely ISR-safe, see Added); FreeRTOS, Zephyr, and CMSIS-RTOS2 are documented to require a thread/task context instead — a dedicated high-priority task, or an RTOS software-timer callback, until they get a real `CriticalSection` implementation too.
- **`ThreadXCriticalSection` referenced a port-specific macro-local variable name** — `lock()`/`unlock()` used `TX_INTERRUPT_SAVE_AREA`/`TX_DISABLE`/`TX_RESTORE`, whose generated local variable is named differently per ThreadX port (`interrupt_save` on most ARM ports, `tx_saved_posture` on Linux/GNU) and is only valid within the function that declared it — broken by construction once `lock()` and `unlock()` are separate calls. Now uses `tx_interrupt_control()`, ThreadX's portable public interrupt-lockout service, which carries the posture across the call boundary itself.
- **`delegateAsyncWait2` bound to the wrong worker thread** — a copy-paste bug in `test/unit-tests/DelegateThreadsTests.cpp`'s `FreeTests()`/`MemberTests()`/`MemberSpTests()`/`FunctionTests()`: `delegateAsyncWait2` was `MakeDelegate(..., workerThread1, ...)` instead of `workerThread2`, so `workerThread2` was never exercised via `AsyncInvoke()`. Silent on the desktop stdlib port; surfaced while porting the same file to the new RTOS samples below (fixed there too).
- **ThreadX Linux/GNU build serialized every kernel critical section through one mutex** — `ports/linux/gnu/CMakeLists.txt` unconditionally enables `TX_LINUX_DEBUG_ENABLE`, which wraps every `TX_DISABLE`/`TX_RESTORE` kernel-wide in a call to `_tx_linux_debug_entry_insert()` serializing through a single global mutex — a real contention risk under genuine multi-thread load. `External.cmake` now strips it via `-UTX_LINUX_DEBUG_ENABLE` rather than patching the vendored ThreadX source.

### Added
- `example/sample-projects/{freertos-linux,threadx-linux}/DelegateThreadsTests.cpp` (Test 9): cross-thread dispatch and `dmq::FullPolicy` (DROP/TIMEOUT/FAULT/default/unlimited-queue) coverage against each RTOS's real `dmq::os::Thread` port, ported from `test/unit-tests/`. Required three fixes beyond the test content itself, all now applied to both samples: worker threads are construct-on-first-use functions rather than file-scope `static Thread` objects (a file-scope static would construct before the RTOS scheduler starts); `dmq::ThisThread::sleep_for()` replaces raw `std::this_thread::sleep_for()` so sleeps yield to the RTOS scheduler instead of just the OS; and the DROP-policy test's consumer thread is given a lower priority than the caller so its "publisher never stalls" timing assertion holds under real preemption (FreeRTOS and ThreadX use opposite priority-number conventions). `freertos-linux` runs the full test — verified passing across repeated runs. `threadx-linux` only runs `ThreadFullPolicyTests()` — see Known Issues.
- `example/sample-projects/{freertos-linux,threadx-linux,zephyr-linux}/TimerDelegateTests.cpp` (Test 10): `dmq::util::PacedDispatch` and `dmq::util::TimerDelegate` coverage, ported from `test/unit-tests/TimerDelegateTests.cpp`, against each RTOS's real `dmq::os::Thread` port. Same construct-on-first-use-thread and `dmq::ThisThread::sleep_for()` treatment as Test 9. `TimerDelegate_WithTimer_DispatchesToThread()` no longer spins up its own thread to drive `Timer::ProcessTimers()` the way the desktop test does -- each sample already runs a periodic system timer for the same purpose (the one Test 8 relies on), and on `zephyr-linux` a raw `std::thread` calling into Zephyr kernel APIs from outside native_sim's cooperative scheduling would risk the same class of hazard as Test 9's `dmq::Mutex` fix. All three samples run every sub-test and pass, verified across repeated runs.

### Changed
- **`test/stress_test*.cpp` moved to `test/stress-tests/`** — now builds as its own static library (`stress_tests_lib`) linked into `delegate_app`, matching the existing `unit-tests`/`sample-code` subdirectory convention instead of living loose alongside `main.cpp`.

### Known Issues
- **`threadx-linux`: two concurrently active `dmq::os::Thread` instances deadlock ThreadX's own Linux/GNU port kernel.** Root-caused via `gdb` to a worker thread's own startup blocking forever in `_tx_thread_interrupt_control()` on ThreadX's internal `_tx_linux_mutex` (a real `PTHREAD_MUTEX_RECURSIVE`, so not simple same-thread self-nesting) — reproduces identically with `TX_LINUX_DEBUG_ENABLE` stripped and with `dmq::util::Timer`'s periodic ticking disabled entirely, so it isn't DelegateMQ's `Timer`/`CriticalSection` code. Appears to be a genuine concurrency bug in vendored ThreadX's kernel sources, not in DelegateMQ. `DelegateThreadsTests()`'s `FreeTests()`/`MemberTests()`/`MemberSpTests()`/`FunctionTests()` (which need two worker threads alive together) are commented out in `example/sample-projects/threadx-linux/DelegateThreadsTests.cpp` until this is understood; see that file's header comment for the full investigation.

---

## [2.0.2] - 2026-07-22

### Added
- New GitHub Actions workflows for CMake embedded and CMake stress tests.
- `embedded_ports_compile.yml` — compiles the FreeRTOS, ThreadX, and CMSIS-RTOS2 thread ports against their real vendor headers (ARM GNU toolchain, compile-only, no execution) on every push/PR. Previously none of these ports were exercised by any CI workflow.

### Fixed
- Fixed Node 20 deprecation warnings in GitHub Actions by updating `actions/checkout` to `v7`.
- DataBus QoS bugs and associated unit tests.
- Build errors in `NetworkEngine.h` and the Cellutron safety example CMake configuration.
- Addressed multiple code review findings across `DelegateAsyncWait`, `DelegateRemote`, fixed-block allocator (`xnew.h`), `NetworkEngine`, and embedded thread ports (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2).
- **RTOS thread self-exit dangling pointer** — `m_selfExitPtr` was never reset to `nullptr` before `Thread::Run()` returned in the FreeRTOS, ThreadX, Zephyr, and CMSIS-RTOS2 ports, leaving it pointing at a destroyed stack variable (caught by GCC `-Wdangling-pointer` on a real embedded build). Now matches the pattern already used correctly in the stdlib port.
- **ThreadX port compile correctness** — `port/os/threadx/Thread.cpp` referenced `ULONG_PTR` and a `TX_THREAD::tx_thread_user_data` member, neither of which exist in the real ThreadX API; `this` is now passed through the standard `tx_thread_create` `entry_input` / `Process(ULONG instance)` mechanism instead. Also fixed `TX_NULL` not implicitly converting to `CHAR**` under C++ in `tx_queue_info_get`.
- **`DelegateOpt.h` include order** — `extras/util/Fault.h` (defining `ASSERT_TRUE`) is now included before `ThreadXMutex.h` / `ThreadXConditionVariable.h`, which call it from their constructors.
- **CMSIS-RTOS2 port** — `DispatchDelegate()` called `ThreadMsg::SetEnqueueTime()` unconditionally, but that method only exists under `DMQ_DATABUS_TOOLS`; now guarded consistently with the other RTOS ports.
- Test fixes: `AllocatorTests.cpp`'s xnew leak-on-throw regression check now only runs when the library actually guards against a throwing constructor (`__cpp_exceptions && !DMQ_ASSERTS`); `DataBusQosTest.cpp`'s `ThrowOnMoveOnce` helper gained a default constructor, fixing a GCC 14 template-instantiation compile failure.

### Changed
- Hardening updates across the core library and transport layer.
- Minor documentation updates in `README.md` and `llms.txt`.

---

## [2.0.1] - 2026-07-18

### Added
- `RemoteChannel` constructor accepts an optional `DelegateRemoteId` — send-only channels no longer require a placeholder `Bind()`; construct with the ID and invoke.
- `DataBus::EnableContinuousErrors(bool)` — DataBus errors are now latched per topic and error code (reported once); continuous mode re-reports every occurrence.
- `DelegateError::ERR_TYPE_MISMATCH` — a publish/subscribe topic type mismatch is now reported through the error signal for diagnosability before faulting.

### Fixed
- **Remote delegate initialization** — fixed a bug across test and sample projects where `MakeDelegate` assignments on a `DelegateMemberRemote` inadvertently overwrote explicitly configured stream, serializer, and error handlers, causing `ERR_NO_SERIALIZER` runtime crashes.
- **`MulticastDelegate` reentrancy** — broadcast now invokes a snapshot (SBO) of the delegate list; adding or removing delegates from within a callback no longer causes infinite loops. `Remove()` now erases all duplicate targets instead of only the first occurrence.
- **`RemoteArg<Arg*>` dangling pointer** — added missing copy and move constructors, fixing a dangling pointer when remote argument wrappers were copied.
- **DataBus remote topic registration** — registering topics now records the topic-to-remote-ID mapping on the participant (`AddRemoteTopic`), fixing inter-node topic forwarding.
- Thread-unsafe `printf` output in Windows sample projects.
- Linux build error and clang strict-warning findings (`-Wunsafe-buffer-usage` in the `Signal` snapshot buffer).
- **bare-metal-remote sample** — Fixed compile error by updating `BareMetalDispatcher` to match the new `IDispatcher::Dispatch` signature.

### Changed
- **Exception Handling & Documentation** — explicitly document `std::runtime_error` and `std::invalid_argument` usage in the library. All OS port `Thread::Process()` loops now explicitly catch these exceptions alongside `std::bad_alloc` when `DMQ_ASSERTS` is disabled.
- `std::shared_ptr` argument rules on asynchronous delegates refined: `const std::shared_ptr<T>&` and `const std::shared_ptr<T>*` are now accepted; non-const reference and pointer forms are rejected at compile time with a clearer diagnostic. Remote delegates still require `std::shared_ptr` by value.
- DataBus internals simplified: `TopicForwarder` helper removed in favor of thin lambda captures on `Participant::RegisterHandler`.
- `ISerializer::Read()` documentation now explains the `const_cast` requirement when deserializing `const T*` arguments.
- Documentation overhaul: README condensed (Motivation/Advantages rewritten, Modular Architecture folded into Overview) and gains a Remote Delegates example; `llms.txt` corrected (`DMQ_TOOLS` option name, updated remote delegate pattern).
- `IDispatcher::Dispatch` signature updated to accept `dmq::xostringstream&` and an optional `uint16_t* outSeqNum` to retrieve transport sequence numbers.
- `Thread` priority queue refactored: replaced `std::priority_queue` with two `std::deque`s (`m_highQueue` and `m_normalQueue`) to guarantee strict FIFO execution of messages within the same priority level. `Priority::LOW` has been removed.

---

## [2.0.0] - 2026-06-17

### Fixed
- **ThreadMonitor deadlock** — `Disable()` no longer holds `m_mutex` while joining the monitor thread; `MonitorLoop` acquires that mutex after its sleep, causing a deadlock in the prior implementation.
- **RetryMonitor stranded entry** — when `TransportMonitor` fires a TIMEOUT while the initial `Send()` is still executing, the sequence number is now re-registered so monitoring continues rather than leaking the entry permanently.
- **NetworkEngine shutdown race** — `Stop()` now sets the exit flag before closing transports (to unblock a blocking `Receive()`) and before joining the receive thread, preventing a narrow window where the receive thread could loop after the transport was closed.
- **Signal `Clear()` correctness** — `Clear()` now marks the old state `alive = false` under the lock, preventing a `ScopedConnection` destructor from accessing a dead delegate list.
- **`DelegateAsync::m_sync` data race** — changed from `bool` to `std::atomic<bool>` to prevent a UB data race between the caller thread and the target thread.
- Compile error in `NetworkEngine.h` under `DMQ_TRANSPORT_SERIAL_PORT` builds (missing `//` on `Reliability Layers` comment).
- `printf` thread safety in sample projects.
- `dmq-monitor` crash when message count is high.

### Changed
- `Thread` (stdlib): statistics (`m_queueDepthMax*`, latency, invoke metrics) are now protected by a dedicated `m_statsMutex` separate from the queue mutex, reducing lock contention on the dispatch hot path.
- `DataBus::RegisterSerializer` gains a `std::shared_ptr<ISerializer<void(T)>>` overload so callers can transfer ownership of heap-allocated serializers; the existing reference overload is preserved.
- `NetworkNode`: receive thread storage changed from `std::unique_ptr<Thread>` to `std::optional<Thread>`, eliminating one heap allocation per node lifetime.
- All `std::lock_guard` usages in `extras/` replaced with `dmq::LockGuard<T>` and direct `#include <mutex>` removed from `extras/` headers, restoring embedded-target build compatibility.
- MSVC parallel compilation (`/MP`) enabled by default.
- `xallocator` reference count is now `std::atomic<int32_t>`.
- `ThreadMonitor::Enable()` now holds `m_mutex` while constructing the monitor thread, closing a data race with concurrent `Disable()` calls.

---

## [1.1.8] - 2026-05-29

### Added
- `[[nodiscard]]` attribute on `ScopedConnection` returned by `Signal::Connect()` — discarding the connection now produces a compiler warning.
- Unit tests for fixed-block allocator paths and DataBus scenarios.

### Fixed
- Deadlock in `Signal` / DataBus path when a subscriber callback triggered re-entrant signal operations.
- Excessive heap usage in the fixed-block allocator path.
- Linux and Cellutron sample app build issues.

### Changed
- Lambda usage eliminated from DataBus internals; replaced with member delegates to keep captures small and allocation-free.
- Fixed-block allocator (`xmake_shared`, `xlist`, `xmap`) usage expanded across more internal structures.
- Improved error reporting and `FaultHandler` debug output.
- Cellutron sample app now builds and runs on Linux.

---

## [1.1.7] - 2026-05-19

### Added
- `dmq::ScopedLock` RAII helper.
- `DelegateMQConfig.h` — user-editable configuration header that separates tuneable constants from library internals; reduces per-thread stack usage.
- Sanitizers CI workflow (AddressSanitizer, UBSan).
- dmq-spy tool: improved data capture and filtering.

### Fixed
- Cross-thread race in `Timer` that could cause missed or double-fired expirations.
- FreeRTOS queue sizes tuned to reduce memory pressure.
- Sanitizer build errors.

### Changed
- DataBus error reporting improved with per-participant error callbacks.
- STM32 FreeRTOS sample app updated to use DataBus.

---

## [1.1.6] - 2026-05-07

### Added
- **`ThreadMonitor`** — new tool that publishes per-thread queue depth, latency, and invoke-time metrics to a DataBus topic; consumed by the `dmq-thread` dashboard.
- **`MakeTimerDelegate()`** — timer-safe async delegate factory guaranteeing at most one in-flight dispatch per timer tick, preventing queue flooding on slow handlers.
- **`MonotonicGuard`** — sequence-counter utility for detecting and discarding stale LVC rewind deliveries at the subscriber.
- TCP transport and example.
- C++ interop library for Python and C# with DataBus support.
- More thread invoke timing metrics (average, max-window, max-all).

### Fixed
- Multiple DataBus deadlock paths when a delegate was invoked while holding internal locks.
- Timer flooding: a slow timer handler could enqueue unbounded messages to the target thread; `MakeTimerDelegate` solves this.
- Watchdog reliability improvements.
- Linux interop build and execution.

### Changed
- `FullPolicy::BLOCK` replaced by `FullPolicy::TIMEOUT` across all thread ports; blocking indefinitely on a full queue is no longer a supported policy.

---

## [1.1.5] - 2026-04-24

### Added
- **DataBus** — topic-based publish/subscribe middleware layer (major feature). Supports local and remote topics, Last Value Cache (LVC), Lifespan, Min Separation, and Deadline QoS policies.
- `IsCurrentThread()` API on `IThread`.
- Win32 native thread port (`port/os/win32/`).
- DataBus multicast support.
- Cellutron multi-node demo (GUI / Controller / Safety nodes over DataBus).
- dmq-spy and dmq-monitor DataBus diagnostic tools.
- `FullPolicy` per-thread queue-full handling (DROP / TIMEOUT / FAULT).
- More unit tests.

### Fixed
- Signal/slot: subscribers could be invoked after disconnect under concurrent access.
- Various Linux and FreeRTOS build fixes.

### Changed
- `xmake_shared` introduced for allocator-aware `shared_ptr` construction.
- Fixed-block allocator coverage expanded across DataBus internals.
- Namespace consolidation.

---

## [1.1.4] - 2026-04-03

### Added
- New DataBus features and namespace updates.
- Linux UDP example.
- Automated sample project build scripts.
- Interop DataBus support for C# and Python.

### Fixed
- Linux build errors introduced by namespace changes.

---

## [1.1.3] - 2026-03-25

### Fixed
- Minor Linux build and lambda handling corrections.

---

## [1.1.2] - 2026-03-22

### Added
- `dmq::Duration` type alias for `std::chrono::milliseconds`; standardises time units across the API.

### Fixed
- `DelegateAsyncWait`: return value was not reliably propagated back to the caller under certain thread scheduling conditions.

---

## [1.1.1] - 2026-02-10

### Added
- `RemoteEndpoint` class simplifies binding a local handler to a remote delegate ID.

### Fixed
- Remote delegate bugs affecting argument forwarding on certain compilers.
- Linux build.

---

## [1.1.0] - 2026-01-15

### Added
- **`Signal<Sig>` / slot** — thread-safe multicast signal with RAII `ScopedConnection` disconnect handles.
- **`ReliableTransport`** — wraps any `ITransport` to add acknowledgement and retry logic.
- **`RetryMonitor`** — automatic retransmission manager with configurable retry budget and timeout detection.
- CRC16 validation for serial-port transports.
- ARM Cortex-M bare-metal sample application.
- STM32 Discovery board FreeRTOS sample application.
- Stress tests for remote delegates under concurrent load.
- FreeRTOS max thread queue size configuration.
- Thread queue size limit enforcement.
- Precompiled header support to speed compile times.

### Fixed
- `WAIT_INFINITE` constant value on Windows vs. Linux.
- Remote delegate serialization edge cases.
- Various Linux and FreeRTOS build issues.

### Changed
- Cross-platform compatibility improvements across all thread and transport ports.
- Documentation expanded with TCP and serial port examples.

---

## [1.0.9] - 2025-12-26

### Fixed
- Linux build errors in network and sample code.
- ZeroMQ transport: separated send and receive sockets to prevent cross-thread interference.

### Changed
- `NetworkMgr` refactored to expose a generic `RemoteInvoke()` interface.
- `ITransport::Receive` interface updated for cleaner integration with the dispatcher layer.
- `msgpack` dependency pinned to a specific version for reproducible builds.

---

## [1.0.8] - 2025-04-13

### Added
- Watchdog feature built into `dmq::os::Thread`: detects deadlocked or unresponsive threads and calls a user-provided `WatchdogHandler`.
- `spdlog` integration for optional structured debug logging.
- Async delegate message priority queue support.
- Python interoperability example.
- Safe timer example demonstrating `MakeTimerDelegate` usage.
- `dmq::SharedDelegate` classes for shared-ownership delegate scenarios.
- NNG (nanomsg-next-gen) transport and example.
- Cereal and Bitsery serialization sample projects.

### Fixed
- `chrono::system_clock` replaced with `chrono::steady_clock` in `Timer` — eliminates skew from wall-clock adjustments.
- `DelegateRemote` now handles arbitrary argument counts (previously limited to 5).
- Delegates can safely be removed from a container during a callback without iterator invalidation.
- Multi-threaded timing corner cases in `DelegateAsyncWait`.

### Changed
- Improved serializer compile-time type checking.
- Documentation: new porting guide, API reference updates.

---

## [1.0.7] - 2025-03-31

### Fixed
- Remote delegate bugs affecting return-value forwarding.
- Linux build.

---

## [1.0.6] - 2025-03-27

### Added
- `dmq-monitor` and `dmq-spy` DataBus tools initial integration.
- Cellutron example: dmq-spy and dmq-monitor support added.

### Fixed
- Linux build errors from namespace changes.
- Sample project build issues.

---

## [1.0.5] - 2025-03-18

### Fixed
- Linux build errors.

### Changed
- Namespace updates across transport and serializer layers.
- Sample app fixes.

---

## [1.0.4] - 2025-03-10

### Added
- New ARM bare-metal demo project (Keil).
- C# and Python interop DataBus support.
- `add_compile_options(/MP)` for parallel MSVC builds (restored for desktop builds).

### Changed
- Directory structure reorganised; tools moved into the DelegateMQ repository.
- Build scripts automated for sample projects.
- Documentation badges and images updated.

---

## [1.0.3] - 2025-03-08

### Fixed
- Minor Linux build corrections.

---

## [1.0.2] - 2025-03-05

### Added
- `UnicastDelegateSafe` — thread-safe single-slot delegate container.
- Timer `once` flag: `Start(timeout, once=true)` fires exactly once then self-disables.

### Fixed
- Thread `ExitThread()` reliability on shutdown.
- Build errors on Linux and Windows.

---

## [1.0.1] - 2025-02-24

### Fixed
- `RemoteDelegate` error handling: transport errors now surface through the error handler delegate rather than being silently dropped.

### Changed
- Documentation improvements.

---

## [1.0.0] - 2025-02-17

Initial public release.

### Added
- `dmq::Delegate<Sig>` — synchronous direct-call delegate.
- `dmq::DelegateAsync<Sig>` — non-blocking async delegate (fire-and-forget).
- `dmq::DelegateAsyncWait<Sig>` — blocking async delegate with optional timeout and return value.
- `dmq::DelegateRemote<Sig>` — remote delegate serializing arguments over a transport.
- `dmq::MulticastDelegate` / `dmq::MulticastDelegateSafe` — broadcast containers.
- `dmq::UnicastDelegate` — single-slot delegate container.
- Thread ports: stdlib, Win32, FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, Qt, bare-metal.
- Transport ports: ZeroMQ, NNG, MQTT, TCP (Win32/Linux), UDP (Win32/Linux/multicast), serial, Win32 named pipe.
- Serializer adapters: MessagePack, RapidJSON, Cereal, Bitsery, custom serialize.
- Fixed-block allocator (`DMQ_ALLOCATOR`) for embedded/RTOS targets.
- CMake build system with platform auto-detection.
- CI: Ubuntu, Windows, Clang, sanitizers workflows.
- MQTT, ZeroMQ, NNG, TCP, UDP, serial example projects.
