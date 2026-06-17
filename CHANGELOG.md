# Changelog

All notable changes to DelegateMQ are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags. Changes are from the perspective of library users and integrators.

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
