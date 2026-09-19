# Safety & Standards Notes

DelegateMQ is **not** designed, verified, or certified to any safety-critical coding standard (MISRA C++, AUTOSAR C++14, ISO 26262, IEC 61508, DO-178C, or similar). No formal safety analysis, static-analysis tool run, or certification process has been performed against this codebase.

This document is an informational self-assessment — a snapshot of how the library's design choices line up against common MISRA-style C++ concerns, written to help an integrator evaluating DelegateMQ for a safety-conscious or safety-adjacent project understand what they'd be taking on. It is not a compliance claim, not a gap analysis performed by a certification body, and not a promise that any item listed as "handled well" below has been verified by a qualified assessor.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Scope](#scope)
- [What's Already Handled Well](#whats-already-handled-well)
- [Areas of Concern](#areas-of-concern)
  - [RTTI Dependency](#rtti-dependency)
    - [Eliminating `dynamic_cast` (If `-fno-rtti` Is Required)](#eliminating-dynamic_cast-if--fno-rtti-is-required)
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
- **`reinterpret_cast` is used sparingly and only where a genuine reinterpretation is intended** (7 files total), rather than as a catch-all substitute for a C-style cast.
- **The `extern "C"`/exception-safety interaction is already an explicit, documented concern**, not an oversight — see `CLAUDE.md`'s note on `BAD_ALLOC()` and MSVC `/EHc` in `xallocator.cpp`. The other two `extern "C"` sites (`extras/util/Fault.h`, `port/fault/Fault.cpp`) are thin `[[noreturn]]`-style fault-handler wrappers, not allocation paths, so they don't carry the same risk.

---

## Areas of Concern

### RTTI Dependency

The delegate-equality mechanism (`Equal()`, used by `Signal::Disconnect()` and delegate comparison generally) relies on `dynamic_cast` at roughly 19 call sites across `delegate/Delegate.h`, `delegate/DelegateAsync.h`, `delegate/DelegateAsyncWait.h`, `delegate/DelegateRemote.h`, and `extras/util/TimerDelegate.h`. This is architectural, not incidental — every delegate family's equality check is built on it.

Separately, `dmq::databus::DataBus` uses `typeid`/`std::type_index` to catch a topic being used with two different C++ types across `Publish`/`Subscribe`/`RegisterSerializer` calls (`extras/databus/DataBus.h`). This is a narrower, well-motivated use (a compile-time-adjacent programmer-error check, not polymorphic dispatch), but it's a second, independent place RTTI is load-bearing.

**Why it matters:** many embedded/safety-critical toolchains build with `-fno-rtti` by default, for code-size and determinism reasons. DelegateMQ does not compile under `-fno-rtti` today.

#### Eliminating `dynamic_cast` (If `-fno-rtti` Is Required)

Not a fundamental blocker — RTTI-free polymorphic type identification is a solved problem (it's how LLVM's `isa<>`/`cast<>`/`dyn_cast<>` work, for example). Every `Equal()` override follows the identical pattern:

```cpp
// Delegate.h:273-277, and the same shape at 19 sites total across
// Delegate.h, DelegateAsync.h, DelegateAsyncWait.h, DelegateRemote.h, TimerDelegate.h
virtual bool Equal(const DelegateBase& rhs) const override {
    auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
    return derivedRhs && m_func == derivedRhs->m_func;
}
```

`dynamic_cast` here answers one narrow question — "is `rhs` provably the same concrete template instantiation as `this`?" — before it's safe to compare `m_func`. A manual type tag answers the same question without RTTI:

```cpp
// DelegateBase: one new pure virtual
virtual const void* TypeTag() const noexcept = 0;

// each concrete ClassType: two new lines
inline static const char s_tag = 0;
const void* TypeTag() const noexcept override { return &s_tag; }

// Equal() becomes:
bool Equal(const DelegateBase& rhs) const override {
    if (TypeTag() != rhs.TypeTag()) return false;
    auto& d = static_cast<const ClassType&>(rhs); // safe: tag match proves same type
    return m_func == d.m_func;
}
```

An `inline static` (C++17, already the library's minimum) gets exactly one ODR-merged definition per template instantiation, so its address is a unique-per-type identifier without touching the RTTI subsystem — and it's cheaper than `dynamic_cast` too (one pointer compare vs. a runtime type-hierarchy walk).

**Scope:** mechanical, not architectural — all 19 sites are the same three-line pattern across the same 5 files.

**Two caveats to weigh before doing this:**
1. **Discipline requirement.** The scheme depends on every tag genuinely being `inline` (or defined out-of-line exactly once). Get that wrong and, unlike `dynamic_cast`'s clean "wrong type → `nullptr`" failure, you get a silent `Equal()` false negative instead — two genuinely-equal delegates built in different translation units comparing unequal.
2. **DLL/shared-library boundaries.** If DelegateMQ is consumed as a DLL from multiple binaries each instantiating the same template, whether they land on the same tag address depends on export/import behavior. `typeid`/`dynamic_cast` have their own well-known cross-DLL RTTI quirks, so this isn't obviously worse — just a different set of things to verify.

`DataBus.h`'s `typeid`/`type_index` use would also need addressing for a fully RTTI-free build (`-fno-rtti` is normally all-or-nothing per target), but it's the easier of the two — it's comparing an app-supplied type per topic and could be replaced with a manually-assigned tag the same way.

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
| RTTI | `dynamic_cast` in every delegate family's `Equal()` | Notable | `Delegate.h`, `DelegateAsync.h`, `DelegateAsyncWait.h`, `DelegateRemote.h`, `TimerDelegate.h` |
| RTTI | `typeid`/`type_index` for DataBus topic type-checking | Minor | `DataBus.h` |
| Casts | C-style casts in allocator core pointer arithmetic | Moderate | `Allocator.cpp`, `xallocator.cpp` |
| Casts | C-style casts in in-scope embedded ports | Moderate | `ZephyrDelegateQueue.h`, `ZephyrThread.cpp`, `NetXUdpTransport.h` |
| Casts | `reinterpret_cast` used sparingly, only where warranted | Positive | 7 files |
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

- If your target requires `-fno-rtti`, the `dynamic_cast`-based delegate equality mechanism would need to be reworked first — this is the single largest structural item above. See [Eliminating `dynamic_cast`](#eliminating-dynamic_cast-if--fno-rtti-is-required) for a concrete, low-risk approach.
- If your certification process requires every cast to be typed as to intent (no C-style casts), the allocator core and the Zephyr/NetX embedded ports are where that work would concentrate.
- If your build must be provably zero-heap, use `Allocator`'s `STATIC_POOL` mode with caller-provided memory rather than the default `HEAP_POOL` configuration.
- None of the above are defects in the ordinary correctness sense — DelegateMQ's test suite and the sample-project builds referenced throughout `CLAUDE.md` exercise this code and it works. These are gaps specifically against *safety-standard style conventions*, which are a stricter and different bar than "does it work."
