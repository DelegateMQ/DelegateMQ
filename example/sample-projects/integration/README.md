# Integration samples

How to bring DelegateMQ into your own project and toolchain.

| Sample | Shows |
| :--- | :--- |
| [clang-native](./clang-native/README.md) | A CMake build with `DelegateMQ.cmake` that compiles with Clang, GCC or MSVC, using every delegate type. |
| [cmake-subdirectory](./cmake-subdirectory/README.md) | DelegateMQ added the way `docs/BUILD.md` recommends: `FetchContent` (or `add_subdirectory`) and the `DelegateMQ::DelegateMQ` target. Checks the options reach the app and nothing leaks into other targets. |
| [no-cmake](./no-cmake/README.md) | DelegateMQ built the way a non-CMake project would: one source file (`src/DelegateMQ.cpp`) and options in `DelegateMQConfig.h`. |

For a Keil MDK project see [../bare-metal/keil](../bare-metal/keil/README.md), and for PlatformIO see [example/platformio](../../platformio/PLATFORMIO.md). `build_all.py` builds and runs these samples; the test scripts name them `integration/<sample>`.
