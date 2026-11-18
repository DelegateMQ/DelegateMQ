# integration/clang-native

Comprehensive DelegateMQ feature demo that exercises **all major delegate types** in a single standalone application. Builds with LLVM/Clang (or any C++17 compiler) natively on Windows or Linux — no embedded toolchain or external library required.

## What it tests

| Test | Feature | Description |
| :--- | :--- | :--- |
| 1 | `Delegate<>` | Synchronous invocation: free function, member function, lambda |
| 2 | `DelegateAsync<>` | Fire-and-forget dispatch to a worker thread |
| 3 | `DelegateAsyncWait<>` | Blocking async call with return value and timeout |
| 4 | `MulticastDelegateSafe<>` | Thread-safe multicast to multiple targets; sync and async |
| 5 | `UnicastDelegateSafe<>` | Thread-safe single-target delegate; rebind and clear |
| 6 | `Signal<>` + `ScopedConnection` | Publish/subscribe with RAII auto-disconnect |

## Build

### Windows — LLVM/Clang (GNU-compatible front end)

Open a **VS Developer Command Prompt** (provides `rc.exe`, `lld-link.exe`, and Windows SDK headers). Then:

```powershell
# Configure — pass full path if clang++ is not on PATH
cmake -S . -B build ^
    -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" ^
    -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" ^
    -G Ninja

# Build
cmake --build build

# Run
.\build\delegate_app.exe
```

### Windows — Visual Studio (MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
.\build\Release\delegate_app.exe
```

Substitute `"Visual Studio 17 2022"` if using VS 2022.

### Linux — Clang

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/delegate_app
```

### Linux — GCC

```bash
cmake -S . -B build
cmake --build build
./build/delegate_app
```

## Expected output

```
=========================================
   DELEGATEMQ FULL-FEATURE DEMO
=========================================

[Test 1] Synchronous Delegates:
  FreeFunc called: 10
  PASS: Free function delegate invoked
  Handler::MemberFunc called: 20
  PASS: Member function delegate invoked
  Lambda called: 30 (capture=99)
  PASS: Lambda delegate invoked
  PASS: Empty delegate reports empty

[Test 2] DelegateAsync (fire-and-forget):
  ...
  PASS: DelegateAsync: both calls dispatched and invoked
  ...

[Test 3] DelegateAsyncWait (blocking, return value):
  ...
  PASS: DelegateAsyncWait: return value is correct (42 * 2 = 84)
  ...

[Test 4] MulticastDelegateSafe (thread-safe multicast):
  ...
  PASS: MulticastDelegateSafe: 3 targets all invoked
  ...

[Test 5] UnicastDelegateSafe (thread-safe single-target):
  ...
  PASS: UnicastDelegateSafe: empty after Clear()

[Test 6] Signal with ScopedConnection (RAII):
  ...
  PASS: Signal: ScopedConnections auto-disconnected on scope exit
  ...

=========================================
  RESULTS: 23 assertions passed
  ALL TESTS PASSED
=========================================
```

## Configuration

| CMake Option | Value | Notes |
| :--- | :--- | :--- |
| `DMQ_THREAD` | `DMQ_THREAD_STDLIB` | `std::thread` / `std::mutex` / `std::condition_variable` |
| `DMQ_SERIALIZE` | `DMQ_SERIALIZE_NONE` | No remote delegates in this demo |
| `DMQ_TRANSPORT` | `DMQ_TRANSPORT_NONE` | No transport in this demo |
| `DMQ_ALLOCATOR` | OFF | Standard `std::allocator` |
