# DelegateMQ NuttX Sim Example

This sample verifies DelegateMQ's NuttX port (`DMQ_THREAD_NUTTX`, `src/delegate-mq/port/os/nuttx/`) against NuttX's own `sim` board — a build of NuttX that runs as an ordinary **host process**, no cross-compiler, QEMU, or target hardware required. It's the same low-cost verification model `zephyr-linux`/`cmsis-rtos2-linux` already use for their respective RTOSes.

Unlike every other sample here, this isn't a CMake project — NuttX has its own Kconfig + GNU Make build system, and this sample's `apps/` directory plugs into it directly (see "How this fits into NuttX's build" below).

## What this verifies

`NuttXThread`, `NuttXMutex`, `NuttXSemaphore`, `NuttXDelegateQueue`, `NuttXClock`, `NuttXThisThread`, and `NuttXCriticalSection` are all genuinely exercised by the full 10-test suite (identical to `zephyr-linux`/`cmsis-rtos2-linux`):

* Tests 1-6: `UnicastDelegate`/`MulticastDelegate`/`Signal`/`MulticastDelegateSafe` (no threading yet).
* Test 7: cross-thread async dispatch to a real `pthread_create`'d worker (`NuttXThread`).
* Test 8: a one-shot `Timer`, driven by a dedicated periodic pthread. This is the one that matters most: `Timer::GetLock()` takes `dmq::CriticalSection` (`NuttXCriticalSection`, `up_irq_save()`/`up_irq_restore()`), and this test confirms those calls are genuinely reachable and correct from application-level code in a FLAT NuttX build (`sim` is FLAT) — closing the single biggest open question `CLAUDE.md` flagged about this port before this sample existed. (A PROTECTED/KERNEL build still has no userspace access to `up_irq_save()` — not applicable to `sim`, but still a real caveat for that configuration; see `NuttXCriticalSection.h`'s own doc comment.)
* Tests 9-10: the full `DelegateThreadsTests`/`TimerDelegateTests` stress suite (`FullPolicy` DROP/TIMEOUT/FAULT/default, `PacedDispatch`, `TimerDelegate`) — ported from `zephyr-linux`'s copies essentially unchanged (NuttX's `SetThreadPriority(int)` takes the same plain-int convention Zephyr's does, just POSIX SCHED_FIFO's "higher number = higher priority" instead of Zephyr/ThreadX's "lower = higher").

## A real toolchain gap this surfaced

NuttX's default C++ library choice for `sim`-style testing, **uClibc++**, has its source archive fetched from `git.busybox.net` at build time — that URL now returns 404 (confirmed: the whole `uClibc++` path on that host appears to have been reorganized/removed upstream). This isn't a DelegateMQ-specific problem, but it blocks building *any* non-trivial C++ NuttX app that follows NuttX's own `cxxtest` reference config.

**Fix: use LLVM's libc++ instead** (`CONFIG_LIBCXX=y`) — its source comes from LLVM's own GitHub releases, which are reachable and stable. Two config knock-on effects from this switch, both handled in the setup below:
* libc++ 17's own *implementation* files need C++20 to build (they use `std::bit_cast` internally) even though your application code can still be C++17 — `CONFIG_CXX_STANDARD` needs bumping to `"gnu++20"` regardless of what standard your own app uses.
* `CONFIG_LIBSUPCXX` (not `CONFIG_LIBCXXABI`) is the low-level ABI support library that pairs correctly with `sim`'s host-GCC toolchain here (`sim` literally compiles with your host's real `g++`, which already has a working `libsupc++`).

## Prerequisites

