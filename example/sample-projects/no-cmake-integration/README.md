# No-CMake Integration Example

Builds and checks DelegateMQ the way a project **without CMake** uses it (Keil, IAR, STM32CubeIDE, Makefiles, ...), on Windows and Linux:

* DelegateMQ is a single source file, `src/DelegateMQ.cpp`.
* `src/delegate-mq` is on the include path.
* Options come from `config/DelegateMQConfig.h`, which is found automatically because `config/` is on the include path. No `-D` flags.

`CMakeLists.txt` exists only so `build_all.py` builds and runs this sample. It deliberately doesn't include `DelegateMQ.cmake` or set any `DMQ_*` options, so it mirrors what you'd put in any other build system. See "Without CMake" in [docs/BUILD.md](../../../docs/BUILD.md#without-cmake).

## Build and Run

```bash
cmake -B build
cmake --build build

# Windows
build\Debug\delegate_app.exe
# Linux
./build/delegate_app
```

`main.cpp` checks synchronous and asynchronous delegates, a signal, and a timer, then prints `ALL TESTS PASSED` and returns 0. `config/DelegateMQConfig.h` turns on the fixed-block allocator (`DMQ_ALLOCATOR`); `main.cpp` stops the build if the header isn't picked up.
