# qt-linux — DelegateMQ on Qt Threads

This sample verifies DelegateMQ's `DMQ_THREAD_QT` port, `dmq::os::QtThread`
(`src/delegate-mq/port/os/qt/QtThread.h`). Each `dmq::os::Thread` is a `QThread`
running its own Qt event loop; a delegate dispatched to it is delivered as a queued Qt
signal and invoked on that thread.

It runs the same test suite as `posix-linux` and the RTOS simulator samples: delegate,
Signal, ScopedConnection, Thread and Timer usage, plus the full `DelegateThreadsTests()`/
`TimerDelegateTests()` battery (ported unchanged from `test/unit-tests/`). The application
code is unchanged across every port; only `DMQ_THREAD` changes.

Built and run in CI by `.github/workflows/qt.yml`. Not built by `build_all.py`, since most
machines don't have Qt installed.

## Requirements

Qt 6 Core only (no GUI modules), CMake 3.16+ and a C++17 compiler. On Debian/Ubuntu:

```bash
sudo apt install qt6-base-dev
```

With a Qt install from the Qt installer instead, add `-DCMAKE_PREFIX_PATH=<Qt>/6.x.x/gcc_64`
to the configure step.

## Platform

Linux (the CI target). The Qt port and this sample use no Linux-specific API, so Windows
and macOS with Qt 6 should work, but they aren't built.

## Port Notes

- **No message priority.** Qt's event queue is first-in, first-out, so `dmq::Priority` is
  ignored: a `Priority::URGENT` message runs after every message dispatched before it. The
  other thread ports run URGENT messages first. None of the tests here depend on ordering.
- **No main event loop needed.** `main()` creates a `QCoreApplication`, as a Qt program
  would, but never calls `exec()`. Each worker thread runs its own event loop, and the
  dispatching thread only needs to emit.

## Build Instructions

```bash
cmake -B build .
cmake --build build
```

The output executable is at `build/delegate_qt_linux`.

## Run Instructions

```bash
./build/delegate_qt_linux
```

It runs the test sequence and exits with status 0.

**Expected Output**

```txt
=========================================
   QT DELEGATE SYSTEM ONLINE
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
