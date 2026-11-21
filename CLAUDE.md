# DelegateMQ — Contributor Rules for Claude Code

Rules for modifying the `delegate-mq` library itself. Apply these whenever adding or changing code under `src/delegate-mq/`.

**Scope: these rules are for the library, not for application code that uses it.** Code in `example/`, `test/`, `interop/` samples, or a user's own project is application code. It may use `std::vector`, `std::string`, `std::mutex`, lambdas, exceptions and the heap as its own platform allows, and follows [docs/APP-GUIDE.md](docs/APP-GUIDE.md) instead. If this repository is a submodule or dependency of another project, these rules apply only to edits inside `src/delegate-mq/`. `tools/` has its own exemptions (see "Port Exception" below).

## Memory Allocation

**No heap allocation in library internals.** The library targets embedded/RTOS platforms where heap use is either forbidden or must be explicit.

- Use `std::array<T, N>` for fixed-size collections. Exceeding capacity calls `FaultHandler`, not a realloc.
- Use `xmap` / `xmultimap` instead of `std::unordered_map` or `std::map`. Under `DMQ_ALLOCATOR` these use the fixed-block allocator; otherwise they fall back to `std::map`. O(log n) on small N is fine.
- Use `xlist` instead of `std::list`.
- Use `xstring` instead of `std::string` for **standalone string data members** (e.g., `xstring m_name`). Do not use `xstring` as a map key when lookups come through `const std::string&` API parameters — `xstring` and `std::string` are different types under `DMQ_ALLOCATOR` (different allocators), so `map.find(std::string_param)` would not compile. Map keys accessed via `const std::string&` must stay `std::string`.
- Use `xmake_shared` instead of `std::make_shared` where allocator control matters.
- Never introduce `std::vector`, `std::deque`, `std::unordered_map`, `std::unordered_set`, or `new`/`delete` in library internals without explicit justification.

## Conditional Includes — Use `DelegateOpt.h`

Do not `#include` allocator or platform headers directly in library files. `DelegateOpt.h` gates everything behind build options:

- `DMQ_ALLOCATOR` — fixed-block allocator (`xmap`, `xlist`, `xstring`, `dmq::stl_allocator`, `xnew`, `xmake_shared`)
- `DMQ_THREAD_*` — OS/mutex/clock selection
- `DMQ_ASSERTS` — assert vs. exception error handling
- `DMQ_STRICT` — enable strict compiler warnings and errors
- `DMQ_FORCE_OPTIMIZE_DEBUG` — force `-Os` on templates in unoptimized debug builds

To add a new allocator-gated type (e.g., `xunordered_map`):
1. Add the header under `extras/allocator/`.
2. Include it in the `#ifdef DMQ_ALLOCATOR` block in `DelegateOpt.h`.
3. Add a `std::*` fallback alias in the `#else` block of `DelegateOpt.h`.
4. Reference it in library code with no direct include — `DelegateOpt.h` is already transitively included everywhere via `DelegateRemote.h` → `DelegateOpt.h`.

**Types `DelegateOpt.h` provides reach library code only through it.** That covers the allocator containers, the port types (`dmq::Mutex`, `dmq::Semaphore`, `dmq::Clock`, ...) and the byte buffers (`dmq::ByteWriter`, `dmq::ByteReader`). A library header that needs one includes `DelegateOpt.h` (most get it already, transitively), never the type's own header: including it directly is a recurring mistake, and it breaks when that header depends on something `DelegateOpt.h` defines first (`ByteWriter` stores an `xstring`). When adding such a type: give it its own header, include that header from `DelegateOpt.h` after whatever it depends on, and don't have it include `DelegateOpt.h` back (a note in the header says to include `DelegateOpt.h` instead).

`DelegateOpt.h` selects; it doesn't implement. It holds conditional includes, `using` aliases, `inline constexpr` constants and adapters of a few lines (e.g. `PortableLockGuard`). Anything larger goes in its own header under `port/` (OS-specific: `port/os/<os>/`; shared by several ports: `port/os/common/`) or `extras/`, and `DelegateOpt.h` includes it and adds the alias, as it does for `dmq::os::FreeRTOSMutex` → `dmq::Mutex` and `dmq::os::PortableScopedLock` → `dmq::ScopedLock`.

## Library `.cpp` Files — Self-Guarding

Builds without CMake (including PlatformIO, which compiles every `.cpp` under `src/delegate-mq`) compile either `src/DelegateMQ.cpp` or every `.cpp` under `src/delegate-mq`, so each library `.cpp` must compile to nothing unless the options select it:

