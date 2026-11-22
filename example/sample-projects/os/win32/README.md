# os/win32 — DelegateMQ on Native Win32 Threads

This sample verifies DelegateMQ's `DMQ_THREAD_WIN32` port, `dmq::os::Win32Thread`, which implements the `dmq::IThread` interface directly on the Win32 API (`CreateThread`, `CRITICAL_SECTION`, `CONDITION_VARIABLE`, a manual-reset event for synchronized start) rather than through `std::thread` the way the default `DMQ_THREAD_STDLIB` port does. See `src/delegate-mq/port/os/win32/Win32Thread.h`. It's the Windows counterpart to [os/posix](../posix/README.md).

`dmq::Mutex`, `dmq::Clock` and `dmq::ThisThread` are not reimplemented for this port: they resolve to `std::mutex`, `std::chrono::steady_clock` and `std::this_thread`, as on the other desktop ports.

The tests are the same as the other `os/` samples: delegates, signals, `ScopedConnection`, cross-thread dispatch and a timer, then the shared `DelegateThreadsTests()` and `TimerDelegateTests()` from [test/portable](../../../../test/portable/PortableTests.h) (cross-thread dispatch, `FullPolicy`, `StartGate`, `PacedDispatch`, `TimerDelegate`). Only `DMQ_THREAD` differs.

No external dependency. Windows only: `CMakeLists.txt` stops with an error elsewhere.

## Build

```bash
cmake -B build .
cmake --build build --config Release
```

## Run

```bash
build\Release\delegate_win32.exe
```

It runs the tests and exits with status 0. A failed check faults through `DMQ_ASSERT_TRUE`.

```txt
=========================================
   WIN32 DELEGATE SYSTEM ONLINE
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction: 100
...
[Test 10] PacedDispatch & TimerDelegate Tests:
...
=========================================
           ALL TESTS PASSED
=========================================
```

`build_all.py` builds and runs it on Windows and requires `ALL TESTS PASSED`.
