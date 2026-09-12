# Changelog

All notable changes to DelegateMQ are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags. Changes are from the perspective of library users and integrators.

---

## [Unreleased]

## [2.0.3] - 2026-09-11

### Added
- **`DMQ_FORCE_OPTIMIZE_DEBUG`** CMake option (off by default) forces GCC/Clang's `-Os` on DelegateMQ's template-heavy headers even inside an otherwise unoptimized Debug build, via `#pragma GCC optimize` push/pop around each header's `namespace dmq`. At `-O0`, none of the library's thin wrapper/accessor calls get inlined, so a meaningful chunk of per-instantiation cost is otherwise left on the table with no way to recover it short of building the whole application at a higher optimization level. Measured ~2.7% flash reduction (`stm32-freertos`, GCC `-Os`) — roughly 6x the combined savings of the per-signature consolidation work below. No effect on MSVC: `#pragma optimize` cannot elevate above the command-line `/Od` baseline and is unsupported on x64 targets, so it's a documented no-op there (confirmed byte-for-byte identical on `cellutron_controller`) rather than a partial workaround.
- **`dmq::CriticalSection`** — new portable ISR-safe lock type for the narrow set of call sites (currently `dmq::util::Timer`) that must work from both thread and hardware ISR context, where OS mutexes cannot. Implemented for all RTOS ports (ThreadX, FreeRTOS, Zephyr, CMSIS-RTOS2, NuttX) and bare metal; desktop stays aliased to `RecursiveMutex`. See `CLAUDE.md`'s "ISR-Safe Locking" section for usage rules and per-port verification status.
- **NuttX port** (`DMQ_THREAD_NUTTX`, `port/os/nuttx/`) — `NuttXThread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`/`ThisThread`/`CriticalSection`, using NuttX's real POSIX primitives directly (`pthread_create`/`pthread_join`, POSIX `mqueue`, `sem_t`) rather than bespoke wrappers, plus `up_irq_save()`/`up_irq_restore()` for the ISR-safe critical section. Wired into `DelegateOpt.h`/`DelegateMQ.h`/`Defaults.cmake`/`Port.cmake`.
- New `example/sample-projects/nuttx-sim/` sample — verifies the NuttX port above against NuttX's own `sim` board (a native host process, no cross-compiler or hardware), the same low-cost verification model as `zephyr-linux`/`cmsis-rtos2-linux`. All 10 tests pass, including cross-thread `FullPolicy`/`PacedDispatch` stress tests and a one-shot `Timer` genuinely exercising `NuttXCriticalSection`'s `up_irq_save()`/`up_irq_restore()` from application code. See the sample's README for the toolchain gap this surfaced (Ubuntu's own NuttX libc++ default, uClibc++, has an unreachable upstream source archive) and two NuttX build-system quirks worked around (not DelegateMQ bugs).
- **`<Platform>DelegateQueue` wrapper classes** (FreeRTOS/ThreadX/CMSIS-RTOS2/Zephyr) isolate each port's native OS queue API behind a small RAII type, keeping `<Platform>Thread.cpp` focused on policy/dispatch logic rather than OS plumbing.
- New RTOS simulator samples — each builds and runs as an ordinary host process, no cross-compiler or target hardware required: `example/sample-projects/threadx-linux/`, `freertos-linux/`, `zephyr-linux/`. Includes ported unit test coverage for cross-thread dispatch/`FullPolicy` (Test 9) and `Timer`/`PacedDispatch` (Test 10) on each.
- New `example/sample-projects/zephyr-udp-serializer/` sample — remote delegate call over real UDP loopback via `ZephyrUdpTransport`, the first time this transport has been built and run.
- New `example/sample-projects/databus-zephyr/` sample — Zephyr DataBus server/client counterpart to `databus-freertos`, verified with real bidirectional UDP traffic (sensor readings, alarm state, rate-control commands).
- New `example/sample-projects/cmsis-rtos2-linux/` sample — verifies `DMQ_THREAD_CMSIS_RTOS2` (`CmsisRtos2Thread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`) against a real CMSIS-RTOS2 implementation (Zephyr's own `CONFIG_CMSIS_RTOS_V2` compatibility layer) on `native_sim`, without any ARM cross-toolchain, QEMU, or hardware. `CmsisRtos2CriticalSection` itself remains unverified (its CMSIS-Core intrinsics need real Cortex-M); see the sample's README and `CLAUDE.md`'s "ISR-Safe Locking" section.
- **`example/sample-projects/bare-metal-arm/` (Test 6)** — a real Cortex-M4 SysTick interrupt now drives `dmq::util::Timer::ProcessTimers()`, verifying `dmq::os::BareMetalCriticalSection` from genuine ISR context (QEMU `mps2-an386`) for the first time. See `CLAUDE.md`'s "ISR-Safe Locking" section and the sample's README for the two real bugs this surfaced (below).
- **First RISC-V port**: `dmq::os::BareMetalCriticalSection`/`BareMetalClock` (`DMQ_THREAD_NONE`) now branch for RISC-V (`mstatus.MIE` via `csrrci`/`csrs`) alongside the existing ARM implementation. New `example/sample-projects/bare-metal-riscv/` sample (RV32IMC, QEMU `virt` machine, direct machine-mode boot via the xPack RISC-V Embedded GCC toolchain — Ubuntu's `gcc-riscv64-unknown-elf` apt package has no libstdc++ for this target, which DelegateMQ's `dynamic_cast`-based `Delegate::Equal()` genuinely needs) verifies both, driven by the CLINT machine timer through a hand-written trap vector (RISC-V, unlike ARM's NVIC, saves nothing automatically on trap entry). See the sample's README for two real bootstrapping issues this surfaced (the toolchain's generic `crt0.o` assumes a hosted-semihosting boot convention incompatible with a direct `-bios none` boot; `libc.a` unconditionally pulls in `_getentropy_r`).

### Fixed
- **Zephyr port silently ignored `dmq::Priority::HIGH`** — `k_msgq_put()` has no priority/front-of-queue argument; `ZephyrDelegateQueue` now keeps two `k_msgq` instances (mirroring the desktop two-queue model) and always drains the high-priority one first.
- **`Timer::ProcessTimers()` could not safely run from a genuine ThreadX ISR** — `Timer::GetLock()` now uses `dmq::CriticalSection` instead of a real `TX_MUTEX` (ThreadX forbids mutex create/acquire from ISR context). `llms.txt`'s ISR call-site guidance corrected to match: ThreadX and bare-metal are genuinely ISR-safe; FreeRTOS/Zephyr/CMSIS-RTOS2 still need thread/task context.
- **`ThreadXClock` tick/millisecond mismatch** — `now()` returned raw `tx_time_get()` ticks unscaled by `TX_TIMER_TICKS_PER_SECOND`, making every `Timer` duration run 10x slow at the default 100Hz tick rate.
- **ThreadX port 64-bit pointer truncation** — `this` was passed to the worker thread entry point via a 32-bit `ULONG`, silently truncating the pointer on 64-bit hosts. `Process()` now looks up the owning `Thread*` via `tx_thread_identify()` against a registry instead.
- **ThreadX Linux/GNU build serialized every kernel critical section through one debug mutex** (`TX_LINUX_DEBUG_ENABLE`) — now stripped from the vendored build via `External.cmake`.
- **`ThreadXCriticalSection` referenced a macro-local variable name that differs per ThreadX port** and isn't valid across separate `lock()`/`unlock()` calls — now uses the portable `tx_interrupt_control()` service instead.
- **`ZephyrThread::ExitThread()` could free thread/stack memory before the kernel finished teardown**, leaving a dangling scheduler reference — fixed by adding `k_thread_join()` after the exit semaphore wait.
- **`.AsyncInvoke()` (`DelegateAsyncWait`) never compiled on Zephyr or CMSIS-RTOS2** — missing from `DelegateMQ.h`'s include guard despite both ports already having the required `dmq::Semaphore` backend.
- **RTOS simulator samples silently compiled in desktop-only `DataBus`/`NetworkConnect.h` code** because the default keyed off the *host* build platform rather than the selected `DMQ_THREAD_*` — caused header collisions with a target's native network stack (e.g. Zephyr's socket headers) once a sample also needed real networking.
- **`ZephyrUdpTransport`** — rewritten to use Zephyr's `zsock_*` API throughout (mixing it with unprefixed BSD names required a POSIX compatibility layer that collided with the host's real pthread headers on `native_sim`); `Create()` also failed unconditionally until `CONFIG_NET_CONTEXT_RCVTIMEO` was enabled in `prj.conf`.
- **`DelegateMQ.h`'s `DMQ_TRANSPORT_THREADX_UDP` include pointed at a nonexistent directory** (`threadx-udp` instead of `netx-udp`) — never caught because nothing in the repo had built it before.
- **`databus-freertos/client/Client.h` hardcoded `Win32UdpTransport`**, failing to compile on Linux despite its own doc comment — now uses the portable `UdpTransport` alias; also reused for `databus-zephyr/client`.
- **`databus-interop`'s Python and C# clients never updated their `.start()`/`.Start()` call after it gained a required `multicastGroup` parameter** — both now pass the multicast group address. Python verified end-to-end; C# correct by inspection but unbuilt (no matching .NET SDK here).
- A handful of smaller issues found while bringing up the new samples: missing `#include <optional>` in `ZephyrThread.h`; `External.cmake`'s hard sibling-directory check for Zephyr (incompatible with its `west`-based workflow) relaxed to a warning; a copy-paste test bug binding `delegateAsyncWait2` to the wrong worker thread in `DelegateThreadsTests.cpp`.
- **`CmsisRtos2Clock::now()` was not ISR-safe** — it called `osKernelLock()`/`osKernelRestoreLock()` to guard its rollover-tracking static state, but a CMSIS-RTOS2 `osTimer` callback runs in genuine ISR context under Zephyr's CMSIS-RTOS2 compatibility layer (a `k_timer` `expiry_fn`), and Zephyr's `osKernelRestoreLock()` unconditionally writes its argument into the *interrupted* thread's scheduler-lock-nesting count before checking for ISR context — corrupting live scheduler state on every tick. Symptom: a one-shot `dmq::util::Timer` fired its callback forever instead of once. Fixed by using `dmq::CriticalSection` instead, the same ISR-safe primitive `Timer::GetLock()` already uses. Found and verified fixed via `example/sample-projects/cmsis-rtos2-linux`.
- **`dmq::util::Timer` was unreachable under `DMQ_THREAD_NONE` (bare metal)** — `DelegateMQ.h` excluded `extras/util/Timer.h`'s include, and `Port.cmake`'s bare-metal source glob explicitly excluded `Timer.cpp`, both under the stale assumption that `Timer` needs real OS mutexes. It doesn't: `Timer::GetLock()` uses `dmq::CriticalSection`, and `BareMetalCriticalSection.h`/`BareMetalClock.h` exist specifically to make `Timer` usable with no RTOS at all. Both now include `Timer` for `DMQ_THREAD_NONE`; `AsyncInvoke.h`/`TimerDelegate.h`/`ThreadMonitor.cpp` (which do need a real `dmq::IThread`) stay excluded. Found while adding a `Timer` test to `example/sample-projects/bare-metal-arm`.
- **`bare-metal-arm/CMakeLists.txt` never linked any `extras/`/`port/` `.cpp` file** — it globbed `DMQ_PORT_SOURCES` into an unused `SOURCES` variable and never passed it (or `DMQ_EXTRAS_SOURCES`) to `add_executable()`, silently relying on the fact that nothing this sample previously used actually needed a compiled `.cpp` outside `DMQ_LIB_SOURCES`. Now adds `${DMQ_EXTRAS_SOURCES}` specifically (not the full `${DMQ_PORT_SOURCES}`, which also includes `port/fault/Fault.cpp` — that would collide at link time with this sample's own `FaultHandler()`/`WatchdogHandler()` definitions).

### Changed
- **Reduced per-signature flash footprint of the delegate containers** — `DelegateAsync`/`DelegateAsyncWait`, `MulticastDelegate`/`MulticastDelegateSafe`, `Signal`, and `UnicastDelegate`/`UnicastDelegateSafe` each generated a separate `shared_ptr` control block (or, for `Signal`, a separate nested `State`/`Snapshot` type) per bound function signature — even where the underlying data was already fully erased to the non-templated `DelegateBase`. Hoisted the signature-independent logic into non-templated `detail::` free functions/structs in each file. No public API changes. Measured per-signature `.text` reduction (GCC, `-Os`): ~24% (`DelegateAsync`/`Wait`), ~50%/~41% (`MulticastDelegate`/`Safe`), ~70% (`Signal`), ~40%/~33% (`UnicastDelegate`/`Safe`). See `CLAUDE.md`'s "Template Instantiation Cost" section for the established pattern this follows.
- **Reduced per-topic-type duplication in `DataBus`/`Participant`** — `DataBus` (a singleton, not a class template) had the same underlying issue via template *member functions*: a "look up/establish topic type" block copy-pasted in four methods, and `InternalPublish<T>`'s signature-independent bookkeeping (LVC lookups, monitor/stringifier/signal/serializer map lookups, the participant-snapshot loop) regenerated per topic type. Hoisted into ordinary (non-template) `DataBus` member functions. Separately, `Participant::GetOrCreateChannelLocked<T>`'s `RemoteChannel<T>` allocation and `DataBus::GetOrCreateSignal<T>`'s `Signal<Sig>` allocation both had the same `shared_ptr`-control-block issue as above, fixed via the same "erase before constructing the shared_ptr" principle even though neither type has a non-templated base to erase to (erases to `void*` via a template-instantiated deleter *function*, not a lambda, since only a function's address decays to a uniform non-templated type). No public API changes. Measured impact of the `DataBus`/`RemoteChannel` fixes is much smaller than the delegate-container work above (~0.9% and ~1.4% respectively); the `GetOrCreateSignal` fix applies to essentially every `DataBus` application (most publish more than one topic type) but wasn't separately re-measured. Investigation showed the dominant per-topic-type cost actually lives in `InternalPublish<T>` unconditionally instantiating the full `Participant`/`RemoteChannel`/async-send stack for every `Publish<T>` call regardless of whether an application uses any remote participants at all, which is a separate, larger architectural question not addressed here.
- **`dmq::FullPolicy` consolidated** — was duplicated verbatim across seven port headers; now defined once in `DelegateOpt.h`, with each port aliasing it (existing `FullPolicy::DROP`-style call sites unaffected).
- **`dmq::os::ThreadMsg` consolidated** into a single `port/os/common/ThreadMsg.h`, replacing near-identical copies in six port directories.
- **Thread port files renamed** to match each platform's existing naming convention — `port/os/<platform>/Thread.{h,cpp}` → `<Platform>Thread.{h,cpp}` — with a `using Thread = <Platform>Thread;` alias so existing `dmq::os::Thread` references keep compiling unchanged.
- `test/stress_test*.cpp` moved to `test/stress-tests/`, now its own static library matching the `unit-tests`/`sample-code` convention.

### Known Issues
- **`threadx-linux`: two concurrently active `dmq::os::Thread` instances deadlock ThreadX's own Linux/GNU simulation kernel.** Root-caused to ThreadX's own internal mutex, not DelegateMQ code; affected tests are disabled in that sample pending upstream investigation (see the file's header comment for details).

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