1. First line of code: `#include "delegate/DelegateSelect.h"` (macros only, so an unselected file adds no code or static objects), then `#if <condition>` around the whole file, closed with `#endif // <condition>`. Use the same condition `Port.cmake` uses to pick the file (e.g. `defined(DMQ_THREAD_FREERTOS)`, `defined(DMQ_ALLOCATOR)`, `defined(DMQ_HAS_THREADING)`). No `#error` for "this file wasn't selected". Files compiled on every port (currently only `extras/util/Timer.cpp`) need no guard.
2. Add the file to `src/DelegateMQ.cpp`.
3. Keep `Port.cmake`'s source selection in step; CMake still lists files explicitly.

New option macros that decide whether code is compiled belong in `delegate/DelegateSelect.h`, not lower in `DelegateOpt.h`.

## Configuration Constants — No Buried Literals

Any compile-time capacity, count, or timeout that an application might reasonably want to tune (array sizes, retry budgets, default timeouts, work-per-tick limits) must be a named `DMQ_*` macro in `DelegateMQConfig_Default.h` (with a matching entry in `DelegateMQConfig_Template.h`), exposed as a `dmq::` constant in `DelegateOpt.h` — never a bare numeric literal hardcoded at the point of use. Follow the existing `DMQ_MAX_PARTICIPANTS` → `dmq::MAX_PARTICIPANTS` pattern:

1. `DelegateMQConfig_Default.h`: `#ifndef DMQ_FOO` / `#define DMQ_FOO <value>` / `#endif`, with a one-line comment on what it controls.
2. `DelegateMQConfig_Template.h`: the same `#define DMQ_FOO <value>` with a doc comment, so users seeding a custom config see it.
3. `DelegateOpt.h`: `inline constexpr <type> FOO = DMQ_FOO;` in the `dmq` namespace, doc comment pointing back to `DMQ_FOO`.
4. At the use site, reference `dmq::FOO` as the default (e.g., a template parameter default or a function parameter default) — this still allows a caller or a specific instantiation to override the value locally; only the *default* moves into config.

Exception: constants tied to protocol framing, wire format, or a genuinely fixed local scratch buffer (not a capacity/count) are implementation details and do not need to be exposed.

## Fixed-Size Containers and Bounds

When a fixed-size container is full, the default is `DMQ_ASSERT_TRUE(condition)` or `DMQ_ASSERT()` — do not silently drop, resize, or throw. Two established refinements:

- **The app already controls the policy for this container** (e.g. a `dmq::os::Thread` message queue): expose it via `FullPolicy` (`FAULT`/`DROP`/`TIMEOUT`) instead of hard-coding a fault. `FAULT` stays the default.
- **The cap is hit mid-drain of a batch, not a fixed-capacity data member** (e.g. `TransportMonitor::Process()`, `Timer::ProcessTimers()`): loop across multiple bounded passes until the backlog is empty instead of faulting — see `TransportMonitor::Process()` for the reference pattern.

A `dmq::os::Thread` must never free its own stack or control block while running on them. On a self-exit (`ExitThread()` called from the thread itself) a port only sets its exit flags and returns; the cleanup runs in a later `ExitThread()` from another thread, which waits for the thread to finish first (`ZephyrThread`, `ThreadXThread`). Deleting the `Thread` object from its own callback is unsupported on ports whose kernel can't free a self-deleted thread's stack later (ThreadX, Zephyr): the destructor faults (`DMQ_ASSERT`) rather than corrupt memory. FreeRTOS's idle task does free it, so FreeRTOS doesn't need this.

A thread that drops a `DelegateMsg` it already accepted into its queue (at exit, `ExitPolicy::DISCARD`, or clearing leftovers) must cancel it first — `CancelThreadMsg()`/`CancelAll()` in `port/os/common/ThreadMsg.h`, which call `DelegateMsg::Cancel()`. Otherwise an async-wait sender blocked on that message waits out its full timeout (forever with `WAIT_INFINITE`). Rejecting a message in `DispatchDelegate()` (`FullPolicy::DROP`) needs no cancel: the sender sees the `false` return.

## Exception vs. Assert (`DMQ_ASSERTS` / `BAD_ALLOC`)

The library supports two error-handling modes, selected at build time:

- **Without `DMQ_ASSERTS`** (desktop default): allocation failures throw `std::bad_alloc`; invalid arguments and unhandled faults throw `std::invalid_argument` and `std::runtime_error`; exceptions are enabled.
- **With `DMQ_ASSERTS`** (embedded default, or when `__cpp_exceptions` is absent): `BAD_ALLOC()` calls `dmq::util::FaultHandler`. Exceptions are disabled. `DelegateOpt.h` auto-enables `DMQ_ASSERTS` when the compiler has no exception support.

Rules:
- Always use the `BAD_ALLOC()` macro for allocation failure paths — never write `throw std::bad_alloc()` directly or bare `assert`.
- Use `DMQ_ASSERT()` or `DMQ_ASSERT_TRUE(condition)` for invariant/capacity violations (wrong type, full container, protocol error) — these are hard faults, not recoverable errors.
- Use `dmq::DelegateError` + `SetErrorHandler` for soft operational errors (serialization failure, dispatch timeout) — these are recoverable and reported to the caller.
- **Report-then-fault hybrid**: when a hard fault has an existing soft-error channel nearby that the app may already be listening to (`SetErrorHandler`/`SubscribeError`/a `Signal`), report through it *immediately before* the fault — one last diagnostic for the app, even though the fault itself stays unconditional and isn't skippable by that report. Reference pattern: `DataBus.h`'s `ERR_TYPE_MISMATCH` sites and `DataBus::InternalAddParticipant`'s `ERR_CAPACITY_EXCEEDED` check. Don't add this where no such channel already exists nearby — it's "route existing signals through the fault path," not "invent a new channel just to check this box."
- `BAD_ALLOC()` and `extern "C"` don't always mix: on MSVC with `/EHc`, an `extern "C"` function is assumed never to throw, so `BAD_ALLOC()`'s non-`DMQ_ASSERTS` branch (`throw std::bad_alloc()`) is unsafe there — confirmed via compiler warning C4297 when attempted in `xallocator.cpp`, not just a lint nit. Keep `DMQ_ASSERT`/`DMQ_ASSERT_TRUE` in `extern "C"` functions; only route allocation failures through `BAD_ALLOC()` in ordinary C++ functions.
- Never add `try`/`catch` inside library internals; exception handling is the application's responsibility. (Exception: OS thread dispatch loops explicitly catch generic exceptions and `std::bad_alloc`, `std::invalid_argument`, and `std::runtime_error`).

## `XALLOCATOR` Macro

Any class whose instances are dynamically allocated and should use the fixed-block allocator must declare `XALLOCATOR` in the class body (top of the class, before any members). This overrides `operator new`/`operator delete` to use `xallocator` when `DMQ_ALLOCATOR` is defined, and compiles to nothing otherwise.

```cpp
class MyMsg {
    XALLOCATOR
public:
    // ...
};
```

Apply to: message types queued to threads (`ThreadMsg` subclasses), delegate argument heap copies, any class with high allocation frequency on the hot path.

## `ScopedConnection` — Always Store

`Signal::Connect()` and all `Subscribe*()` methods are `[[nodiscard]]`. The returned `dmq::ScopedConnection` owns the subscription — discarding it destroys the connection immediately, resulting in no callbacks and silent failure.

```cpp
// Wrong — connection destroyed immediately, no callbacks ever fire
DataBus::SubscribeError([&](auto topic, auto err) { ... });

// Correct — connection lives as long as conn is in scope
auto conn = DataBus::SubscribeError([&](auto topic, auto err) { ... });
```

Store connections in member variables (`dmq::ScopedConnection m_conn`) or in a container (`std::vector<dmq::ScopedConnection>` / `xlist<dmq::ScopedConnection>`).

## Byte Buffers, Not Streams

