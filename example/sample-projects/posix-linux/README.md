# posix-linux — DelegateMQ on Raw POSIX Threads

This sample verifies DelegateMQ's `DMQ_THREAD_POSIX` port — `dmq::os::PosixThread` — which
implements the `dmq::IThread` interface directly on `pthread_create`/`pthread_mutex_t`/
`pthread_cond_t`, rather than going through `std::thread`/`std::mutex`/`std::condition_variable`
the way the default `DMQ_THREAD_STDLIB` port does. It's the POSIX counterpart to `DMQ_THREAD_WIN32`
(native OS API instead of the C++ standard library) — see `src/delegate-mq/port/os/posix/PosixThread.h`.

`dmq::Mutex`, `dmq::ConditionVariable`, `dmq::Clock`, and `dmq::ThisThread` are **not**
reimplemented for this port — they still resolve to `std::mutex`/`std::condition_variable`/
`std::chrono::steady_clock`/`std::this_thread`, since those are already thin, efficient wrappers
over these same POSIX primitives under glibc. `PosixThread.h`/`.cpp` are the only files this port
adds to the library.

No external repo or toolchain dependency — pthreads are part of the host libc, so unlike
`threadx-linux`/`freertos-linux`/`zephyr-linux`, this is a plain desktop CMake project, wired
into `01_fetch_repos.py`/`03_generate_samples.py`/`04_build_samples.py`/`05_run_samples.py`
like any other `example/sample-projects/` entry — no special-casing needed.

Identical test suite to `threadx-linux`/`freertos-linux`/`zephyr-linux`/`cmsis-rtos2-linux`/
`nuttx-sim` — same delegate, Signal, ScopedConnection, Thread, and Timer usage, plus the full
`DelegateThreadsTests()`/`TimerDelegateTests()` battery (ported unchanged from
`test/unit-tests/`) — showing the application code is unchanged across every port; only
`DMQ_THREAD` changes.

## Platform

Linux only (verified). Written against the POSIX API rather than any Linux-specific syscall,
but `pthread_condattr_setclock(CLOCK_MONOTONIC)` — used here so timed waits are immune to
wall-clock jumps (NTP steps, manual clock changes) — is a common POSIX extension present on
Linux (glibc, musl) and not guaranteed elsewhere (not implemented on Darwin/macOS). Treat
portability beyond Linux as untested rather than assumed; the CMakeLists.txt refuses to
configure on macOS for this reason.

## Build Instructions

```bash
cmake -B build .
cmake --build build
```

The output executable is at `build/delegate_posix_linux`.

## Run Instructions

```bash
./build/delegate_posix_linux
```

Unlike the RTOS simulator samples (which loop forever after their tests complete — their "OS"
doesn't stop just because one task returned), this is a plain POSIX process: it runs the test
sequence and exits normally with status 0, like any other Unix program.

**Expected Output**

```txt
=========================================
   POSIX DELEGATE SYSTEM ONLINE
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction: 100
...
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
