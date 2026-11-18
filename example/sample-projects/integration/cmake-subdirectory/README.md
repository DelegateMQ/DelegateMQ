# integration/cmake-subdirectory — DelegateMQ as a CMake Dependency

An application that uses DelegateMQ the way [docs/BUILD.md](../../../../docs/BUILD.md#with-cmake) ("With CMake") tells users to: set `DMQ_*` options, bring DelegateMQ in with `FetchContent`, and link the `DelegateMQ::DelegateMQ` target. It uses nothing else from DelegateMQ: no `DelegateMQ.cmake` include, no source lists. The other samples include `DelegateMQ.cmake` directly, so this is the only build that tests the path most CMake users take.

`FetchContent_Declare` points at this checkout (`SOURCE_DIR`) instead of GitHub, so it builds the code here, offline. An application would use `GIT_REPOSITORY`/`GIT_TAG`, or `add_subdirectory()` on a clone or submodule, which goes through the same `CMakeLists.txt`.

## What it checks

| Check | Where |
| :--- | :--- |
| An option set before DelegateMQ is added (`DMQ_ALLOCATOR`) reaches the app through the target | `main.cpp`, at compile time |
| Adding DelegateMQ leaves other targets' compile definitions alone | `not_linked.cpp`, in a target that doesn't link DelegateMQ, at compile time |
| Only the library is built, not DelegateMQ's tests, samples or tools | `CMakeLists.txt`, at configure time |
| The library works: sync, async and async-wait delegates, a signal, a timer | `main.cpp`, at run time |

## Build and run

```bash
cmake -B build .
cmake --build build
./build/delegate_cmake_subdirectory      # Windows: build\Release\delegate_cmake_subdirectory.exe
```

It prints `ALL TESTS PASSED` and exits 0, or names the failed check and exits 1. `build_all.py` builds and runs it on Windows and Linux.
