# Build and Configuration Guide

DelegateMQ is a header-only C++ library. The core functionality requires no pre-compilation; you simply include the headers in your project. However, the library includes a comprehensive ecosystem of examples, tests, and TUI tools that require a build environment.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Main Application Build](#main-application-build)
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

## Example Ecosystem (Sandbox)
Most advanced examples (ZeroMQ, MQTT, MessagePack, etc.) depend on third-party libraries. We provide scripts to automate setting up a "Sandbox Workspace" where all dependencies are built as siblings of the main repository.

### Automated Workspace Sandbox Setup
1. Create a parent workspace directory:
   ```bash
   mkdir DMQWorkspace && cd DMQWorkspace
   git clone https://github.com/DelegateMQ/DelegateMQ.git
   cd DelegateMQ
   ```
2. Run the numbered scripts in order:
   ```bash
   python3 01_fetch_repos.py       # Clone 3rd-party dependencies
   python3 02_build_libs.py        # Build dependencies as static libraries
   python3 03_generate_samples.py  # Generate CMake files for all samples
   python3 04_build_samples.py     # Compile all samples
   python3 05_run_samples.py       # Execute all and report pass/fail
   ```

### Manual Sample Build
If you want to build a specific sample without scripts, navigate to its directory:
```bash
cd example/sample-projects/tcp-msgpack-cpp
cmake -B build
cmake --build build
```

---

## Configuration and Overrides
DelegateMQ behavior can be customized at build-time using three levels of precedence.

### 1. CMake Options
Passed via `-D` flags during configuration. These are the most common overrides:

| Option | Default | Description |
| :--- | :--- | :--- |
| `DMQ_ASSERTS` | `OFF` | Enable internal library assertions. |
| `DMQ_ALLOCATOR` | `OFF` | Use fixed-block memory allocator (deterministic) instead of heap. |
| `DMQ_DEBUG_LOG` | `OFF` | Enable verbose internal debug logging (requires spdlog). |

### 2. User Config File (`DelegateMQConfig.h`)
For fine-tuning numeric constants without editing library files:
1. Copy `src/delegate-mq/delegate/DelegateMQConfig_Template.h` to your project and rename it `DelegateMQConfig.h`.
2. Set the CMake variable `-DDMQ_USER_CONFIG="DelegateMQConfig.h"`.
3. Modify the constants as needed (e.g., `DMQ_MAX_TIMER_EXPIRED`, `DMQ_DEFAULT_QUEUE_SIZE`).

---

## Platform Specifics

### Embedded (ARM/Bare-Metal)
- **Stack Usage**: In Debug mode (`-O0`), templates may generate deep call stacks. Use Release mode (`-O2`) or increase your task stack sizes (min 4-8KB recommended).
- **Static Memory**: Use `-DDMQ_ALLOCATOR=ON` to ensure all internal library allocations use fixed-sized pools, avoiding heap fragmentation.

### Windows / Linux
- **Standard Library**: On PC platforms, the library defaults to using `std::thread`, `std::mutex`, and `std::chrono` via the `stdlib` port.
- **TUI Tools**: Building the `dmq-spy` and `dmq-monitor` tools requires a terminal that supports ANSI escape codes (Windows Terminal, iTerm2, xterm).
