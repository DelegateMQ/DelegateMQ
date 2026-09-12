# DelegateMQ — Contributor Rules for Claude Code

Rules for modifying the `delegate-mq` library itself. Apply these whenever adding or changing code under `src/delegate-mq/`.

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

- `DMQ_ALLOCATOR` — fixed-block allocator (`xmap`, `xlist`, `xstring`, `xsstream`, `dmq::stl_allocator`, `xnew`, `xmake_shared`)
- `DMQ_THREAD_*` — OS/mutex/clock selection
- `DMQ_ASSERTS` — assert vs. exception error handling
- `DMQ_STRICT` — enable strict compiler warnings and errors
- `DMQ_FORCE_OPTIMIZE_DEBUG` — force `-Os` on templates in unoptimized debug builds

To add a new allocator-gated type (e.g., `xunordered_map`):
1. Add the header under `extras/allocator/`.
2. Include it in the `#ifdef DMQ_ALLOCATOR` block in `DelegateOpt.h`.
3. Add a `std::*` fallback alias in the `#else` block of `DelegateOpt.h`.
4. Reference it in library code with no direct include — `DelegateOpt.h` is already transitively included everywhere via `DelegateRemote.h` → `DelegateOpt.h`.

## Configuration Constants — No Buried Literals

Any compile-time capacity, count, or timeout that an application might reasonably want to tune (array sizes, retry budgets, default timeouts, work-per-tick limits) must be a named `DMQ_*` macro in `DelegateMQConfig_Default.h` (with a matching entry in `DelegateMQConfig_Template.h`), exposed as a `dmq::` constant in `DelegateOpt.h` — never a bare numeric literal hardcoded at the point of use. Follow the existing `DMQ_MAX_PARTICIPANTS` → `dmq::MAX_PARTICIPANTS` pattern:

1. `DelegateMQConfig_Default.h`: `#ifndef DMQ_FOO` / `#define DMQ_FOO <value>` / `#endif`, with a one-line comment on what it controls.
2. `DelegateMQConfig_Template.h`: the same `#define DMQ_FOO <value>` with a doc comment, so users seeding a custom config see it.
3. `DelegateOpt.h`: `inline constexpr <type> FOO = DMQ_FOO;` in the `dmq` namespace, doc comment pointing back to `DMQ_FOO`.
4. At the use site, reference `dmq::FOO` as the default (e.g., a template parameter default or a function parameter default) — this still allows a caller or a specific instantiation to override the value locally; only the *default* moves into config.

Exception: constants tied to protocol framing, wire format, or a genuinely fixed local scratch buffer (not a capacity/count) are implementation details and do not need to be exposed.

## Fixed-Size Containers and Bounds

When a fixed-size container is full, call `ASSERT_TRUE(condition)` or `ASSERT()` — do not silently drop, resize, or throw. This is the established pattern for capacity violations (see `DataBus::InternalAddParticipant`).

## Exception vs. Assert (`DMQ_ASSERTS` / `BAD_ALLOC`)

The library supports two error-handling modes, selected at build time:

- **Without `DMQ_ASSERTS`** (desktop default): allocation failures throw `std::bad_alloc`; invalid arguments and unhandled faults throw `std::invalid_argument` and `std::runtime_error`; exceptions are enabled.
- **With `DMQ_ASSERTS`** (embedded default, or when `__cpp_exceptions` is absent): `BAD_ALLOC()` calls `dmq::util::FaultHandler`. Exceptions are disabled. `DelegateOpt.h` auto-enables `DMQ_ASSERTS` when the compiler has no exception support.

Rules:
- Always use the `BAD_ALLOC()` macro for allocation failure paths — never write `throw std::bad_alloc()` directly or bare `assert`.
- Use `ASSERT()` or `ASSERT_TRUE(condition)` for invariant/capacity violations (wrong type, full container, protocol error) — these are hard faults, not recoverable errors.
- Use `dmq::DelegateError` + `SetErrorHandler` for soft operational errors (serialization failure, dispatch timeout) — these are recoverable and reported to the caller.
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

## Stream Types

Use the portable stream aliases instead of std:: streams in transport and serialization code:

| Instead of | Use |
|---|---|
| `std::ostringstream` | `dmq::xostringstream` |
| `std::stringstream` | `dmq::xstringstream` |

Under `DMQ_ALLOCATOR` these use the fixed-block allocator for internal buffers. They are defined in `DelegateOpt.h` and available everywhere.

## Error Reporting

- Library errors surface through `dmq::DelegateError` and a `SetErrorHandler` delegate on the relevant channel/participant, not through exceptions or return codes. However, unhandled critical faults (e.g., missing error handlers) may throw `std::runtime_error` as a final fallback.
- DataBus-level errors propagate via `DataBus::SubscribeError` (global) and `Participant::SubscribeError` (per-node).
- `InternalReportError` is private; do not expose error injection as public API.