1. A NuttX + NuttX-apps checkout:
   ```bash
   git clone --depth 1 --branch releases/12.4 https://github.com/apache/nuttx.git nuttx
   git clone --depth 1 --branch releases/12.4 https://github.com/apache/nuttx-apps.git apps
   ```
   (The `apps` checkout is only used for its role in NuttX's own directory conventions — this sample's `CONFIG_APPS_DIR` overrides it entirely with the `apps/` directory in this sample. `nuttx-apps` isn't otherwise touched or needed for anything this sample builds.)
2. Host build tools: `bison flex gettext texinfo libncurses5-dev libncursesw5-dev xxd gperf automake libtool pkg-config build-essential genromfs` (Debian/Ubuntu: `sudo apt install` all of these).
3. Kconfig tooling for NuttX's configure step: `kconfig-tweak`/`kconfig-mconf`/`kconfig-conf`, from the `kconfig-frontends` package (`sudo apt install kconfig-frontends`). `pip install kconfiglib` is *not* a substitute here — NuttX's own `tools/configure.sh`/`Makefile` call `kconfig-tweak` directly, a different tool than anything the `kconfiglib` PyPI package provides.

## Build Instructions

```bash
# Symlink this sample's apps/ directory in alongside your nuttx/ checkout --
# NuttX's -a flag wants a path relative to nuttx/, not absolute (see below).
ln -s /path/to/DelegateMQ/example/sample-projects/nuttx-sim/apps ../dmq_apps

cd nuttx
./tools/configure.sh -a ../dmq_apps sim:cxxtest

# Swap cxxtest's own entrypoint/app selection for this sample's, and switch
# off uClibc++ (see "A real toolchain gap" above):
kconfig-tweak --file .config --set-str CONFIG_INIT_ENTRYPOINT dmq_test_main
kconfig-tweak --file .config --disable CONFIG_UCLIBCXX --enable CONFIG_LIBCXX
kconfig-tweak --file .config --set-str CONFIG_CXX_STANDARD gnu++20
make olddefconfig

export DELEGATEMQ_ROOT=/path/to/DelegateMQ
make -j1   # -j1: see "Why -j1" below
```

### Why `-a ../dmq_apps` and not an absolute path

NuttX's `tools/configure.sh -a <app-dir>` treats `<app-dir>` as relative to the `nuttx/` directory itself (it literally concatenates `$(TOPDIR)/<app-dir>` internally) — an absolute path breaks that concatenation outright. Symlinking this sample's `apps/` directory in as a sibling of `nuttx/` (matching `Documentation/guides/customapps.rst`'s own "method 3" pattern) and passing the relative symlink name sidesteps this.

### Why this Makefile copies 3 DelegateMQ library files locally instead of referencing them in place

`apps/Makefile` needs `NuttXThread.cpp`/`Timer.cpp`/`Fault.cpp` from `$(DELEGATEMQ_ROOT)/src/delegate-mq/`. Two more direct approaches were tried and both failed for build-system reasons, not DelegateMQ reasons:

* **Absolute paths in `CXXSRCS`**: NuttX's `tools/mkdeps` (used to generate `Make.dep`) always concatenates a `--dep-path` entry with the filename argument — it never recognizes "this argument is already a complete path" — so an absolute path here makes it search for e.g. `"./ /home/.../NuttXThread.cpp"` and fail with `"not found at any location"`.
* **Bare filenames + `VPATH`**: this looked like the fix, but GNU Make's own pattern-rule-plus-archive interaction then decided the "real" location of `NuttXThread.o` was the VPATH-found source directory (`$(DELEGATEMQ_ROOT)/src/delegate-mq/port/os/nuttx/`), not this sample's own directory — silently writing build artifacts into DelegateMQ's checked-in source tree, and then failing on the *next* build once that stray `.o` looked newer than the `.cpp` and made `ar` unable to find it locally.

Copying the 3 files here at build time (see the `Makefile`'s `NuttXThread.cpp: .../NuttXThread.cpp` -style rules) sidesteps both: every path from that point on is a bare filename in this one directory, exactly like `main.cpp`. They're `.gitignore`'d (`apps/.gitignore`) since they're just build-time copies, not this sample's own sources.

### Why `-j1`

NuttX's `pass2dep`/`depend` step for `libs/libxx` (which builds LLVM's libc++) intermittently failed under `-j8`/`-j2` in this environment (a real memory-pressure issue on an 8GB host compiling libc++'s own template-heavy sources in parallel, the same class of problem `CLAUDE.md`'s `04_build_samples.py` notes for Cellutron) -- serial (`-j1`) built reliably every time. If you have more RAM available, higher parallelism may work fine.

## Run Instructions

```bash
timeout 15 ./nuttx
```

NuttX's `sim` behaves like a persistent OS, not a one-shot test binary — the init task (this sample's `dmq_test_main`) finishing doesn't power off the simulated system, the same way a real embedded target wouldn't reboot itself just because one application task returned. Run it under `timeout` (or Ctrl+C) and look for the `ALL TESTS PASSED` banner; the process not exiting on its own afterward is expected, not a hang.

**Expected output** (abbreviated — full output includes all 10 tests):

```txt
=========================================
   NUTTX DELEGATE SYSTEM ONLINE
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction: 100 (Thread: 0x3)
...
[Test 7] Async Delegate (Cross-Thread Dispatch):
  -> Dispatching to Worker Thread (Non-Blocking)...
  [Callback] FreeFunction: 800 (Thread: 0x5)

[Test 8] Timer Delegate (One-Shot):
  -> Starting Timer (200ms delay)...
  [Callback] Timer Expired! (Thread: 0x4)

[Test 9] Cross-Thread Dispatch & FullPolicy Tests:
FreeTests() complete!
...
[Test 10] PacedDispatch & TimerDelegate Tests:
...
TimerDelegateTests() complete!

=========================================
           ALL TESTS PASSED
=========================================
```

## How this fits into NuttX's build

`apps/` here replaces NuttX's own `apps/` directory entirely for this one board build (`Documentation/guides/customapps.rst`'s "method 1"), via `CONFIG_APPS_DIR`. It's hand-rolled (no `$(APPDIR)/Application.mk`/`Directory.mk`, since those live inside the real `nuttx-apps` checkout this replaces) and builds a single `libapps.a` containing the test suite; NuttX's own init calls `dmq_test_main()` directly (`CONFIG_INIT_ENTRYPOINT`) — no NSH shell or command registration needed, matching the "boot straight into the test suite" pattern `zephyr-linux`/`bare-metal-arm`/etc. all use.

## Project Structure

* `apps/main.cpp`: entry point (`dmq_test_main`), Tests 1-8, and the dedicated periodic pthread that drives `Timer::ProcessTimers()` (NuttX's own doc comment for this port names "a hardware timer ISR or the highest-priority task in the system" as the two valid drivers for this — a thread is the simpler of the two for a first verification pass).
* `apps/DelegateThreadsTests.cpp` / `TimerDelegateTests.cpp`: Tests 9-10, ported from `zephyr-linux`'s copies (see their file comments for the small adjustments: `SetThreadPriority` value and a `std::random_device` → clock-seeded generator swap, `/dev/urandom` isn't available in this minimal `sim` config).
* `apps/Makefile` / `apps/Kconfig`: the hand-rolled NuttX apps replacement described above.
