# Safety & Standards Notes

DelegateMQ is **not** designed, verified, or certified to any safety-critical coding standard (MISRA C++, AUTOSAR C++14, ISO 26262, IEC 61508, DO-178C, or similar). No formal safety analysis, static-analysis tool run, or certification process has been performed against this codebase.

This document is an informational self-assessment — a snapshot of how the library's design choices line up against common MISRA-style C++ concerns, written to help an integrator evaluating DelegateMQ for a safety-conscious or safety-adjacent project understand what they'd be taking on. It is not a compliance claim, not a gap analysis performed by a certification body, and not a promise that any item listed as "handled well" below has been verified by a qualified assessor.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Scope](#scope)
- [What's Already Handled Well](#whats-already-handled-well)
- [Areas of Concern](#areas-of-concern)
  - [RTTI Is Optional (`DMQ_NO_RTTI`)](#rtti-is-optional-dmq_no_rtti)
  - [C-Style Casts](#c-style-casts)
  - [Dynamic Memory Details](#dynamic-memory-details)
  - [Global State](#global-state)
- [Findings Table](#findings-table)
- [Notes for Safety-Conscious Integrators](#notes-for-safety-conscious-integrators)

---

## Scope

This assessment covers `src/delegate-mq/**` — the library itself, not `example/` or `test/`. It was produced by reviewing the codebase against categories drawn from MISRA C++ (dynamic memory, RTTI, casts, recursion, banned standard-library functions, global state, undefined-behavior-prone patterns, `extern "C"`/exception boundaries, format strings). Every finding below cites the actual file(s) it came from rather than a general impression.

---

## What's Already Handled Well

- **Allocator gating is consistent.** The `DMQ_ALLOCATOR`/`XALLOCATOR` mechanism is applied correctly through the delegate class hierarchy — base classes declare `XALLOCATOR` and derived `Clone()` paths inherit it, rather than each derived type needing its own opt-in (`delegate/Delegate.h`, `delegate/Signal.h`, `extras/util/TimerDelegate.h`).
- **No banned/deprecated CRT functions.** `sprintf`, `strcpy`, `strcat`, `atoi`, `gets`, and `rand` do not appear anywhere in the library.
- **No unions**, and no `printf`-family call with a non-literal format string.
- **Multiple inheritance is used only in the MISRA-permitted shape** — one concrete base plus pure-interface bases (`DelegateAsync.h`, `DelegateAsyncWait.h`, `DelegateRemote.h` via `IThreadInvoker`/`IRemoteInvoker`; `port/os/qt/QtThread.h` via `QObject` + `IThread`) — never diamond or multiple concrete-state inheritance.
- **No unbudgeted self-recursion** found anywhere in `src/delegate-mq/**`.
- **`reinterpret_cast` is used sparingly and only where a genuine reinterpretation is intended** (11 files total), rather than as a catch-all substitute for a C-style cast.
- **The `extern "C"`/exception-safety interaction is already an explicit, documented concern**, not an oversight — see `CLAUDE.md`'s note on `BAD_ALLOC()` and MSVC `/EHc` in `xallocator.cpp`. The other `extern "C"` function sites (`extras/util/Fault.h`, `port/fault/Fault.cpp`) are thin `[[noreturn]]`-style fault-handler wrappers, not allocation paths, so they don't carry the same risk. `port/os/bare-metal/BareMetalClock.h` also declares the `extern "C"` tick variable `g_ticks`, a variable rather than a function, so exceptions don't apply.
- **Test coverage is extensive.** 20 unit-test files under `test/unit-tests/` (13 `.cpp`, 7 headers) plus 3 stress-test files, run via a custom assertion-based harness (not just a smoke test) — covering the core delegate types (Unicast/Multicast/Async/AsyncWait/Remote), `Signal`, the fixed-block allocator, `Dispatcher`/`RemoteChannel`, `RetryMonitor`/`TransportMonitor`/`ReliableTransport`, and DataBus end-to-end (local, remote, QoS, deadline, spy, error, capacity, `NetworkNode`). A real `NetworkNode::Stop()` use-after-free was found specifically *because* new tests were added and exercised it — evidence the test suite finds real bugs, not just passes trivially. See [`DATABUS.md`](DATABUS.md) and this repository's `test/` tree for details.

---

## Areas of Concern

### RTTI Is Optional (`DMQ_NO_RTTI`)

Many embedded and safety-critical toolchains build with `-fno-rtti`, for code size and determinism. DelegateMQ builds that way too: when the compiler has RTTI off (`-fno-rtti` on GCC and Clang, `/GR-` on MSVC), `delegate/DelegateSelect.h` defines `DMQ_NO_RTTI` and the library uses no `dynamic_cast` and no `typeid` (`delegate/DelegateCast.h`):

| RTTI use | Without RTTI |
|---|---|
| `dynamic_cast` in every delegate's `Equal()` | `DelegateCast<T>()`: each delegate class's `CastTo()` matches its own type ID or asks its base class, the same answers `dynamic_cast` gives along the delegate hierarchy |
| `std::dynamic_pointer_cast` of the message in an async `Invoke()` | `MsgCast<T>()`: each message class reports its type ID |
| `std::function::target_type()` in `DelegateFunction::Equal()` | the callable's type ID, recorded by `MakeDelegate()` from a lambda or other callable (see the limit below) |
| `std::type_index` for DataBus and Participant topic type checks | `dmq::TypeKey`, a type ID |
| `dynamic_cast` to detect an `xostringstream` in the serializers | not needed: the remote delegate clears its stream before each write |
| `typeid` in MessageSerialize's parse handler | the parse handler gets a `dmq::TypeId` |

A type ID is the address of a static variable that exists once per type (`TypeIdOf<T>()`, an inline variable since C++17). With RTTI on, the helpers are plain `dynamic_cast`, `std::dynamic_pointer_cast` and `std::type_index`, so nothing changes.

**Limits without RTTI:**
1. **Delegates made from a `std::function` object.** Its callable's type is erased, so such a delegate equals only its own copies. Removing it by making a new delegate from the same `std::function` (`signal -= MakeDelegate(stdFunction)`) doesn't match: keep the delegate you added and remove that. Delegates made from a lambda or other callable object directly (`MakeDelegate([](int) {...})`, or an `auto` variable) compare as with RTTI.
2. **Build the whole program one way.** `DMQ_NO_RTTI` changes class layouts (`DelegateBase` and `DelegateMsg` gain a virtual function), so every file that includes DelegateMQ must agree, as `-fno-rtti` normally requires anyway.
3. **Shared libraries.** A type ID is an address, so two binaries (an executable and a DLL) can disagree about one type. Within one binary, which is the embedded case, it's exact.

**Measured** (`-Os`, `--gc-sections`): bare metal on a Cortex-M4, 29,780 → 28,108 bytes of code (−5.6%); FreeRTOS on a Cortex-M3 with async delegates, 211,748 → 209,364 bytes (−1.1%). Tested: the whole unit-test app with RTTI off (CI: GCC and MSVC), and `bare-metal/qemu`'s Cortex-M0 target, which builds with `-fno-rtti`.

### C-Style Casts

MISRA C++ treats C-style casts (`(T)x`) as a blanket violation regardless of whether any individual instance is actually unsafe, because the compiler can't distinguish an intended `static_cast` from an intended `reinterpret_cast` at the call site. This codebase uses them in two places worth flagging:

- **The fixed-block allocator core** — pointer arithmetic over allocation header/footer bytes (`extras/allocator/Allocator.cpp`, `extras/allocator/xallocator.cpp`). This is exactly the kind of low-level pointer code a MISRA review scrutinizes most closely.
- **Embedded ports that are explicitly in-scope for the library's own portability rules** — not the desktop-only transports `CLAUDE.md` exempts. For example `port/os/zephyr/ZephyrDelegateQueue.h` (`(char*)k_aligned_alloc(...)`), `port/os/zephyr/ZephyrThread.cpp`, and `port/transport/netx-udp/NetXUdpTransport.h`.

**Why it matters:** the second point is a gap against this project's *own* stated convention (`CLAUDE.md`: embedded ports "must follow all allocation and abstraction rules"), not just a general MISRA nitpick.

### Dynamic Memory Details

- Two heap allocations are deliberately never freed, to dodge static-initialization/destruction-order undefined behavior for process-lifetime singletons (`extras/util/Timer.h`, `extras/allocator/xallocator.cpp`) — both are commented as intentional, not leaks.
- `xallocator`'s default configuration backs its fixed-block pools with a one-time `new char[]` (`HEAP_POOL` mode), even though the underlying `Allocator` class already supports a fully static `STATIC_POOL` mode fed by caller-provided memory (`extras/allocator/Allocator.cpp`). A genuinely zero-heap build is possible with this library, but it isn't the default path.
- Placement-new followed by a manual destructor call appears in the allocator internals and in `DataBus`'s last-value-cache in-place reconstruction (`extras/databus/DataBus.h`) — the latter's exception-safety was reviewed earlier in this project's own development history and has explicit handling for a throwing move/copy constructor.

### Global State

`DataBus` is a Meyers-singleton (`GetInstance()`) holding internal `xmap`s of topic/participant state. Every internal method acquires `m_mutex` (a `dmq::RecursiveMutex`) before touching that state — no unguarded access was found. Global/static state is generally discouraged in safety-critical guidance, but a process-wide pub/sub registry arguably needs it structurally; the finding here is that it exists and is consistently guarded, not that it's unguarded.

---

## Findings Table

| Category | Finding | Severity | Where |
|---|---|---|---|
| RTTI | Optional: builds with `-fno-rtti`/`/GR-` (`DMQ_NO_RTTI`) | Positive | `DelegateCast.h` |
| Casts | C-style casts in allocator core pointer arithmetic | Moderate | `Allocator.cpp`, `xallocator.cpp` |
| Casts | C-style casts in in-scope embedded ports | Moderate | `ZephyrDelegateQueue.h`, `ZephyrThread.cpp`, `NetXUdpTransport.h` |
| Casts | `reinterpret_cast` used sparingly, only where warranted | Positive | 11 files |
| Dynamic memory | Allocator gating verified correct via inheritance | Positive | `Delegate.h`, `Signal.h`, `TimerDelegate.h` |
| Dynamic memory | Two intentional "immortal singleton" allocations | Minor | `Timer.h`, `xallocator.cpp` |
| Dynamic memory | Default pool mode uses heap `new[]` despite a static-pool alternative existing | Moderate | `Allocator.cpp`, `xallocator.cpp` |
| Global state | Singleton `DataBus`, but consistently mutex-guarded | Minor | `DataBus.h` |
| Banned CRT | None found (`sprintf`/`strcpy`/`strcat`/`atoi`/`gets`/`rand`) | Positive | library-wide |
| UB-prone patterns | Placement-new + manual destructor, confined and reviewed | Minor | allocator internals, `DataBus.h` |
| Format strings | No non-literal format strings | Positive | library-wide |
| Multiple inheritance | Only "concrete base + interfaces" shape used | Positive | library-wide |
| Recursion | None found | Positive | library-wide |

---

## Notes for Safety-Conscious Integrators

- If your target requires `-fno-rtti`, DelegateMQ builds that way (`DMQ_NO_RTTI`, automatic). See [RTTI Is Optional](#rtti-is-optional-dmq_no_rtti) for the one behavior difference.
- If your certification process requires every cast to be typed as to intent (no C-style casts), the allocator core and the Zephyr/NetX embedded ports are where that work would concentrate.
- If your build must be provably zero-heap, use `Allocator`'s `STATIC_POOL` mode with caller-provided memory rather than the default `HEAP_POOL` configuration. For `xallocator`, define `STATIC_POOLS` in `extras/allocator/xallocator.cpp` and size its pools there.
- None of the above are defects in the ordinary correctness sense — see [Test coverage is extensive](#whats-already-handled-well) above, plus the sample-project builds referenced throughout `CLAUDE.md`: this code is exercised and it works. These are gaps specifically against *safety-standard style conventions*, which are a stricter and different bar than "does it work."
