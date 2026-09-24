# Pumptron — Pump Controller on Real Hardware

**Pumptron** is a two-CPU DelegateMQ demo: an **STM32F4 Discovery** board runs a simulated industrial pump controller on FreeRTOS, and a **Windows/Linux** operator console (FTXUI) monitors and commands it over **RS-232**. Both sides communicate only through the **DataBus**.

It is Cellutron's hardware sibling. Cellutron simulates three CPUs on one PC; Pumptron runs the controller on a real microcontroller. The exact same controller code also runs on the FreeRTOS simulator over UDP, so you can develop without the board.

---

## What It Does

The F4 runs a centrifugal pump + motor model driven by a state machine:

```
IDLE --START--> PRIMING --(3 s)--> RUNNING
PRIMING/RUNNING --STOP or GUI link lost--> STOPPING --(stopped)--> IDLE
any --E-STOP / board button / over-temp / vibration trip--> FAULT (latched)
FAULT --RESET (stopped, cooled, button released)--> IDLE
```

| Signal | Source on the F4 | Source on the simulator |
|:---|:---|:---|
| Speed, flow, pressure, motor temperature | Pump model | Pump model |
| Ambient temperature | **MCU die temperature sensor** (ADC1) | Fixed 25 °C |
| Vibration | Model + **LIS3DSH accelerometer** (shake the board!) | Model only |
| Local E-STOP | **Blue USER button** | — |
| Status indicators | **LD3–LD6 LEDs** | Console prints |

Demo scenarios built into the model:

- **Shake the board** while running → vibration warning, then a `VIBRATION TRIP` fault if sustained for 300 ms.
- **Run at 3000 RPM** for about a minute → motor temperature climbs past 70 °C (warning), then trips `OVER-TEMP` at 85 °C. `RESET` is refused until it cools below 60 °C.
- **Sweep through ~2200 RPM** → a mild structural resonance is visible in the vibration trace.
- **Press the blue button** → local E-STOP.
- **Unplug the serial cable** while running → the controller loses the GUI heartbeat and safe-stops the pump (`GUI LINK LOST`), and the GUI shows `OFFLINE`. Plug it back in and both sides resync automatically.

LEDs: **orange** = powered, **green** = priming/running, **red** = fault, **blue** = toggles on every telemetry frame.

---

## Architecture

```
 ┌──────────────────────── STM32F4 Discovery (FreeRTOS) ───────────────────────┐        ┌──────────── PC (Windows/Linux) ────────────┐
 │  PumpController (active object, 1 thread)                                   │        │  UI (FTXUI)        System                  │
 │    ├─ PumpModel         ├─ IBoard → F4Board (LIS3DSH, ADC, button, LEDs)   │        │    │                 └─ GUI heartbeat      │
 │    └─ DataBus Publish/Subscribe only                                        │        │    └─ DataBus Publish/Subscribe only       │
 │                         │                                                   │        │                         │                  │
 │  SerialLink<Stm32UartTransport>  ── USART6 / RS-232, 115200 8N1 ──────────────────────  SerialLink<SerialTransport> (libserialport)│
 └─────────────────────────────────────────────────────────────────────────────┘        └────────────────────────────────────────────┘
```

| Topic | Direction | Reliability | Purpose |
|:---|:---|:---|:---|
| `pump/cmd` | GUI → F4 | RELIABLE (ACK + retry) | START / STOP / ESTOP / RESET / SET_SPEED / QUERY |
| `pump/status` | F4 → GUI | RELIABLE | State, latched fault, setpoint (on change and on QUERY) |
| `pump/alarm` | F4 → GUI | RELIABLE | Alarm raised / cleared |
| `pump/telemetry` | F4 → GUI | UNRELIABLE, 10 Hz | RPM, flow, pressure, temperatures, vibration |
| `sys/heartbeat/controller` | F4 → GUI | UNRELIABLE, 2 Hz | GUI `OFFLINE` detection (`DeadlineSubscription`) |
| `sys/heartbeat/gui` | GUI → F4 | UNRELIABLE, 2 Hz | Safe-stop when the operator disappears |

All of this wiring lives in one place, [`common/util/Topology.h`](common/util/Topology.h). It is written as templates, so the same functions configure the serial link (hardware) and the UDP `NetworkNode` (simulator).

