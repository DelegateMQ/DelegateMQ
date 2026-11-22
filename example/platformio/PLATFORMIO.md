# DelegateMQ with PlatformIO

DelegateMQ is a [PlatformIO](https://platformio.org/) library: add one line to `platformio.ini` and use delegates, signals and async calls on ESP32, STM32, Raspberry Pi Pico and the desktop.

- [Supported Boards](#supported-boards)
- [Quick Start](#quick-start)
- [Project Setup](#project-setup)
  - [platformio.ini](#platformioini)
  - [C++17](#c17)
  - [Configuration Header](#configuration-header)
- [Thread Ports](#thread-ports)
  - [ESP32 (FreeRTOS)](#esp32-freertos)
  - [STM32 with FreeRTOS](#stm32-with-freertos)
  - [Bare Metal (No RTOS)](#bare-metal-no-rtos)
  - [Desktop (native)](#desktop-native)
- [The Example Sketch](#the-example-sketch)
- [Troubleshooting](#troubleshooting)
- [Developing DelegateMQ with PlatformIO](#developing-delegatemq-with-platformio)

## Supported Boards

The example sketch, [`example/platformio/async-delegates`](async-delegates/src/main.cpp), is built for each board below by the [PlatformIO CI workflow](../../.github/workflows/platformio.yml).

| Board | `env` | Thread port | Async delegates | Verified |
| :--- | :--- | :--- | :--- | :--- |
| ESP32 DevKit | `esp32dev` | FreeRTOS (Arduino-ESP32) | Yes | CI build |
| ESP32-C3 DevKitM-1 | `esp32-c3` | FreeRTOS (Arduino-ESP32) | Yes | CI build |
| ESP32-S3 DevKitC-1 | `esp32-s3` | FreeRTOS (Arduino-ESP32) | Yes | CI build |
| STM32F4 Discovery | `disco_f407vg` | FreeRTOS (STM32duino FreeRTOS) | Yes | CI build; run on the board |
| STM32 Nucleo-F401RE | `nucleo_f401re` | None (bare metal) | No | CI build |
| Raspberry Pi Pico (RP2040) | `pico` | None (bare metal) | No | CI build |
| Desktop (Linux, macOS, Windows with MinGW) | `native` | stdlib threads | Yes | CI build and run |

Other 32-bit boards with a GCC that supports C++17 should work, but aren't built.

**Not supported: 8-bit AVR** (Arduino Uno, Mega 2560, Nano). Its compiler has no C++ standard library (`<functional>`, `<memory>`, `<chrono>`), which DelegateMQ is built on.

## Quick Start

Build the example for a board, flash it and open the serial monitor:

```bash
git clone https://github.com/DelegateMQ/DelegateMQ.git
cd DelegateMQ/example/platformio/async-delegates
pio run -e esp32dev -t upload -t monitor
```

Or run it on the desktop, no board needed:

```bash
pio run -e native -t exec
```

Expected output (async part on boards with threads):

```
DelegateMQ PlatformIO example
Synchronous delegates
  OnTemperature: 21 C
Signal
  Display::Show: 42
Asynchronous delegates on thread 'Worker'
  Task 99
  Task 1
  Task 2
  Task 3
  Add(2, 3) on Worker returned 5
Done
```

Task 99 is sent last with `Priority::URGENT` and runs first.

## Project Setup

### platformio.ini

Create a project for your board, then edit its `platformio.ini`:

```bash
pio project init --board esp32dev --project-option "framework=arduino"
```

`pio boards <text>` finds a board's ID (e.g. `pio boards discovery`). PlatformIO downloads DelegateMQ from `lib_deps` on the first build into `.pio/libdeps/<env>/DelegateMQ`; the project itself holds only your code.

A minimal ESP32 project:

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
lib_deps  = endurodave/DelegateMQ@^3.0.0
; DelegateMQ needs C++17; Arduino cores default to C++11 or C++14
build_unflags = -std=gnu++11 -std=gnu++14 -std=c++11 -std=c++14
build_flags   = -std=gnu++17 -DDMQ_THREAD_FREERTOS
```

PlatformIO compiles every `.cpp` under the library's `src/delegate-mq`. Each file compiles to nothing unless the selected options need it, so there's no file list to maintain (see [Without CMake](../../docs/BUILD.md#without-cmake)).

### C++17

DelegateMQ needs C++17. Most Arduino cores compile C++ as `gnu++11` or `gnu++14`, so remove the core's standard with `build_unflags` and add `-std=gnu++17` to `build_flags`.

RTTI is optional. Most cores compile with `-fno-rtti`, and DelegateMQ then builds without it (`DMQ_NO_RTTI`, automatic). If your project turns RTTI on, turn it on for every C++ file, DelegateMQ's included: the library builds with the project's flags, and mixing the two is undefined. Without RTTI, a delegate made from a `std::function` object equals only its own copies; see [RTTI Is Optional](../../docs/SAFETY.md#rtti-is-optional-dmq_no_rtti).

### Configuration Header

Options and capacities go in a `DelegateMQConfig.h` (copy `src/delegate-mq/delegate/DelegateMQConfig_Template.h`). Put it in the project's **`include/`** folder, not `src/`. PlatformIO compiles the library with `include/` on the path but not `src/`, so a config in `src/` is seen by your code and not by the library, and the two disagree (for example, link errors for `FreeRTOSThread`).

```cpp
// include/DelegateMQConfig.h
#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

#define DMQ_DEFAULT_QUEUE_SIZE      10      // messages per thread queue
#define DMQ_FREERTOS_STACK_WORDS    4096    // worker task stack

#endif
```

The thread port (`DMQ_THREAD_*`) can go here or in `build_flags`. The example uses `build_flags`, so one `DelegateMQConfig.h` serves every board.

## Thread Ports

With no `DMQ_THREAD_*` defined, a board build uses `DMQ_THREAD_NONE` (bare metal) and the desktop uses `DMQ_THREAD_STDLIB`.

### ESP32 (FreeRTOS)

Add `-DDMQ_THREAD_FREERTOS`. Arduino-ESP32 and ESP-IDF already run FreeRTOS, and `setup()`/`loop()` run in a FreeRTOS task, so nothing else is needed. The port detects `ESP_PLATFORM` and uses ESP-IDF's `freertos/` header paths, its SMP spinlock critical section (`portENTER_CRITICAL_SAFE`) and `xPortInIsrContext()`. ESP32 (Xtensa, dual core) and ESP32-C3 (RISC-V, single core) both work.

A `dmq::os::Thread` worker's default priority is `configMAX_PRIORITIES - 2`, above Arduino's loop task. On a single core, a message sent to it usually runs at once, before the sender continues. To queue several messages first (as the example does to show priority), create the worker with a [`dmq::os::StartGate`](../../docs/PORTING.md#start-gate-synchronized-start) and open the gate after sending them.

### STM32 with FreeRTOS

The STM32 Arduino core (STM32duino) has no RTOS. Add the STM32duino FreeRTOS library and start the scheduler yourself:

```ini
[env:disco_f407vg]
platform  = ststm32
board     = disco_f407vg
framework = arduino
lib_deps  = endurodave/DelegateMQ@^3.0.0
            stm32duino/STM32duino FreeRTOS@^10.3.2
build_unflags = -std=gnu++11 -std=gnu++14 -std=c++11 -std=c++14
build_flags   = -std=gnu++17 -DDMQ_THREAD_FREERTOS
```

```cpp
#include "DelegateMQ.h"
#include <STM32FreeRTOS.h>

static void AppTask(void*)
{
    // Create dmq::os::Thread workers and run the application here
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void setup()
{
    Serial.begin(115200);
    // Below the worker threads' default priority (configMAX_PRIORITIES - 2)
    xTaskCreate(AppTask, "App", 1024, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    vTaskStartScheduler();  // doesn't return
}

void loop() {}
```

STM32duino FreeRTOS leaves `configSUPPORT_STATIC_ALLOCATION` off, so threads use the heap, and `Thread::SetStackMem()` with a buffer faults. To give a thread a static stack, provide your own `FreeRTOSConfig.h` with static allocation on.

**STM32F4 Discovery serial output:** its ST-LINK/V2 has no USB serial port. The example sends `Serial` to USART6 (TX PC6, RX PC7) with `-DSERIAL_UART_INSTANCE=6 -DPIN_SERIAL_TX=PC6 -DPIN_SERIAL_RX=PC7`; connect a 3.3 V USB-UART adapter (adapter RX to PC6, TX to PC7, GND to GND) at 115200 8N1. Boards with an ST-LINK/V2-1 (Nucleo) have a USB serial port and need nothing extra. Flash with `upload_protocol = stlink`.

### Bare Metal (No RTOS)

With `DMQ_THREAD_NONE` you get synchronous delegates, multicast delegates, signals and `dmq::util::Timer`, but no `dmq::os::Thread`, so no async delegates. The library's clock reads a millisecond tick that the application provides:

```cpp
extern "C" { volatile uint64_t g_ticks = 0; }

void loop()
{
    g_ticks = millis();
    dmq::util::Timer::ProcessTimers();   // if you use timers
}
```

### Desktop (native)

`platform = native` builds with the host GCC or Clang and uses `std::thread` (`DMQ_THREAD_STDLIB`), so async delegates work. On Windows it needs MinGW; with MSVC, use CMake instead (see [BUILD.md](../../docs/BUILD.md)).

## The Example Sketch

[`example/platformio/async-delegates/src/main.cpp`](async-delegates/src/main.cpp) is one sketch for every board:

- **All boards:** a synchronous delegate, a multicast-style `Signal` with a `ScopedConnection` that disconnects when it goes out of scope.
- **Boards with threads:** a worker `dmq::os::Thread` held by a `StartGate`, three `NORMAL` messages and one `URGENT`, then a blocking call (`dmq::WAIT_INFINITE`) that returns a value from the worker.

Its `platformio.ini` is the board matrix: `pio run` builds them all, `pio run -e <env>` builds one.

To start your own project from it, copy the folder and change `lib_deps = symlink://../../..` to `lib_deps = endurodave/DelegateMQ@^3.0.0` (the symlink points at this repository so that CI tests every commit).

## Troubleshooting

| Error | Cause and fix |
| :--- | :--- |
| `functional: No such file or directory` (or `chrono`, `cstdio`) | 8-bit AVR board (Uno, Mega). Not supported: no C++ standard library. |
| Crashes or wrong results in delegate calls, with RTTI on for some files only | Mixed RTTI settings: DelegateMQ's classes differ with and without it. Use one setting for every C++ file. |
| `if constexpr`, `std::optional`, structured-binding errors | Not C++17. Add the core's `-std=gnu++11`/`gnu++14` (some cores use `-std=c++11`/`c++14`) to `build_unflags` and `-std=gnu++17` to `build_flags`. |
| Link errors for `FreeRTOSThread` or other port symbols | `DelegateMQConfig.h` is in `src/`, so the library was built with different options. Move it to `include/`. |
| `FreeRTOS.h: No such file or directory` | `DMQ_THREAD_FREERTOS` on a core without FreeRTOS. Add an RTOS library (STM32duino FreeRTOS) or use `DMQ_THREAD_NONE`. |
| `macro "abs" passed 2 arguments` (Raspberry Pi Pico) | PlatformIO's default Pico core (Arduino mbed) breaks under C++17. Use the arduino-pico core: `platform = https://github.com/maxgerhardt/platform-raspberrypi.git` and `board_build.core = earlephilhower`. |
| Errors involving `HIGH` or `LOW` in your code | Arduino `#define`s these. DelegateMQ's priority is `dmq::Priority::URGENT` for this reason; avoid them as names in your own headers too. |
| Async delegates run in the wrong order, or each runs before the sender continues | RTOS task priorities: the worker outranks the sender. See [ESP32 (FreeRTOS)](#esp32-freertos) and use a `StartGate`. |

## Developing DelegateMQ with PlatformIO

When the example links DelegateMQ from the repository (`lib_deps = symlink://../../..`), PlatformIO's dependency finder searches the whole repository. Several sample projects carry their own FreeRTOS headers, so it concludes DelegateMQ provides `FreeRTOS.h` and doesn't give the library the STM32duino FreeRTOS include path. The `disco_f407vg` environment adds that path with [`freertos_path.py`](async-delegates/freertos_path.py). The registry package has no FreeRTOS copies, so projects using `lib_deps = endurodave/DelegateMQ` don't need it.

The package is described by [`library.json`](../../library.json) at the repository root: what is exported to the registry, the source and include directories, and the example's file list. To publish, tag the release, then pack from the repository root on a clean working tree with `python scripts/package_readme.py` and publish the tarball it makes: `pio pkg publish DelegateMQ-<version>.tar.gz --type library --no-interactive`. The registry shows the packaged README.md without the repository, so the script packs a copy whose relative links and images point to the release tag on GitHub; the README.md in the repository keeps its relative links.
