# Build and Configuration Guide

DelegateMQ is a header-only C++ library. The core functionality requires no pre-compilation; you simply include the headers in your project. However, the library includes a comprehensive ecosystem of examples, tests, and TUI tools that require a build environment.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Main Application Build](#main-application-build)
- [Consuming as a Dependency (FetchContent)](#consuming-as-a-dependency-fetchcontent)
- [Example Ecosystem (Sandbox)](#example-ecosystem-sandbox)
  - [Automated Workspace Setup](#automated-workspace-sandbox-setup)
  - [Manual Sample Build](#manual-sample-build)
- [Configuration and Overrides](#configuration-and-overrides)
  - [CMake Options](#1-cmake-options)
  - [User Config File (`DelegateMQConfig.h`)](#2-user-config-file-delegatemqconfigh)
- [Platform Specifics](#platform-specifics)
  - [Embedded (ARM/Bare-Metal)](#embedded-armbare-metal)
  - [Windows / Linux](#windows--linux)

---

## Prerequisites
- **CMake**: 3.15 or higher.
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

---

## Consuming as a Dependency (FetchContent)
DelegateMQ isn't in vcpkg or Conan — its behavior is selected via compile-time options (`DMQ_THREAD_*`, `DMQ_SERIALIZE_*`, `DMQ_ALLOCATOR`, etc.), so there's no single prebuilt library target to hand a binary package manager. Instead, pull the source in with CMake's built-in `FetchContent` and build it as part of your own project, the same way `test/CMakeLists.txt` does internally:

```cmake
include(FetchContent)
FetchContent_Declare(
  delegatemq
  GIT_REPOSITORY https://github.com/DelegateMQ/DelegateMQ.git
  GIT_TAG v2.1.0
)
FetchContent_MakeAvailable(delegatemq)

# Set any DMQ_* options (DMQ_THREAD, DMQ_ALLOCATOR, etc.) before this include --
# see "Configuration and Overrides" below.
include("${delegatemq_SOURCE_DIR}/src/delegate-mq/DelegateMQ.cmake")

add_executable(my_app main.cpp ${DMQ_PORT_SOURCES})
target_include_directories(my_app PRIVATE ${DMQ_ROOT_DIR})
```

`DelegateMQ.cmake` resolves its own paths from `CMAKE_CURRENT_LIST_DIR`, so it works identically whether it's reached via `FetchContent`, a git submodule, or a plain manual clone.

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

### Manual Sample Build
If you want to build a specific sample without scripts, navigate to its directory (its third-party dependencies must already be built, e.g. by `02_build_libs.py`):
```bash
cd example/sample-projects/zeromq-msgpack-cpp
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
| `DMQ_LOG` | `OFF` | Enable verbose internal debug logging (requires spdlog). |
| `DMQ_TOOLS` | `OFF` | Build the diagnostic tools (`dmq-spy`, `dmq-monitor`, `dmq-thread`); see [Tools](../tools/TOOLS.md). |
| `DMQ_FORCE_OPTIMIZE_DEBUG` | `OFF` | Force `-Os` template optimization in unoptimized GCC/Clang Debug builds to reduce embedded flash bloat (silent no-op on MSVC). |

### 2. User Config File (`DelegateMQConfig.h`)
For fine-tuning numeric constants without editing library files:
1. Copy `src/delegate-mq/delegate/DelegateMQConfig_Template.h` to your project and rename it `DelegateMQConfig.h`.
2. Set the CMake variable `-DDMQ_USER_CONFIG="DelegateMQConfig.h"`.
3. Modify the constants as needed (e.g., `DMQ_MAX_TIMER_EXPIRED`, `DMQ_DEFAULT_QUEUE_SIZE`).

---

## Platform Specifics

### Embedded (ARM/Bare-Metal)
- **Stack Usage & Code Bloat**: In unoptimized Debug mode (`-O0`), variadic templates generate deep call stacks and heavy flash usage. Either use Release mode (`-O2`/`-Os`), increase your RTOS task stack sizes (min 4-8KB recommended), or enable `-DDMQ_FORCE_OPTIMIZE_DEBUG=ON` to eliminate template bloat while keeping your application debuggable.
- **Static Memory**: Use `-DDMQ_ALLOCATOR=ON` to ensure all internal library allocations use fixed-sized pools, avoiding heap fragmentation.

### Windows / Linux
- **Standard Library**: On PC platforms, the library defaults to using `std::thread`, `std::mutex`, and `std::chrono` via the `stdlib` port.
- **TUI Tools**: Running the `dmq-spy` and `dmq-monitor` tools requires a terminal that supports ANSI escape codes (Windows Terminal, iTerm2, xterm).
