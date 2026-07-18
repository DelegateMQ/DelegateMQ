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

To add a new allocator-gated type (e.g., `xunordered_map`):
1. Add the header under `extras/allocator/`.
2. Include it in the `#ifdef DMQ_ALLOCATOR` block in `DelegateOpt.h`.
3. Add a `std::*` fallback alias in the `#else` block of `DelegateOpt.h`.
4. Reference it in library code with no direct include — `DelegateOpt.h` is already transitively included everywhere via `DelegateRemote.h` → `DelegateOpt.h`.

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

## Testing Conventions

- Tests use `DataBus::ResetForTesting()` between cases — always call it at the top of each test block.
- Do not use `exit(1)` in tests; use `return 1` so subsequent test suites still run.
- Document synchronous-dispatch assumptions explicitly when a test checks results immediately after `Publish` (a send thread would make this a race).
- Document cross-layer exception contracts (e.g., "assumes `RemoteChannel` converts serializer exceptions to `ERR_SERIALIZE`").