### DelegateMQ Features Shown

- **Location transparency.** `PumpController` and `UI` only call `DataBus::Publish/Subscribe`. Neither knows whether the other end is across RS-232, UDP, or absent.
- **DataBus over a serial link.** [`common/util/SerialLink.h`](common/util/SerialLink.h) is a point-to-point counterpart to `NetworkNode`. It has the same `Send<T>()`/`Receive<T>()` API and status signals, with RELIABLE and UNRELIABLE tiers sharing one UART.
- **Active objects.** Every handler (commands, 20 Hz control tick, heartbeat, link-loss deadline) is marshalled onto the pump thread, so the state machine needs no locks.
- **`DeadlineSubscription` heartbeats**, in both directions.
- **Explicit queue policies.** The link send thread uses `DROP`: a stalled cable drops frames instead of faulting the node, and drops are counted and shown in the GUI header. The pump thread uses `FAULT`, and the UI thread uses `DROP`.
- **Embedded-friendly build.** Static task stacks, a 64 KB FreeRTOS `heap_4` in CCM RAM, `DMQ_ALLOCATOR` fixed-block allocation, `DMQ_ASSERTS`, and no exceptions. The build uses about 333 KB of flash and 41 KB of SRAM.
- **Off-target development.** The controller's application code (`pump/`, `board/IBoard.h`, `common/`) is identical on the F4 and on the FreeRTOS simulator. Only `platform/f4/` or `platform/sim/` differs.

---

## Directory Layout

```
pumptron/
├── CMakeLists.txt            Desktop build: gui + controller (FreeRTOS simulator)
├── common/                   Shared by every node
│   ├── messages/             PumpCommand/PumpStatus/Telemetry/Alarm/Heartbeat messages
│   └── util/                 Constants, Serializers, SerialLink, Topology
├── controller/
│   ├── board/IBoard.h        Hardware abstraction used by the pump application
│   ├── pump/                 PumpController (state machine) + PumpModel (physics)
│   ├── platform/sim/         FreeRTOS simulator main + SimBoard (UDP link)
│   └── platform/f4/          STM32F4 firmware: main, F4Board, HAL/FreeRTOS config,
│                             startup, linker script, toolchain file, CMakeLists.txt
└── gui/
    ├── system/               Link selection (serial or UDP), heartbeat, link stats
    ├── ui/                   FTXUI operator console
    └── selftest/             Headless end-to-end test (--selftest)
```

---

## Building

Prerequisites: the DelegateMQ workspace with `FreeRTOS`, `ftxui` and `libserialport` fetched next to `DelegateMQ` (`python3 01_fetch_repos.py`).

### Desktop: GUI + Simulated Controller

From `example/pumptron`:

```bash
cmake -B build .
cmake --build build --config Release
```

Output: `build/bin/<config>/pumptron_gui` and `pumptron_controller`. On Windows, the top-level project forces a Win32 build because the FreeRTOS Windows simulator port is 32-bit only. For that reason libserialport is compiled from source into the GUI build.

### STM32F4 Firmware

Needs `arm-none-eabi-gcc`, Ninja, and the STM32Cube **FW_F4** package (HAL, CMSIS, Discovery BSP), which STM32CubeIDE/CubeMX install to `~/STM32Cube/Repository/`.

From `example/pumptron`:

```bash
cmake -S controller/platform/f4 -B build/f4 -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/f4
```

Output: `build/f4/pumptron_f4.elf`, `.hex`, `.bin`, `.map`.

Options:

| Variable | Default | Purpose |
|:---|:---|:---|
| `STM32_REPO` | `~/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3` | STM32Cube FW_F4 package |
| `ARM_TOOLCHAIN_DIR` | STM32CubeIDE's bundled GCC if found, else `PATH` | Directory containing `arm-none-eabi-gcc` |
| `CMAKE_BUILD_TYPE` | `Debug` (`-Og -g3`) | `Release` = `-Os -g` |

The toolchain file prefers STM32CubeIDE's own "GNU Tools for STM32" GCC, so command-line builds use the same compiler as the IDE.

### Flashing and Debugging

Any of these work with the `.elf`:

- **STM32CubeIDE:** *File → Import → C/C++ → STM32 Cortex-M Executable*, select `build/f4/pumptron_f4.elf`, then Debug. To see `printf` output, enable SWV (core clock **168 MHz**) and open the *SWV ITM Data Console* on port 0.
- **STM32CubeProgrammer:** `STM32_Programmer_CLI -c port=SWD -w build/f4/pumptron_f4.elf -v -rst`
- **OpenOCD:** `openocd -f board/stm32f4discovery.cfg -c "program build/f4/pumptron_f4.elf verify reset exit"`

---

## Running

### `run_pumptron.py`

```bash
python run_pumptron.py                         # simulation: controller + GUI, each in its own terminal
python run_pumptron.py --serial COM5           # hardware: GUI only, talking to the flashed board
python run_pumptron.py --selftest              # simulation, headless end-to-end check
python run_pumptron.py --serial COM5 --selftest
```

Options: `--baud <rate>` (default 115200) and `--config Debug|Release` (default Debug).

### Hardware

1. Mount the Discovery board on the **STM32F4DIS-BB** base board and connect its RS-232 DB9 (USART6) to the PC with a **null-modem** cable (or USB-RS232 adapter).
2. Flash `pumptron_f4.elf`. The orange LED turns on and the blue LED starts blinking.
3. Run `python run_pumptron.py --serial <port>`, or start the GUI directly:
   ```bash
   pumptron_gui --serial COM5          # Windows
   pumptron_gui --serial /dev/ttyUSB0  # Linux
   ```
   Run `pumptron_gui` with no arguments to list the serial ports it can see. On Linux, `--serial` needs libserialport (`libserialport-dev`, or a build in the workspace's `libserialport/`); without it the GUI builds UDP-only.

### Simulator (No Board)

Run `python run_pumptron.py`, or start `pumptron_controller` and `pumptron_gui --udp` in separate terminals.

> **Known issue: the FreeRTOS Windows simulator's clock is unreliable.** The Win32 port simulates the tick with `Sleep(1)` and suspends task threads with `SuspendThread`, and the controller's tasks make real Windows calls (UDP sockets, console output), which the FreeRTOS Win32 port documents as unsafe. Measured effects:
> - In good runs, simulated time runs at a steady **~0.5× real time**. Priming, ramps and heartbeats look about 2× slower than on the board.
> - In some runs, in both Debug and Release, the simulated tick slows to **~0.1× or nearly stops**. Run-time stats from one stalled run pointed at the timer-daemon task. The pump then appears stuck in PRIMING and the self-test fails.
>
> The root cause has not been found yet. If a simulator run looks frozen, restart the controller. Console output is serialized through a FreeRTOS mutex (`controller/util/Logger.h`, the same approach as Cellutron); that removed one stall but not all of them. The STM32F4 hardware target does not use this port.

### Controls

| Key | Action |
|:---|:---|
| `s` / `t` | START / STOP |
| `e` | E-STOP |
| `r` | RESET a latched fault |
| `+` / `-` | Setpoint ±100 RPM (or drag the slider) |
| `q` / `Esc` | Quit |

The on-screen buttons can also be used with Tab/arrow keys and Enter, or with the mouse.

### Self-Test

`--selftest` replaces the UI with a scripted operator. It drives start, speed changes, stop, E-STOP and reset, and simulates a lost GUI heartbeat, then verifies every status, telemetry and alarm reply. It exits 0 on success.

```bash
python run_pumptron.py --selftest                 # simulator
python run_pumptron.py --serial COM5 --selftest   # real board
```

It measures the controller's clock against wall time first and scales its timeouts to match, so a steady half-speed simulator still passes. It fails if the simulator clock stalls (see the known issue above).

---

## Porting Notes

- **New board.** Implement `board::IBoard` and write a `platform/<board>/main.cpp` that creates the link and calls `ConfigureControllerLink()`. Nothing in `pump/` or `common/` changes.
- **New link.** Any type with NetworkNode-style `Send<T>()`/`Receive<T>()` works with `Topology.h`.
- **UART error recovery.** `Stm32UartTransport` does not restart interrupt-driven reception after a UART error (overrun, framing, noise). `platform/f4/main.cpp` re-arms it from `HAL_UART_ErrorCallback()`. The frame CRC rejects any frame corrupted by the error.