## Encapsulation

- Keep implementation details (`GetRemoteId`, `GetTopicName`, internal helpers) `private`. Use `friend class DataBus` on `Participant` to grant cross-class access rather than promoting members to `public`.
- Avoid `public` methods documented "internal use only" — that is a design smell. Either make them private/friend-gated or remove them.

## Hot-Path Discipline

`InternalPublish` is on the critical path for every message. Rules:

- No heap allocation inside `InternalPublish` or anything it calls inline.
- Prefer stack arrays sized to `dmq::MAX_PARTICIPANTS` for temporaries (e.g., `Participant* interested[dmq::MAX_PARTICIPANTS]`).
- Do not call `SetErrorHandler` (or any handler-registration function) on an already-configured channel on every send. Attach handlers once at channel creation.
- Avoid redundant per-participant lock acquisitions; one pass per publish is the target.

## Template Instantiation Cost

`AttachErrorHandler<T>`, `GetOrCreateChannel<T>`, and similar templated helpers instantiate once per message type `T`. On flash-constrained targets this matters. Do not introduce new template helpers on the send path unless necessary.

**Established mitigation pattern**: when a templated container's data is already erased to `DelegateBase` (or another non-templated type) but the methods that operate on it are still class-template members, the compiler still regenerates that code per signature even though none of it touches the template parameters. Extract it into non-templated `detail::` free functions/structs instead — see `detail::DispatchAsync`/`AsyncDispatchState` (`DelegateAsync.h`), `detail::Multicast*`/`BroadcastGuard` (`MulticastDelegate.h`), `detail::Signal*`/`SignalState`/`SignalSnapshot` (`Signal.h`), and `detail::UnicastClone` (`UnicastDelegate.h`) for worked examples — measured per-signature code-size reductions from ~24% to ~70%. Prefer free functions over inserting a new non-templated base class into an existing hierarchy, particularly when another type already publicly derives from the one you'd be changing — external code may rely on that "is-a" relationship, and there's often no clean way to insert a new class between an existing base and its derived class without re-templating it (this is why `MulticastDelegate.h`/`Signal.h`/`UnicastDelegate.h` all use free functions rather than a `*Base` class, even though `MulticastDelegate` itself had no pre-existing base to worry about).

## Port Exception — Desktop-Only Transport and OS Files

Files under `port/transport/` and `port/os/` that are **desktop-only** (ZeroMQ, NNG, MQTT, Win32 TCP/UDP/pipe, Linux TCP/UDP, serial port, stdlib thread, Win32 thread, Qt thread) may use `std::` primitives directly — `std::mutex`, `std::recursive_mutex`, `std::lock_guard`, `std::vector`, `std::thread` — because these transports and OS ports will never compile for a bare-metal or RTOS target. Applying the `dmq::` portable abstractions there adds no value.

**Embedded transport and OS ports** (`port/transport/stm32-uart/`, `port/transport/arm-lwip-*/`, `port/transport/netx-udp/`, `port/transport/zephyr-udp/`, `port/os/freertos/`, `port/os/threadx/`, `port/os/zephyr/`, `port/os/cmsis-rtos2/`, `port/os/bare-metal/`) must follow all allocation and abstraction rules — they run on constrained targets.

**`extras/`** (DataBus, utils, dispatcher, allocator) is shared across all targets and must always follow the rules.

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

`dmq::Mutex` / `dmq::RecursiveMutex` are real OS mutexes on every RTOS port (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2) and **cannot be acquired or created from a genuine hardware ISR on any of them** — mutexes carry an "owning thread" concept for priority inheritance that has no meaning in interrupt context. Verified directly against the ThreadX source: `tx_mutex_get()` and `tx_mutex_create()` both check `TX_THREAD_GET_SYSTEM_STATE()` and return `TX_CALLER_ERROR` for an ISR caller, on *every* call, not just the first.

`dmq::CriticalSection` exists for the narrow set of call sites that must work correctly from **both** thread and ISR context — currently just `dmq::util::Timer`'s internal list lock (`Timer::GetLock()`), since `Timer::ProcessTimers()` is documented as callable from the highest-priority context available, including a hardware ISR where that's safe. Per-port, `DelegateOpt.h` resolves it to:

| Port | `dmq::CriticalSection` | ISR-safe? | Verified how |
|---|---|---|---|
| ThreadX | `dmq::os::ThreadXCriticalSection` (interrupt masking, not a `TX_MUTEX`) | **Yes** | Built and run — `example/sample-projects/threadx-linux` |
| FreeRTOS | `dmq::os::FreeRTOSCriticalSection` (auto-detects context via `xPortIsInsideInterrupt()`, selects `taskENTER_CRITICAL[_FROM_ISR]`) | **Yes on real ARM Cortex-M targets** (e.g. `stm32-freertos`); task-context-only on the POSIX/Win32 simulator ports (no real hardware interrupt to detect there) | Task-context path built and run — `example/sample-projects/freertos-linux`. ISR-context path reasoned through, not exercised (no genuine hardware interrupt on that sample) |
| Zephyr | `dmq::os::ZephyrCriticalSection` (`irq_lock()`/`irq_unlock(key)`) | **Yes** | Built and run — `example/sample-projects/zephyr-linux` (`native_sim`). Genuinely exercised from ISR context: Zephyr's `k_timer` expiry callback runs in ISR context per its own `kernel.h` doc comment, and that callback is what calls `Timer::ProcessTimers()`/takes this lock — though on `native_sim` that ISR context is itself simulated, not a genuine hardware interrupt, the same caveat as FreeRTOS's task-context-only verification above |
| CMSIS-RTOS2 | `dmq::os::CmsisRtos2CriticalSection` (`__disable_irq()`/`__enable_irq()` via CMSIS-Core, bypassing `osMutex`) | Yes, per documented CMSIS-Core/CMSIS-RTOS2 API | **The type itself is still unverified** — its `__get_PRIMASK()`/`__disable_irq()`/`__set_PRIMASK()` calls are real ARM Cortex-M CMSIS-Core intrinsics (inline `cpsid i`/`mrs` assembly) with no Cortex-M target available to build/run it against (no CMSIS-RTOS2 SDK, hardware, or QEMU here). Everything else in this port (`CmsisRtos2Thread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`) **is** built and run — `example/sample-projects/cmsis-rtos2-linux`, against Zephyr's own real CMSIS-RTOS2 implementation (`CONFIG_CMSIS_RTOS_V2`) on `native_sim`, via a sample-local no-op stub standing in only for the three CMSIS-Core intrinsics. That verification pass found and fixed a genuine ISR-safety bug in `CmsisRtos2Clock::now()` (see that sample's README) |
| Bare metal (`DMQ_THREAD_NONE`) | `dmq::os::BareMetalCriticalSection` (same PRIMASK save/disable/restore technique as `BareMetalClock.h`) | Yes | Not built/run in this pass — mirrors an already-proven pattern in this codebase |
| Desktop (stdlib/Win32/Qt) | aliased to `RecursiveMutex` | N/A — no ISR concept reachable from userspace; correct as-is | Built and run as part of every desktop sample |

Rules when touching this:

- **Never use `dmq::CriticalSection` as a general-purpose lock.** It masks *all* maskable interrupts on the CPU for as long as it's held. Reach for it only to protect something genuinely tiny and bounded that may be touched from ISR context (`Timer`'s use case). Anything that can block, take a while, or is normally protected by `dmq::Mutex`/`RecursiveMutex` (DataBus internals, a `Thread`'s message queue, etc.) must never be moved to `CriticalSection` — that's a real-time correctness bug on hardware, not a style choice.
- **There is no `RecursiveCriticalSection`.** A critical section has no ownership concept, so recursion isn't meaningful the way it is for `RecursiveMutex` — nesting *different* instances in proper LIFO lock/unlock order is safe by construction (each saves/restores its own interrupt state), but calling `lock()` twice on the **same** instance without an intervening `unlock()` is not (see the warning in each `*CriticalSection.h`).
- **FreeRTOS's ISR detection is scoped to ARM Cortex-M.** `FreeRTOSCriticalSection.h` selects the real `xPortIsInsideInterrupt()` vs. a dummy `pdFALSE` stub via the `__arm__`/`__ARM_ARCH` compiler predefines (set by `arm-none-eabi-gcc`, never set by desktop GCC/Clang building the POSIX/Win32 simulators) — no new `DMQ_*` build option needed. A RISC-V or Xtensa FreeRTOS port would need that detection extended for its own architecture predefines.
- **Porting to a new RTOS**: use `port/os/threadx/ThreadXCriticalSection.h` as the reference implementation, and pick that port's true interrupt-masking primitive — not its OS mutex, no matter how it's configured.
- **`CmsisRtos2CriticalSection` itself is still unverified** — its CMSIS-Core intrinsics need real ARM Cortex-M (hardware, QEMU, or a CMSIS-RTOS2 SDK), none available here. Review carefully and exercise on real hardware or an appropriate simulator before relying on it in production. The rest of the CMSIS-RTOS2 port (`Thread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`) is now verified against a real CMSIS-RTOS2 implementation (Zephyr's, on `native_sim`) — see the table above and `example/sample-projects/cmsis-rtos2-linux`.

## Testing Conventions

- Tests use `DataBus::ResetForTesting()` between cases — always call it at the top of each test block.
- Do not use `exit(1)` in tests; use `return 1` so subsequent test suites still run.
- Document synchronous-dispatch assumptions explicitly when a test checks results immediately after `Publish` (a send thread would make this a race).
- Document cross-layer exception contracts (e.g., "assumes `RemoteChannel` converts serializer exceptions to `ERR_SERIALIZE`").