The remote path carries bytes in `dmq::ByteWriter` (serializers write into it, transports send `data()`/`size()`) and `dmq::ByteReader` (a transport's `Receive()` points it at its receive buffer with `assign()`, serializers read from it). Both come from `DelegateOpt.h`. No iostreams (`<iostream>`, `<sstream>`, `std::cout`/`std::cerr`) in library code that can run on an MCU: `delegate/`, `extras/`, every `port/os/` port (desktop ones too, to keep them alike), `port/fault/`, and the embedded transports. They cost about 200 KB of flash on an ESP32-S3. For text (stringifiers, log lines, fault messages) use `printf`/`fprintf(stderr, ...)`, or `snprintf` into a `char` buffer, then `dmq::xstring`. The desktop-only transports (see "Port Exception") may use `std::cerr`, but must `#include <iostream>` themselves: no library header provides it any more. Application code (`test/`, `example/`) includes what it uses too.

- A transport's `Receive()` must set the reader on every call (`is.assign(nullptr, 0)` first), and the bytes it points at must outlive the call: a member buffer, valid until the next `Receive()`, never a local.
- A serializer that rejects data calls `setfail()`; reading past the end sets `fail()` by itself.
- `ByteWriter` stores an `xstring`, so under `DMQ_ALLOCATOR` its buffer comes from the fixed-block allocator; `reset()` keeps the capacity. It never grows past `dmq::MAX_MESSAGE_SIZE` (`DMQ_MAX_MESSAGE_SIZE`): a write past it sets `fail()` and `overflowed()` without allocating, and a remote delegate reports `ERR_MESSAGE_TOO_LARGE`. Under `DMQ_ALLOCATOR` a `static_assert` in `ByteWriter.h` keeps the cap within the allocator's largest block.
- A remote delegate reports a failed buffer as `ERR_BUFFER` (formerly `ERR_STREAM_NOT_GOOD`, same value).

## Error Reporting

- Library errors surface through `dmq::DelegateError` and a `SetErrorHandler` delegate on the relevant channel/participant, not through exceptions or return codes. However, unhandled critical faults (e.g., missing error handlers) may throw `std::runtime_error` as a final fallback.
- DataBus-level errors propagate via `DataBus::SubscribeError` (global) and `Participant::SubscribeError` (per-node).
- `InternalReportError` is private; do not expose error injection as public API.
- Operational status that isn't `DelegateError`-shaped (delivery/backpressure health, not a serialize/dispatch fault) gets its own plain `dmq::Signal<Sig>` member instead of overloading the `DelegateError` channel — see `TransportMonitor::OnCapExceeded`/`OnPendingExceeded`, `RetryMonitor::OnDeliveryFailed`, `NetworkNode::OnDeliveryFailed`/`OnPeerCapExceeded`/`OnPeerPendingExceeded`, and `RemoteDispatcher`/`NetworkMgr`'s matching `OnDeliveryFailed`/`OnDeliveryFailure`. Prefer a single `Delegate`/`UnicastDelegate` member (`SetXHandler`-style) over a full `Signal` for low-level, effectively-single-owner types (e.g. a per-thread hook) where multi-subscriber support isn't needed — it's cheaper on flash-constrained builds.
- Retry/timeout status (`TransportMonitor::OnSendStatus`, `Status::TIMEOUT`) and final delivery failure after retries are exhausted (`RetryMonitor::OnDeliveryFailed`) are deliberately separate signals — a message can report `TIMEOUT` several times while still being retried before either succeeding or finally firing `OnDeliveryFailed` once. Don't collapse these into one signal; an app needs to tell "still retrying" apart from "permanently abandoned."

## Encapsulation

- Keep implementation details (`GetRemoteId`, `GetTopicName`, internal helpers) `private`. Use `friend class DataBus` on `Participant` to grant cross-class access rather than promoting members to `public`.
- Avoid `public` methods documented "internal use only" — that is a design smell. Either make them private/friend-gated or remove them.

## Hot-Path Discipline

`InternalPublish` is on the critical path for every message. Rules:

- No heap allocation inside `InternalPublish` or anything it calls inline.
- Prefer stack arrays sized to `dmq::MAX_PARTICIPANTS` for temporaries (e.g., `Participant* interested[dmq::MAX_PARTICIPANTS]`).
- Do not call `SetErrorHandler` (or any handler-registration function) on an already-configured channel on every send. Attach handlers once at channel creation.
- Avoid redundant per-participant lock acquisitions; one pass per publish is the target.

## Lambdas in Library Internals

Lambdas are fine in application code, but library internals and middleware glue (DataBus, Participant, RAII helpers) avoid capturing lambdas. `MakeDelegate()` stores a lambda in a `std::function` (`DelegateFunction`), and a capture that outgrows `std::function`'s small-buffer optimization is a hidden heap allocation.

- **Member delegates over capturing lambdas.** If a callback needs `this` or other state, move the logic into a private member function and bind it with `dmq::MakeDelegate(this, &Class::Method)`. Member-function delegates are fixed-size and never allocate.
- **Closure state goes in a helper class.** When a delegate needs real state (e.g. QoS rate limiting or filtering), put it in a dedicated class in a `detail` namespace, owned by a `shared_ptr`, and capture only that `shared_ptr` in a thin lambda if a lambda is needed at all.
- **Long-lived connections use weak-pointer delegates.** Prefer `DelegateMemberSp` (`std::shared_ptr` target, held as `std::weak_ptr`) over a lambda capturing `this`: it expires when the object is destroyed instead of calling through a dangling pointer.

## Template Instantiation Cost

`AttachErrorHandler<T>`, `GetOrCreateChannel<T>`, and similar templated helpers instantiate once per message type `T`. On flash-constrained targets this matters. Do not introduce new template helpers on the send path unless necessary.

**Established mitigation pattern**: when a templated container's data is already erased to `DelegateBase` (or another non-templated type) but the methods that operate on it are still class-template members, the compiler still regenerates that code per signature even though none of it touches the template parameters. Extract it into non-templated `detail::` free functions/structs instead — see `detail::DispatchAsync`/`AsyncDispatchState` (`DelegateAsync.h`), `detail::Multicast*`/`BroadcastGuard` (`MulticastDelegate.h`), `detail::Signal*`/`SignalState`/`SignalSnapshot` (`Signal.h`), and `detail::UnicastClone` (`UnicastDelegate.h`) for worked examples — measured per-signature code-size reductions from ~24% to ~70%. Prefer free functions over inserting a new non-templated base class into an existing hierarchy, particularly when another type already publicly derives from the one you'd be changing — external code may rely on that "is-a" relationship, and there's often no clean way to insert a new class between an existing base and its derived class without re-templating it (this is why `MulticastDelegate.h`/`Signal.h`/`UnicastDelegate.h` all use free functions rather than a `*Base` class, even though `MulticastDelegate` itself had no pre-existing base to worry about).

## Port Exception — Desktop-Only Transport and OS Files

Files under `port/transport/` and `port/os/` that are **desktop-only** (ZeroMQ, NNG, MQTT, Win32 TCP/UDP/pipe, Linux TCP/UDP, serial port, stdlib thread, Win32 thread, Qt thread) may use `std::` primitives directly — `std::mutex`, `std::recursive_mutex`, `std::lock_guard`, `std::vector`, `std::thread` — because these transports and OS ports will never compile for a bare-metal or RTOS target. Applying the `dmq::` portable abstractions there adds no value.

**Embedded transport and OS ports** (`port/transport/stm32-uart/`, `port/transport/arm-lwip-*/`, `port/transport/netx-udp/`, `port/transport/zephyr-udp/`, `port/os/freertos/`, `port/os/threadx/`, `port/os/zephyr/`, `port/os/cmsis-rtos2/`, `port/os/nuttx/`, `port/os/bare-metal/`) must follow all allocation and abstraction rules — they run on constrained targets.

**`extras/`** (DataBus, utils, dispatcher, allocator) is shared across all targets and must always follow the rules.

**`tools/`** (the diagnostic consoles, `tools/bridge/*`, the Wireshark dissector) is desktop-only diagnostic and integration code, not library code, and may use `std::` primitives. Code that must run on an MCU belongs in a port or in `extras/` and follows the full rules.

## Tools Bridges

- `tools/bridge/` is split by purpose: `common/` (shared JSON layer: `JsonTopics`, `BridgeJson.h`), `spy/` (`SpyBridge`), `node/` (`NodeBridge`), `mqtt/` (`MqttBridge`). Put a new bridge in its own `tools/bridge/<name>/` with a `<Name>Bridge.cmake` helper (`dmq_add_<name>_bridge(target)`), following `mqtt/MqttBridge.cmake`.
- **JSON bridges are `JsonTopics::ISink`s.** Applications declare their JSON view once with `JsonTopics::Expose<T>()` / `Accept<T>()`; a bridge serves whatever is registered and must not grow its own per-topic Publish/Subscribe API.
- A bridge sends on its own thread (`FullPolicy::DROP`, drops reported rate-limited), so a slow peer can't hold up the JsonTopics thread or other bridges. It must not queue sends while disconnected; replay `JsonTopics::LatchedValues()` on (re)connect instead, after marking itself connected, so the newest value always goes out last.
- Inbound commands go to `JsonTopics::Inbound()` directly on the bridge's receive thread, never through the DROP send queue, so a command can't be dropped behind outbound traffic.
- Report through `JsonTopics::OnEvent()`, prefixed with the bridge name (e.g. `"MQTT: "`).

## Interop Layer

`interop/native` (`DmqInterop` C API), `interop/python`, `interop/csharp` and the interop samples (`example/sample-interop`, `example/sample-projects/databus/interop`) ship together: a C API change must update both wrappers and all interop samples in the same change. Cross-language messages use MessagePack (`MSGPACK_DEFINE`).

## Portable Abstractions

Always use the dmq-provided portable types — never raw OS or std primitives in library code:

| Instead of | Use |
|---|---|
| `std::mutex` / `std::recursive_mutex` | `dmq::Mutex` / `dmq::RecursiveMutex` |
| `std::lock_guard` | `dmq::LockGuard<T>` |
| `std::thread` | `dmq::IThread` / `dmq::os::Thread` |
| `std::chrono::steady_clock` | `dmq::Clock` |
| `std::string` (data structures) | `xstring` |
| `std::list` | `xlist` |
| `std::map` / `std::unordered_map` | `xmap` |

## ISR-Safe Locking — `dmq::CriticalSection`

`dmq::Mutex` / `dmq::RecursiveMutex` are real OS mutexes on every RTOS port (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX) and **cannot be acquired or created from a genuine hardware ISR on any of them** — mutexes carry an "owning thread" concept for priority inheritance that has no meaning in interrupt context. Verified directly against the ThreadX source: `tx_mutex_get()` and `tx_mutex_create()` both check `TX_THREAD_GET_SYSTEM_STATE()` and return `TX_CALLER_ERROR` for an ISR caller, on *every* call, not just the first.

`dmq::CriticalSection` exists for the narrow set of call sites that must work correctly from **both** thread and ISR context — currently just `dmq::util::Timer`'s internal list lock (`Timer::GetLock()`), which guards the timer list and each timer's expiry state. `Timer::ProcessTimers()` itself is ISR-callable only on bare metal: after the list scan it copies each expired timer's subscribers (`Signal::GetSnapshot()`, OS mutexes, allocates above `DMQ_SIGNAL_SBO_COUNT` subscribers) under `Timer::GetProcessLock()`, a `RecursiveMutex`, outside the critical section. On RTOS ports it must run in a task. On bare metal those mutexes are `NullMutex`, so an ISR caller is only safe under the rules in `Timer.h` ("Bare Metal: ProcessTimers() in an ISR"): change a timer's `OnExpired` subscriptions only while it is stopped, at most `DMQ_SIGNAL_SBO_COUNT` subscribers per timer, ISR-safe synchronous callbacks. Keep those rules, and the bare-metal samples that follow them, in step with any change to this path. Never call `GetSnapshot()` or anything else that locks or allocates under `GetLock()`. Per-port, `DelegateOpt.h` resolves it to:

| Port | `dmq::CriticalSection` | ISR-safe? | Verified how |
|---|---|---|---|
| ThreadX | `dmq::os::ThreadXCriticalSection` (interrupt masking, not a `TX_MUTEX`) | **Yes** | Built and run — `example/sample-projects/os/threadx` |
| FreeRTOS | `dmq::os::FreeRTOSCriticalSection` (auto-detects context via `xPortIsInsideInterrupt()`, selects `taskENTER_CRITICAL[_FROM_ISR]`) | **Yes on real ARM Cortex-M targets** (e.g. `boards/stm32-freertos`); task-context-only on the POSIX/Win32 simulator ports (no real hardware interrupt to detect there) | Task-context path built and run — `example/sample-projects/os/freertos`. ISR-context path built and run — `os/freertos-bare-metal`'s `arm-m3` target (FreeRTOS `GCC/ARM_CM3`, QEMU `mps2-an385`): a CMSDK timer interrupt calls `Timer::Start()`/`Stop()` while a task does the same. With detection forced off, the port's `configASSERT` (no task-level critical section in an interrupt) fails the run |
| Zephyr | `dmq::os::ZephyrCriticalSection` (`irq_lock()`/`irq_unlock(key)`) | **Yes** | Built and run — `example/sample-projects/os/zephyr` (`native_sim`), from thread context only: the sample's `k_timer` expiry callback (ISR context) just gives a semaphore, and a highest-priority thread calls `Timer::ProcessTimers()`, since `ProcessTimers()` takes OS mutexes. Not exercised from ISR context (`os/threadx` and `os/cmsis-rtos2` use the same timer-signals-a-thread pattern) |
| CMSIS-RTOS2 | `dmq::os::CmsisRtos2CriticalSection` (`__disable_irq()`/`__enable_irq()` via CMSIS-Core, bypassing `osMutex`) | **Yes** | Built and run on a real Cortex-M3 — `example/sample-projects/os/cmsis-rtos2`'s Zephyr `mps2/an385` build under QEMU (real CMSIS-Core): PRIMASK set while held and restored after, nested, from a thread and from the SysTick interrupt (a CMSIS `osTimer` callback), and `Timer::Start()`/`Stop()` from that interrupt. The same test checks the port's interrupt detection (`detail::IsInsideCmsisRtos2Interrupt()`, IPSR on Cortex-M): the current-thread key is 0 in the interrupt even when it lands on a CMSIS-RTOS2 thread. The rest of the port also runs on `native_sim` (Zephyr's real CMSIS-RTOS2 layer, `CONFIG_CMSIS_RTOS_V2`, with a no-op stub for the three intrinsics). That sample found and fixed an ISR-safety bug in `CmsisRtos2Clock::now()` and a 64-bit `std::atomic` in `CmsisRtos2Thread` that didn't link on 32-bit Cortex-M (see its README) |
| NuttX | `dmq::os::NuttXCriticalSection` (`up_irq_save()`/`up_irq_restore(flags)`, NuttX's own architecture-portable interrupt-masking primitive, not a `pthread_mutex_t`) | **Yes, confirmed reachable from application code in a FLAT build** (a PROTECTED/KERNEL build still has no userspace access — not applicable to `sim`, which is FLAT) | Built and run — `example/sample-projects/os/nuttx` (NuttX's own `sim` board, a native host process, `CONFIG_LIBCXX`). `up_irq_save()`/`up_irq_restore()` genuinely work from this port's application-level `Timer::GetLock()` call site: Test 8's one-shot `Timer` fires exactly once, and Test 9/10's cross-thread `FullPolicy`/`PacedDispatch` stress tests all pass. Not exercised from a genuine hardware ISR (no real interrupt controller under `sim`) — only from ordinary thread context, same caveat as CMSIS-RTOS2/Zephyr's own `native_sim`-based verification |
| Bare metal (`DMQ_THREAD_NONE`) | `dmq::os::BareMetalCriticalSection` — architecture-branched: ARM (PRIMASK save/disable/restore, same technique `BareMetalClock.h` uses) or RISC-V (`mstatus.MIE` save/clear/restore via `csrrci`/`csrs`) | **Yes, both architectures** | Built and run — `example/sample-projects/bare-metal/qemu`, targets `arm-m4` (QEMU `mps2-an386`, Cortex-M4, ARM GCC), `arm-m0` (QEMU `microbit`, Cortex-M0/nRF51, ARM GCC), `arm-m3-atfe` (QEMU `mps2-an385`, Cortex-M3, ATfE clang) and `riscv32` (QEMU `virt`, RV32IMC). Both genuinely exercised from ISR context: a real hardware timer interrupt (SysTick on ARM, the nRF51's TIMER0 on `arm-m0`, CLINT `mtime`/`mtimecmp` on RISC-V) calls `Timer::ProcessTimers()` (which takes this lock) directly, not a simulated stand-in |
| Desktop (stdlib/Win32/Qt) | aliased to `RecursiveMutex` | N/A — no ISR concept reachable from userspace; correct as-is | Built and run as part of every desktop sample |

Rules when touching this:

- **Never use `dmq::CriticalSection` as a general-purpose lock.** It masks *all* maskable interrupts on the CPU for as long as it's held. Reach for it only to protect something genuinely tiny and bounded that may be touched from ISR context (`Timer`'s use case). Anything that can block, take a while, or is normally protected by `dmq::Mutex`/`RecursiveMutex` (DataBus internals, a `Thread`'s message queue, etc.) must never be moved to `CriticalSection` — that's a real-time correctness bug on hardware, not a style choice.
- **There is no `RecursiveCriticalSection`.** A critical section has no ownership concept, so recursion isn't meaningful the way it is for `RecursiveMutex` — nesting *different* instances in proper LIFO lock/unlock order is safe by construction (each saves/restores its own interrupt state), but calling `lock()` twice on the **same** instance without an intervening `unlock()` is not (see the warning in each `*CriticalSection.h`).
- **FreeRTOS's ISR detection covers ARM Cortex-M and ESP32.** `FreeRTOSCriticalSection.h` checks `ESP_PLATFORM` first (ESP32 Xtensa and RISC-V: SMP spinlock `portMUX_TYPE` with `portENTER_CRITICAL_SAFE()`, ISR detection via `xPortInIsrContext()`), then the `__arm__`/`__ARM_ARCH` predefines for the real `xPortIsInsideInterrupt()`, else a dummy `pdFALSE` stub (desktop GCC/Clang building the POSIX/Win32 simulators). No `DMQ_*` build option needed. Another architecture's FreeRTOS port would need its own branch.
- **Porting to a new RTOS**: use `port/os/threadx/ThreadXCriticalSection.h` as the reference implementation, and pick that port's true interrupt-masking primitive — not its OS mutex, no matter how it's configured.
- **Porting bare metal (`DMQ_THREAD_NONE`) to a new architecture**: `BareMetalCriticalSection.h`/`BareMetalClock.h` branch internally per architecture (`#if defined(__riscv)` / `#elif defined(__arm__) && ...` / ARMCC / fallback) rather than each architecture getting its own file — follow that pattern for a new one (e.g. Xtensa) rather than creating a parallel `port/os/bare-metal-<arch>/` tree. Check the architecture macro *before* any generic `__GNUC__`/`__clang__` check, since those are true for every GCC/Clang target regardless of architecture — checking compiler before architecture is what caused RISC-V to briefly try to assemble ARM `cpsid`/`cpsie` opcodes when this port was extended for RISC-V.
- **The CMSIS-RTOS2 port is verified on a real Cortex-M3**, `CmsisRtos2CriticalSection` included — `os/cmsis-rtos2`'s Zephyr `mps2/an385` build under QEMU (see the table above and that sample's README).
- **No 64-bit `std::atomic` in embedded ports.** On a 32-bit Cortex-M (no 64-bit `LDREX`/`STREX`) it compiles to a libatomic call, and arm-none-eabi toolchains ship no libatomic, so the port doesn't link. Keep time stamps as 32-bit counts with wrap-safe unsigned differences: `FreeRTOSThread` and `CmsisRtos2Thread` store `dmq::Clock` counts; `ThreadXThread`, `ZephyrThread` and `NuttXThread` store milliseconds (NuttX's clock is nanoseconds, which would wrap a 32-bit count in 4 s). `os/zephyr`'s Zephyr `mps2/an385` build under QEMU (Cortex-M3) is the link-and-run check for `ZephyrThread`; the old 64-bit members failed there with `undefined reference to __atomic_load_8`. Desktop-only ports (stdlib, Win32, POSIX, Qt) may keep 64-bit atomics.
- **The NuttX port (`port/os/nuttx/`) is now verified** — built and run via `example/sample-projects/os/nuttx` (NuttX's own `sim` board, native host process, no cross-compiler/hardware). `NuttXThread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`/`CriticalSection` all genuinely exercised: cross-thread async dispatch, `MulticastDelegateSafe`, a real one-shot `Timer` (via `NuttXCriticalSection`), and the full `FullPolicy`/`PacedDispatch` stress-test suite (Tests 1-10) all pass. See the sample's README for what this took to get building (a working `libstdc++`/RTTI toolchain for NuttX's own build, and two real NuttX/mkdeps build-system quirks worked around, not DelegateMQ bugs).

## FreeRTOS Port Portability

The FreeRTOS port builds against many FreeRTOS packagings (vendor SDKs, ESP-IDF, STM32duino FreeRTOS, the POSIX/Win32 simulators), each with its own `FreeRTOSConfig.h`:

- Include kernel headers through `port/os/freertos/FreeRTOSHeaders.h`, never `FreeRTOS.h`/`task.h` directly: ESP-IDF puts them under `freertos/`.
- Never assume an optional kernel feature is on. Guard its use with the config macro (e.g. `xTaskCreateStatic()` only `#if (configSUPPORT_STATIC_ALLOCATION == 1)`; STM32duino FreeRTOS leaves it 0), and fault with `DMQ_ASSERT` where an API needs it.

## Arduino Compatibility

Arduino cores `#define` common names (`HIGH`, `LOW`, `INPUT`, `OUTPUT`, `abs`, `min`, `max`, `round`, ...). Don't use them as identifiers in public headers: that is why the message priority is `dmq::Priority::URGENT`, not `HIGH`. Check a new public name against `Arduino.h` before adding it.

## Testing Conventions

- Tests use `DataBus::ResetForTesting()` between cases — always call it at the top of each test block.
- Do not use `exit(1)` in tests; use `return 1` so subsequent test suites still run.
- Document synchronous-dispatch assumptions explicitly when a test checks results immediately after `Publish` (a send thread would make this a race).
- Document cross-layer exception contracts (e.g., "assumes `RemoteChannel` converts serializer exceptions to `ERR_SERIALIZE`").
