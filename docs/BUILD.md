# Build and Configuration Guide

DelegateMQ is mostly headers. The delegates themselves are header-only; the OS ports, allocator, fault handler and a few utilities are a small set of `.cpp` files, built for you by CMake or added as a single file without it (see [Adding DelegateMQ to Your Project](#adding-delegatemq-to-your-project)). The repository also includes examples, tests and TUI tools, which need a build environment.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Main Application Build](#main-application-build)
  - [Checking for Heap Use Outside the Fixed-Block Allocator](#checking-for-heap-use-outside-the-fixed-block-allocator)
- [Adding DelegateMQ to Your Project](#adding-delegatemq-to-your-project)
  - [With CMake](#with-cmake)
  - [Without CMake](#without-cmake)
- [Example Ecosystem (Sandbox)](#example-ecosystem-sandbox)
  - [Automated Workspace Setup](#automated-workspace-sandbox-setup)
  - [Full Test Suite (`--full`)](#full-test-suite---full)
  - [Manual Sample Build](#manual-sample-build)
- [Configuration and Overrides](#configuration-and-overrides)
  - [CMake Options](#1-cmake-options)
  - [User Config File (`DelegateMQConfig.h`)](#2-user-config-file-delegatemqconfigh)
- [Platform Specifics](#platform-specifics)
  - [Embedded (ARM/Bare-Metal)](#embedded-armbare-metal)
  - [Windows / Linux](#windows--linux)

---

## Prerequisites
- **CMake**: 3.16 or higher for the library and most samples; 3.20 or higher for `build_all.py` (it also configures the Pumptron STM32F4 firmware) and the Zephyr samples. On Windows, CMake must also know your Visual Studio version: 3.21+ for Visual Studio 2022, 4.2+ for Visual Studio 2026. `build_all.py` checks this before it starts.
- **C++ Compiler**: C++17 compliant (GCC 8+, Clang 7+, MSVC 2019+).
- **Python 3**: Required for the automated sample build scripts.

---

## Main Application Build
To build the primary `delegate_app` (which contains basic feature demonstrations) with no third-party dependencies:

```bash
git clone https://github.com/DelegateMQ/DelegateMQ.git
cd DelegateMQ
cmake -B build
cmake --build build
```

The output executable will be located in `build/delegate_app/`.

### Checking for Heap Use Outside the Fixed-Block Allocator

With `DMQ_ALLOCATOR`, the library should allocate through the fixed-block allocator, not global `operator new`. `test/AllocTrack.cpp` checks this (Windows/MSVC only, off unless `DMQ_ALLOC_TRACK` is defined). It replaces global `new`/`delete` with `malloc`/`free`, records the call stack of every global `new` during the 2nd and 3rd pass of the test loop in `test/main.cpp` (the 1st pass warms up the allocator pools), and writes `alloc_report.txt`, grouped by the innermost DelegateMQ or test frame:

```bash
cmake -S test -B build-alloctrack -A x64 -DDMQ_ALLOCATOR=ON "-DCMAKE_CXX_FLAGS=-DWIN32 -D_WINDOWS -EHsc -DDMQ_ALLOC_TRACK" "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -Ob0 -Zi -DNDEBUG"
cmake --build build-alloctrack --config RelWithDebInfo
cd build-alloctrack/RelWithDebInfo
./delegate_app.exe
```

`-Ob0` turns off inlining so every frame and line number shows up. In the report, `LIBRARY` groups are library code allocating with global `new`. `LIBRARY desktop-only port` groups are the stdlib/Win32 thread ports and desktop transports, which may use `std::` allocation. `TEST/SAMPLE` groups are test code, including user-owned types (`std::string`, `std::vector`) passed through the library.

---

## Adding DelegateMQ to Your Project
There are two ways to add DelegateMQ to an application:

- **[With CMake](#with-cmake):** link the `DelegateMQ::DelegateMQ` target, and set options in CMake.
- **[Without CMake](#without-cmake)** (Keil, IAR, STM32CubeIDE, PlatformIO, Makefiles): add one source file or one folder, and set options in a header.

DelegateMQ isn't in vcpkg or Conan. Its behavior is chosen with compile-time options (`DMQ_THREAD_*`, `DMQ_SERIALIZE_*`, `DMQ_ALLOCATOR`, ...), so there's no single prebuilt binary to package.

### With CMake
Pull the source in with `FetchContent` (or `add_subdirectory` on a submodule or clone), and link the target:

```cmake
# Set any DMQ_* options before DelegateMQ is added (see "Configuration and
# Overrides" below). The defaults (stdlib threads, built-in serializer, UDP)
# need no third-party code.
set(DMQ_ALLOCATOR "ON")

include(FetchContent)
FetchContent_Declare(
  delegatemq
  GIT_REPOSITORY https://github.com/DelegateMQ/DelegateMQ.git
  GIT_TAG master   # or a release tag newer than v2.2.0
)
FetchContent_MakeAvailable(delegatemq)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE DelegateMQ::DelegateMQ)
```

What you get:
- Only the DelegateMQ static library is built. The tests, samples and tools are skipped.
- The target carries what the selected options need: the `DMQ_*` definitions, the include directories (third-party ones marked `SYSTEM`), and the libraries DelegateMQ can resolve itself (ZeroMQ, ThreadX, Qt, `Threads::Threads` on Linux).
- Nothing is added to your own directory's compile definitions.

**Third-party ports need `DMQ_DEPS_DIR`.** `external/` is empty in a fresh clone, so selecting a port that uses a third-party library (ZeroMQ, NNG, MQTT, MessagePack, FreeRTOS, ThreadX, ...) stops configuration until `DMQ_DEPS_DIR` points at those libraries. Run `scripts/01_fetch_repos.py` and `02_build_libs.py` once (see [Third-Party Directory](#third-party-directory-dmq_deps_dir)), or lay out your own builds the same way.

Your application still provides:
- **NNG, MQTT and libserialport libraries.** The target has their include directories; your project links the library itself, as the samples do. NNG and Paho MQTT C come from `DMQ_DEPS_DIR` (`<lib>/install`, where `02_build_libs.py` puts them) on Linux as on Windows; to use another install instead, such as a system one under `/usr/local`, set `-DDMQ_NNG_PREFIX=<path>` or `-DDMQ_MQTT_PREFIX=<path>` (Linux).
- **The RTOS kernel and its config header.** Add the directory with your `FreeRTOSConfig.h` (or equivalent) to the target: `target_include_directories(DelegateMQ PUBLIC config/)`. Your application builds the FreeRTOS kernel sources (`FREERTOS_SOURCES` when using `DMQ_DEPS_DIR`), since they depend on your config, port and heap choice. ThreadX's own CMake target is linked for you. Zephyr and NuttX also need the target linked to the RTOS build's interface target.
- **A user config header**, if you use one: set `DMQ_USER_CONFIG` and `DMQ_USER_CONFIG_DIR` (see [User Config File](#2-user-config-file-delegatemqconfigh)).

#### Including `DelegateMQ.cmake` Directly
The older approach, which every sample in this repository uses: include `DelegateMQ.cmake` and add the source lists to your own target.

```cmake
include("${delegatemq_SOURCE_DIR}/src/delegate-mq/DelegateMQ.cmake")

add_executable(my_app main.cpp ${DMQ_PORT_SOURCES})
target_include_directories(my_app PRIVATE ${DMQ_ROOT_DIR})
```

- The `DMQ_*` definitions apply to the whole calling directory (`add_compile_definitions`). Set `DMQ_NO_GLOBAL_DEFINITIONS ON` before the include to avoid that.
- To get a target instead, call `dmq_add_library(<name>)` after the include.
- One build tree can hold several configurations (for example a FreeRTOS controller and a stdlib GUI): include once per configuration, each with its own `dmq_add_library` target. Each include recomputes source lists, third-party paths and DelegateMQ's defaults; variables you set yourself carry over as usual in CMake.
- One edge case: if, between includes, you set an option to the value DelegateMQ defaulted it to last time, it's treated as a default and may be recalculated. To pin a value across includes, set it before the first include or as a cache variable (`-DDMQ_DATABUS=OFF`).
- `DelegateMQ.cmake` finds its own paths, so it works the same from `FetchContent`, a submodule or a plain clone.

### Without CMake
1. **Include path:** add `src/delegate-mq`.
2. **Options:** copy `src/delegate-mq/delegate/DelegateMQConfig_Template.h` into your project as `DelegateMQConfig.h`, on the include path, and select ports and options there (`#define DMQ_THREAD_FREERTOS`, `#define DMQ_ALLOCATOR`, ...). It's found automatically, so no `-D` flags are needed. Anything left undefined is auto-selected: stdlib threads, the built-in serializer and UDP on Windows/Linux, nothing on bare metal.
3. **Sources:** add **one** of these, not both:
   - **`src/DelegateMQ.cpp`**, a single file. Best when you list files yourself (Keil, IAR, Makefiles). It sits beside the `delegate-mq` folder, so copy it too if you copy only that folder. Example: `example/sample-projects/bare-metal/qemu/Makefile`.
   - **Every `.cpp` in `src/delegate-mq`.** Best for IDEs that compile a whole folder (STM32CubeIDE, PlatformIO). Files for ports you didn't select compile to nothing, so there's nothing to exclude. Example: the STM32CubeIDE project in `example/sample-projects/boards/stm32-freertos/server`.

   `example/sample-projects/integration/no-cmake` builds the single-file way on Windows and Linux, and `build_all.py` builds and runs it.
4. **Third-party and RTOS code:** add the include paths and libraries your ports need, e.g. FreeRTOS's `include/` and `portable/<port>` plus your `FreeRTOSConfig.h`. The RTOS kernel itself is part of your application.

Requirements:
- **The same options everywhere.** Keep them in `DelegateMQConfig.h` (or identical `-D` flags for the whole project), not in per-file settings.
- **C++17 with RTTI.** Exceptions are optional; `DMQ_ASSERTS` routes errors to `FaultHandler()`.
- **Bare metal (`DMQ_THREAD_NONE`):** define `extern "C" volatile uint64_t g_ticks;` and increment it every millisecond, e.g. from `SysTick_Handler`. See `example/sample-projects/bare-metal/qemu` (`targets/*/bsp.cpp`).
- **Your own fault handler (optional):** define `DMQ_CUSTOM_FAULT_HANDLER`, and the library's `port/fault/Fault.cpp` compiles to nothing. Your application then defines `dmq::util::FaultHandler()`, the C `FaultHandler()` and `WatchdogHandler()`, and `InstallCrashHandlers()` if you call it. See `example/pumptron/controller/platform/f4/Fault.cpp`.

---

### PlatformIO
DelegateMQ is a PlatformIO library: `lib_deps = endurodave/DelegateMQ@^2.4.0`, with C++17 and RTTI enabled and `DelegateMQConfig.h` in the project's `include/` folder. PlatformIO compiles every `.cpp` in `src/delegate-mq`, so it follows the [Without CMake](#without-cmake) rules.

See [PLATFORMIO.md](../example/platformio/PLATFORMIO.md) for supported boards, project setup, thread ports (ESP32, STM32 with FreeRTOS, bare metal), the example sketch and troubleshooting.

---

## Example Ecosystem (Sandbox)
Most advanced examples (ZeroMQ, MQTT, MessagePack, etc.) depend on third-party libraries. We provide scripts that fetch and build all dependencies into `external/` inside the repository (gitignored).

### Automated Workspace Sandbox Setup
1. Clone the repository:
   ```bash
   git clone https://github.com/DelegateMQ/DelegateMQ.git
   cd DelegateMQ
   ```
2. Run all the steps in order:
   ```bash
   python3 build_all.py                # 01 -> 05; --from N resumes at step N
   ```
   Or run the numbered scripts in `scripts/` one at a time:
   ```bash
   python3 scripts/01_fetch_repos.py       # Clone 3rd-party dependencies
   python3 scripts/02_build_libs.py        # Build dependencies as static libraries
   python3 scripts/03_generate_samples.py  # Generate CMake files for all samples
   python3 scripts/04_build_samples.py     # Compile all samples
   python3 scripts/05_run_samples.py       # Execute all and report pass/fail
   ```
   `build_all.py` stops if step 01 or 02 fails, and passes `--clang`, `--clean` and `--deps-dir` through to the scripts that take them.

### Third-Party Directory (`DMQ_DEPS_DIR`)
`01_fetch_repos.py` clones into `<repo>/external/` by default. To put the dependencies somewhere else, for example one copy shared by several clones or git worktrees, set the `DMQ_DEPS_DIR` environment variable, or pass `--deps-dir` to the `01`/`02` scripts and `-DDMQ_DEPS_DIR=<path>` to CMake. CMake resolves it in `src/delegate-mq/DepsDir.cmake`.

`git clean -xdf` deletes `external/` along with build output, because it's gitignored. Use `git clean -xdf -e external` to keep it.

Older versions cloned the dependencies next to the repository (`<parent>/zeromq`, `<parent>/FreeRTOS`, ...). That layout is no longer used: rerun `01_fetch_repos.py` and `02_build_libs.py`, then delete the old folders.

### Full Test Suite (`--full`)
A plain `build_all.py` run skips whatever the machine lacks the tools for. `--full` runs everything the platform can run, and treats a missing tool or an unbuilt sample as a failure, so a clean run means it all ran:

- every sample, as usual, plus the clang builds on Linux (`--full` implies `--clang` there)
- the unit-test app in `test/`, in every build listed in `scripts/full_suite.py`: the default build, clang, AddressSanitizer, ThreadSanitizer and UndefinedBehaviorSanitizer (Linux), big-endian s390x and 32-bit Arm (armhf) builds run under QEMU user mode (Linux), the `DMQ_ALLOCATOR`/`DMQ_ASSERTS` embedded configuration, and the stress tests
- a cross-endian check (Linux): `system-architecture/no-deps` with one side native x86 (little-endian) and the other built for s390x (big-endian) under QEMU user mode, both ways round, checking each side decodes the other's IDs
- real Cortex-M and RISC-V CPUs under QEMU, each with a real timer interrupt: the `bare-metal/qemu` targets (Cortex-M0, M3, M4, RV32), FreeRTOS on a Cortex-M3 (`os/freertos-bare-metal`'s `arm-m3` target) and Zephyr and CMSIS-RTOS2 on Zephyr's `mps2/an385` board (`os/zephyr`, `os/cmsis-rtos2`, Linux). The RTOS ones check `dmq::CriticalSection` from the interrupt

Results are also written to `full-suite-results/<platform>.json` (gitignored).

#### Where the Builds Go
`build_all.py` builds outside the source tree, in `out/<platform>/<path of the project>/<variant>` (gitignored), the layout Zephyr's `twister` uses: for example `out/windows/example/sample-projects/databus/basic/client/default`, `out/linux/example/sample-projects/transport/serializer/udp-builtin-clang` or `out/linux/test/asan`. `<platform>` is `windows`, `linux`, or `linux-docker` for `docker/run.sh`. Moving or renaming a sample leaves no stale build behind it, and deleting `out/` removes every build. A sample built by hand, as its README shows, still uses its own `build/` directory. `interop/native`'s library is copied to `interop/python/` after it's built, where the Python and C# interop clients find it when there's no `interop/native/build`.

```bash
python3 build_all.py --full
```

`transport/serializer` is built and run once per transport/serializer combination in `full_suite.TRANSPORT_COMBOS`, each in its own `<transport>-<serializer>` build directory. Samples that need a broker, a serial port pair or an emulator are run with them: `transport/mqtt` against a mosquitto broker started for the run, `transport/serialport` over a `socat` pseudo-terminal pair (Linux), the `bare-metal` targets on QEMU, the Zephyr samples on `native_sim` (with `ZEPHYR_BASE` set) and `os/nuttx` on NuttX's `sim` board (with `NUTTX_SRC` set). Samples that need real hardware (`boards/stm32-freertos`) are listed in `full_suite.PENDING` and reported as skipped with the reason; `bare-metal/keil` needs Keil MDK and is never built. Windows-only samples are skipped on Linux and the reverse: a full run needs one run on each platform.

#### In Docker (Linux)
`docker/Dockerfile` has every Linux tool `--full` needs, and `docker/run.sh` runs `build_all.py` in it on the current checkout. It needs Docker, with your user in the `docker` group:

```bash
docker/run.sh --full               # builds the image on first use
docker/run.sh --full --from 3      # resume at step 03
```

The third-party libraries go to `~/.cache/delegatemq/external` (override with `DMQ_DEPS_DIR`) and are kept between runs. Build directories land in the checkout under `out/linux-docker/`, apart from a native Linux run's `out/linux/`, since a container build records the container's `/work` paths.

#### Both Platforms from One Command
From Windows, `--linux-host` runs the Linux half on a Linux machine at the same time as the local run, and prints one combined table:

```bash
python build_all.py --linux-host user@linux-box
python build_all.py --linux-host user@linux-box --from 3    # --from/--to/--clean apply to both
```

It copies the working tree, uncommitted changes included, to a checkout on that machine (`~/dmq-full/DelegateMQ` by default, cloned on first use; `--linux-dir` changes it) and runs `docker/run.sh --full` there. The copy is a temporary commit pushed to that checkout, so only changes travel and its build directories stay for incremental builds. The Linux output is saved to `full-suite-results/linux.log`; its step banners and failures are also shown with a `[linux]` prefix.

The Linux machine needs git, Docker (with your user in the `docker` group) and key-based ssh login from this machine.

### Manual Sample Build
If you want to build a specific sample without scripts, navigate to its directory (its third-party dependencies must already be built, e.g. by `02_build_libs.py`):
```bash
cd example/sample-projects/system-architecture/no-deps/server
cmake -B build
cmake --build build
```

---

## Configuration and Overrides
DelegateMQ behavior can be customized at build time in two ways.

### 1. CMake Options
Passed via `-D` flags during configuration. These are the most common overrides:

| Option | Default | Description |
| :--- | :--- | :--- |
| `DMQ_THREAD` | Platform (`DMQ_THREAD_STDLIB` on Windows/Linux) | OS port: `DMQ_THREAD_STDLIB`, `_WIN32`, `_FREERTOS`, `_THREADX`, `_ZEPHYR`, `_CMSIS_RTOS2`, `_NUTTX`, `_NONE`, etc. |
| `DMQ_SERIALIZE` | `DMQ_SERIALIZE_SERIALIZE` (Windows/Linux) | Serializer port, e.g. `DMQ_SERIALIZE_MSGPACK`, `DMQ_SERIALIZE_RAPIDJSON`, `DMQ_SERIALIZE_NONE`. |
| `DMQ_TRANSPORT` | Platform UDP (Windows/Linux) | Transport port, e.g. `DMQ_TRANSPORT_ZEROMQ`, `DMQ_TRANSPORT_WIN32_TCP`, `DMQ_TRANSPORT_NONE`. |
| `DMQ_ASSERTS` | `OFF` | Use asserts routed to `FaultHandler` instead of C++ exceptions (auto-enabled when exceptions are disabled). |
| `DMQ_ALLOCATOR` | `OFF` | Use fixed-block memory allocator (deterministic) instead of heap. |
| `DMQ_DATABUS` | `ON` (`OFF` on RTOS and bare metal) | Build the DataBus middleware (`extras/databus`). |
| `DMQ_UTIL` | `ON` | Build the utility classes (`extras/util`: `Timer`, monitors, `ReliableTransport`, ...). |
| `DMQ_LOG` | `OFF` | Enable verbose internal debug logging (requires spdlog). |
| `DMQ_TOOLS` | `OFF` | Build the diagnostic tools (`dmq-spy`, `dmq-monitor`, `dmq-thread`, and the `dmq-target` test app); see [Tools](../tools/TOOLS.md). |
| `DMQ_STRICT` | `OFF` | Enable strict compiler warnings, treated as errors, for DelegateMQ sources. |
| `DMQ_FORCE_OPTIMIZE_DEBUG` | `OFF` | Force `-Os` template optimization in unoptimized GCC/Clang Debug builds to reduce embedded flash bloat (silent no-op on MSVC). |

### 2. User Config File (`DelegateMQConfig.h`)
For fine-tuning numeric constants without editing library files (and, without CMake, for selecting ports; see [Without CMake](#without-cmake)):
1. Copy `src/delegate-mq/delegate/DelegateMQConfig_Template.h` to your project and rename it `DelegateMQConfig.h`.
2. Put its directory on the include path and it's used automatically. With the `DelegateMQ::DelegateMQ` target, `DMQ_USER_CONFIG_DIR` adds that directory for you. To name the file explicitly instead, set the CMake variable `DMQ_USER_CONFIG` (`-DDMQ_USER_CONFIG="DelegateMQConfig.h"`).
3. Modify the constants as needed (e.g., `DMQ_MAX_TIMER_EXPIRED`, `DMQ_DEFAULT_QUEUE_SIZE`).

---

## Platform Specifics

### Embedded (ARM/Bare-Metal)
- **Stack Usage & Code Bloat**: In unoptimized Debug mode (`-O0`), variadic templates generate deep call stacks and heavy flash usage. Either use Release mode (`-O2`/`-Os`), increase your RTOS task stack sizes (min 4-8KB recommended), or enable `-DDMQ_FORCE_OPTIMIZE_DEBUG=ON` to eliminate template bloat while keeping your application debuggable.
- **Static Memory**: Use `-DDMQ_ALLOCATOR=ON` to ensure all internal library allocations use fixed-sized pools, avoiding heap fragmentation.

### Windows / Linux
- **Standard Library**: On PC platforms, the library defaults to using `std::thread`, `std::mutex`, and `std::chrono` via the `stdlib` port.
- **TUI Tools**: Running the `dmq-spy` and `dmq-monitor` tools requires a terminal that supports ANSI escape codes (Windows Terminal, iTerm2, xterm).
