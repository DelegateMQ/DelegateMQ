# Pumptron — Pump Controller on Real Hardware

**Pumptron** is a two-CPU DelegateMQ demo: an **STM32F4 Discovery** board runs a simulated industrial pump controller on FreeRTOS, and a **Windows/Linux** operator console (FTXUI) monitors and commands it over a **serial link** (USART6: a 3.3 V USB-UART adapter, or RS-232 via a base board). Both sides communicate only through the **DataBus**.

It is Cellutron's hardware sibling. Cellutron simulates three CPUs on one PC; Pumptron runs the controller on a real microcontroller. The exact same controller code also runs on the FreeRTOS simulator over UDP, so you can develop without the board.

**What it showcases:**

- **DataBus over a serial link**: the controller and the console share named topics over one UART, with RELIABLE (ACK + retry) and UNRELIABLE tiers.
- **Real embedded target**: FreeRTOS on a Cortex-M4 with static task stacks, a fixed-block allocator and no exceptions.
- **Off-target development**: the same controller code runs on the FreeRTOS simulator on Windows/Linux, linked to the console over UDP instead of serial.
- **Multithreading with active objects**: every handler runs on the pump thread, so the state machine needs no locks.
- **Signals and error handling**: every DelegateMQ error and link-health signal is handled and reported, from delivery failures to dropped frames.
- **Heartbeats and safe-stop**: `DeadlineSubscription` in both directions; the pump stops if the operator disappears.
- **Watchdogs and crash dumps**: per-thread and hardware watchdogs; a crash is captured on the board and delivered to the console, which saves it as a text file ready to decode.
- **Terminal GUI**: an FTXUI operator console for Windows and Linux.

<img src="pumptron_gui.png" width="1000" alt="Pumptron operator console connected to the STM32F4 board over a serial link">

*Operator console connected to the STM32F4 board over COM3: the pump running at 2000 RPM, live sparklines, alarm events from shaking the board, and the bus monitor.*

---

## Quick Start

Pumptron builds and runs on **Windows** and **Linux**. Steps 1–3 run the whole system on the PC, with the controller on the FreeRTOS simulator. Steps 4–6 are optional: they move the controller onto a real STM32F4 Discovery board.

1.  **Initialize Workspace**: Ensure `DelegateMQ` is placed inside a workspace directory (e.g., `DelegateMQWorkspace`). From the repository root, fetch the dependencies (FreeRTOS, FTXUI, libserialport):
    ```bash
    python3 01_fetch_repos.py
    ```
2.  **Build Pumptron**: From this directory (`example/pumptron/`), build the GUI and the simulated controller:
    ```bash
    cmake -B build .
    cmake --build build
    ```
3.  **Run Pumptron**: Launch the controller and the GUI, each in its own terminal:
    ```bash
    python3 run_pumptron.py
    ```
    Press `s` to start the pump, `t` to stop it, `q` to quit (see [Controls](#controls)). If the simulated pump looks frozen, see the known issue under [Simulator](#simulator-no-board).
4.  **Build the firmware** *(optional, STM32F4 hardware)*: Needs `arm-none-eabi-gcc`, Ninja and the STM32Cube FW_F4 package (see [STM32F4 Firmware](#stm32f4-firmware)). From this directory:
    ```bash
    cmake -S controller/platform/f4 -B build/f4 -G Ninja
    cmake --build build/f4
    ```
5.  **Flash and wire the board** *(optional, STM32F4 hardware)*: Flash `build/f4/pumptron_f4.elf` with STM32CubeIDE or:
    ```bash
    STM32_Programmer_CLI -c port=SWD -w build/f4/pumptron_f4.elf -v -rst
    ```
    Connect a 3.3 V USB-UART adapter: adapter TX → **PC7**, RX → **PC6**, GND → **GND** (see [Hardware](#hardware)).
6.  **Run against the board** *(optional, STM32F4 hardware)*: Launch only the GUI, on the adapter's serial port:
    ```bash
    python3 run_pumptron.py --serial COM5          # Windows
    python3 run_pumptron.py --serial /dev/ttyUSB0  # Linux
    ```
    On Linux, `--serial` needs libserialport (see [Hardware](#hardware)).
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
- **Hold the blue button for 5 s** → deliberate test crash; see [Crash Dumps](#crash-dumps).
- **Unplug the serial cable** while running → the controller loses the GUI heartbeat and safe-stops the pump (`GUI LINK LOST`), and the GUI shows `OFFLINE`. Plug it back in and both sides resync automatically.

LEDs:

| LED | Meaning |
|:---|:---|
| **Orange** solid / blinking | Powered / link degraded (`LINK_DEGRADED`: delivery failures, dropped frames or a retry backlog in the last 10 s) |
| **Green** | Priming or running |
| **Blue** | Toggles on every telemetry frame |
| **Red** solid | Pump fault latched, or an initialization error (board halted) |
| **Red** fast blink for 3 s at startup | The previous run crashed; a core dump is stored and will be sent to the GUI |

---

## Architecture

![Pumptron Architecture](pumptron_architecture.svg)

| Topic | Direction | Reliability | Purpose |
|:---|:---|:---|:---|
| `pump/cmd` | GUI → F4 | RELIABLE (ACK + retry) | START / STOP / ESTOP / RESET / SET_SPEED / QUERY |
| `pump/status` | F4 → GUI | RELIABLE | State, latched fault, setpoint (on change and on QUERY) |
| `pump/alarm` | F4 → GUI | RELIABLE | Alarm raised / cleared |
| `pump/telemetry` | F4 → GUI | UNRELIABLE, 10 Hz | RPM, flow, pressure, temperatures, vibration |
| `sys/heartbeat/controller` | F4 → GUI | UNRELIABLE, 2 Hz | GUI `OFFLINE` detection (`DeadlineSubscription`) |
| `sys/heartbeat/gui` | GUI → F4 | UNRELIABLE, 2 Hz | Safe-stop when the operator disappears |

All of this wiring lives in one place, [`common/util/Topology.h`](common/util/Topology.h). It is written as templates, so the same functions configure the serial link (hardware) and the UDP `NetworkNode` (simulator).

---

## Why DelegateMQ

What this project would need without it, and what DelegateMQ replaces:

| Concern | Hand-written | With DelegateMQ |
|:---|:---|:---|
| Message protocol | Type enum, packed structs, `switch` decoder, kept byte-compatible on both sides by hand | Typed topics + registered serializers |
| Framing, CRC | UART byte state machine | Built into the transports |
| Reliability | Seq numbers, ACKs, retry timers, give-up path | `Reliability::RELIABLE` in `Topology.h` |
| Cross-task handoff | Queue-item unions, `xQueueSend`/`switch` per task | `MakeDelegate(obj, &Fn, thread)` |
| State machine locking | Mutexes around shared state | None: every handler runs on the pump thread |
| Link supervision | Timestamps checked in loops | `DeadlineSubscription` |
| Off-target development | Separate UDP code path, or develop on hardware only | Same controller code runs on the PC FreeRTOS simulator; only the link and board in `main.cpp` change |
| Testing | Hardware in the loop, or custom test harnesses | `--selftest` drives the real controller over DataBus. The same test runs against the simulator (no board) and the F4 (over serial) |
| Bus inspection | Custom logging | `DataBus::Monitor` → GUI bus monitor |

**Net effect:** application code is domain logic only. Link choice, direction and reliability of every topic live in about 20 lines (`Topology.h`). Adding a subscriber costs one line, not a protocol change.

---

## Directory Layout

```
pumptron/
├── CMakeLists.txt            Desktop build: gui + controller (FreeRTOS simulator)
├── common/                   Shared by every node
│   ├── messages/             PumpCommand/PumpStatus/Telemetry/Alarm/Heartbeat/CoreDump messages
│   └── util/                 Constants, Serializers, SerialLink, Topology
├── controller/
│   ├── board/IBoard.h        Hardware abstraction used by the pump application
│   ├── pump/                 PumpController (state machine) + PumpModel (physics)
│   ├── platform/sim/         FreeRTOS simulator main + SimBoard (UDP link)
│   └── platform/f4/          STM32F4 firmware: main, F4Board, CoreDump, HAL/FreeRTOS
│                             config, startup, linker script, toolchain, CMakeLists.txt
└── gui/
    ├── system/               Link selection (serial or UDP), heartbeat, link stats,
    │                         crash dump files
    ├── ui/                   FTXUI operator console
    └── selftest/             Headless end-to-end test (--selftest)
```

---

## Build Details

The commands are in [Quick Start](#quick-start); this section covers what they produce and how to change them.

### Desktop: GUI + Simulated Controller

Builds both the GUI and the controller to run on Windows or Linux, with the controller on the FreeRTOS simulator. No embedded hardware needed.

Output: `build/bin/<config>/pumptron_gui` and `pumptron_controller`. `run_pumptron.py` launches the Debug build by default; after a Release build (`cmake --build build --config Release`), pass it `--config Release`. On Windows, the top-level project forces a Win32 build because the FreeRTOS Windows simulator port is 32-bit only. For that reason libserialport is compiled from source into the GUI build.

### STM32F4 Firmware

Needs `arm-none-eabi-gcc`, Ninja, and the STM32Cube **FW_F4** package (HAL, CMSIS, Discovery BSP), which STM32CubeIDE/CubeMX install to `~/STM32Cube/Repository/`.

Output: `build/f4/pumptron_f4.elf`, `.hex`, `.bin`, `.map`.

Options, passed to the first `cmake` command as `-D<variable>=<value>`:

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

**What you need**

| Item | Notes |
|:---|:---|
| **STM32F407G-DISC1** Discovery kit | ST's current order code (Active); replaces the original STM32F4DISCOVERY. Both work: the BSP detects the LIS3DSH or older LIS302DL accelerometer at startup. |
| **3.3 V USB-UART adapter** (recommended) | Any FTDI, CP2102 or CH340 type adapter with 3.3 V logic levels. |
| *or* **STM32F4DIS-BB** base board + null-modem cable | Provides an RS-232 DB9 on USART6. Discontinued (Embest/element14); only use it if you already have one. |
| Mini-USB cable | Power plus the on-board ST-LINK for flashing and debugging. |

**Wiring (USB-UART adapter)**

| Adapter | Discovery board |
|:---|:---|
| TX | **PC7** (USART6 RX) |
| RX | **PC6** (USART6 TX) |
| GND | **GND** |

Cross TX to RX, leave the adapter's VCC unconnected (the board is powered over mini-USB), and **never** connect true RS-232 voltage levels directly to the pins. With the base board instead, mount the Discovery on it and connect its DB9 to the PC with a null-modem cable. Either way the link is 115200 8N1 and appears on the PC as a COM or tty port, so no firmware or GUI changes are needed.

**Run**

1. Flash `pumptron_f4.elf` (see *Flashing and Debugging*). The orange LED turns on and the blue LED starts blinking.
2. Run `python run_pumptron.py --serial <port>`, or start the GUI directly:
   ```bash
   pumptron_gui --serial COM5          # Windows
   pumptron_gui --serial /dev/ttyUSB0  # Linux
   ```
   Run `pumptron_gui` with no arguments to list the serial ports it can see. On Linux, `--serial` needs libserialport (`libserialport-dev`, or a build in the workspace's `libserialport/`); without it the GUI builds UDP-only.
3. Optionally check the link first with `python run_pumptron.py --serial <port> --selftest`.

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

It measures the controller's clock against wall time first and scales its timeouts to match, so a steady half-speed simulator still passes. It fails if the simulator clock stalls (see the known issue above). On the STM32F4 board over the serial link it passes every step, with the controller clock at 1.00× real time and no retries.

---

## Crash Dumps

Any crash on the F4 stores a small core dump in RAM that survives reset, then resets the board immediately. A crash here means any of:

- a `DMQ_ASSERT` or `BAD_ALLOC` (file and line)
- a hardware exception: HardFault, MemManage, BusFault or UsageFault
- a thread watchdog timeout (names the thread that stopped responding)
- a FreeRTOS `configASSERT`, stack overflow (names the task) or heap exhaustion

**What happens next**

1. The fault handler writes the crash record to a `.noinit` RAM section, which the startup code never clears, and calls `NVIC_SystemReset()`. It takes no locks, uses no heap and makes no `printf` calls, so it works even with a corrupted heap or stack.
2. On the next boot the red LED blinks fast for 3 s: a crash was detected.
3. Once the GUI connects (its first heartbeat arrives), the controller publishes a `CoreDumpMsg` on `sys/coredump` (RELIABLE).
4. The GUI writes `dump_YYYYMMDD_HHMMSS.txt` to its working directory and logs the file name in the Events pane. When started with `run_pumptron.py`, that directory is `build/bin/<config>/`.
5. The controller clears the stored record only after the GUI ACKs the message. If delivery fails, it resends on the next heartbeat.

**What a dump contains**

| Field | Contents |
|:---|:---|
| Type | Assertion, hardware exception, thread watchdog or FreeRTOS fault |
| File / Line, Exception, Unresponsive thread, Task | Depends on the type: where the fault was raised |
| Active context | What was running at the time of the fault: a task name, `ISR <n>`, or `main` before the scheduler started |
| Uptime at crash, Crash number | Time since boot when the fault hit, and a count of crashes since power-up. They make every crash distinct, so the GUI can tell a resend of the same dump (skipped, with an Events entry) from a new crash that happens to be identical, such as the button test |
| Build ID | GNU build ID of the firmware that crashed, so the dump can be matched to the exact `pumptron_f4.elf` |
| R0–R3, R12, LR, PC, xPSR | Hardware exceptions only: the registers the CPU stacked on entry |
| CFSR, HFSR, MMFAR, BFAR | Hardware exceptions only: fault status registers, with the set bits named (`UNDEFINSTR`, `PRECISERR`, …). MMFAR/BFAR are shown only when valid |
| Call stack | Up to 12 addresses, innermost first: the faulting PC (if in flash), then return addresses found by scanning the active stack. A word counts as a return address only if it directly follows a `BL`/`BLX` instruction |

**Try it:** hold the blue button for 5 s. `F4Board::IsLocalStopPressed()` executes an undefined instruction from the Pump thread, which raises a UsageFault.

**Sample dump** (from the button test):

```text
Pumptron controller core dump
Received: 2026-09-25 08:36:26
Type: Hardware exception
Exception: UsageFault (vector 6)
Active context: Pump
Build ID: 19adab3c919fca2dc3ff11

R0: 0x00001388
R1: 0x00000001
R2: 0x00000000
R3: 0x00001387
R12: 0x00000000
LR: 0x08015481
PC: 0x0801543c
xPSR: 0x21000000
CFSR: 0x00010000  UNDEFINSTR
HFSR: 0x00000000  -

Call stack (innermost first):
  0: 0x0801543d  (PC)
  1: 0x08015481
  2: 0x0801c337
  3: 0x080182eb
  4: 0x0801830f
  5: 0x08003aad
  6: 0x08003ab9
  7: 0x080073f5
  8: 0x08007425
  9: 0x0800f08b
  10: 0x080146a9
  11: 0x0801470f

Decode (with the pumptron_f4.elf whose build ID matches):
  arm-none-eabi-addr2line -f -C -p -e pumptron_f4.elf 0x0801543c 0x0801547f 0x0801c335 0x080182e9 0x0801830d 0x08003aab 0x08003ab7 0x080073f3 0x08007423 0x0800f089 0x080146a7 0x0801470d
```

**Decoding**

1. Check that the build ID matches the image you are decoding against:

   ```
   arm-none-eabi-readelf -n build/f4/pumptron_f4.elf
       Build ID: 19adab3c919fca2dc3ff11853dbff465d1404386
   ```

   The dump stores the first 22 hex digits. Keep the `.elf` of any firmware you hand out; without the matching image, the addresses cannot be decoded.

2. Run the `addr2line` command from the end of the dump. The GUI has already adjusted each address. The PC is used as is. Each return address becomes `(addr & ~1) - 1`, which clears the Thumb bit and steps back into the call instruction; decoding the address after the call can name the wrong line, or the next function if the callee never returns.

Output for the sample, with paths and template arguments shortened:

```text
pumptron::board::TriggerTestFault() at controller/platform/f4/F4Board.cpp:129
pumptron::board::F4Board::IsLocalStopPressed() at controller/platform/f4/F4Board.cpp:142
pumptron::pump::PumpController::OnControlTick() at controller/pump/PumpController.cpp:146
std::__invoke_impl<void, void (PumpController::*&)(), ...> at bits/invoke.h:74
std::__invoke<void (PumpController::*&)(), ...> at bits/invoke.h:96
dmq::UnicastDelegate<void ()>::operator()() const at delegate/UnicastDelegate.h:77
dmq::util::TimerDelegate::InvokeTarget(std::shared_ptr<dmq::util::DispatchToken>) at extras/util/TimerDelegate.h:174
std::__invoke_impl<void, void (TimerDelegate::*&)(...), ...> at bits/invoke.h:74
std::__invoke<void (TimerDelegate::*&)(...), ...> at bits/invoke.h:96
dmq::DelegateMemberAsync<TimerDelegate, void (std::shared_ptr<DispatchToken>)>::operator()(...) at delegate/DelegateAsync.h:633
std::__invoke_impl<void, void (DelegateMember<TimerDelegate, ...>::*)(...), ...> at bits/invoke.h:74
std::__invoke<void (DelegateMember<TimerDelegate, ...>::*)(...), ...> at bits/invoke.h:96
```

Read it from the bottom up. The Pump thread's control timer fired, and DelegateMQ dispatched it asynchronously to the Pump thread (`DelegateMemberAsync` → `TimerDelegate::InvokeTarget`). The timer called `PumpController::OnControlTick()`, which polled the board button, and `IsLocalStopPressed()` called `TriggerTestFault()`. CFSR `UNDEFINSTR` confirms the cause: an undefined instruction at the PC. The 12-entry limit cut off the outermost frames (the Pump thread's message loop).

**Reading a hardware exception.** CFSR usually names the cause directly. Common ones:

| CFSR bit | Meaning |
|:---|:---|
| `PRECISERR` + `BFARVALID` | Bad data address; BFAR holds it (for example a null or dangling pointer) |
| `DACCVIOL` + `MMARVALID` | MPU/data access violation at MMFAR |
| `UNDEFINSTR` / `INVSTATE` | Executed garbage or an ARM-mode address: a corrupted function pointer or return address |
| `DIVBYZERO`, `UNALIGNED` | Only when trapping is enabled; the CubeIDE debug launch can enable it (Startup tab, exception settings) |
| `STKERR` / `MSTKERR` | Fault while stacking: usually a stack overflow |

HFSR `FORCED` means a lower-priority fault escalated to HardFault; CFSR still has the original cause. HFSR `DEBUGEVT` with a PC in RAM (`0x2000xxxx`) is not an application crash: the debugger's flash loader hit a breakpoint without a debugger attached, typically during an interrupted flash or debug launch.

The implementation is in `controller/platform/f4/CoreDump.cpp`/`CoreDumpReporter.h` and `gui/system/CoreDumpFile.cpp`. It is adapted from [CoreDump](https://github.com/endurodave/CoreDump), which explains the technique in detail.

**Limitations**

- For a thread watchdog timeout, the call stack is the watchdog checker's (the `Startup` task), not the stuck thread's. The dump does name the stuck thread.
- Only the active thread's stack is captured, not every FreeRTOS task's.
- The stack scan is heuristic. The `BL`/`BLX` check rejects nearly all non-addresses, but a stale return address left behind by an earlier call can still appear.
- Only the first crash is kept until it has been delivered; a second crash before then is not recorded.
- A power cycle loses an undelivered dump. RAM survives a reset, not a power loss.
- A hang that the thread watchdog does not catch ends in an IWDG reset with no dump. The boot log reports `restarted by hardware watchdog`.

**Future expansion: call stacks for all FreeRTOS tasks**

Today only the active thread's stack is captured. Capturing every task would show the stuck thread's own stack in a thread watchdog dump, and where every other task was blocked at the time of a crash, which is what diagnosing a deadlock or starvation needs. The outline:

1. **Enumerate the tasks from the fault handler.** `uxTaskGetSystemState()`/`vTaskGetInfo()` are not usable there: they suspend the scheduler and enter critical sections, which assert from handler mode on the CM4F port. Instead, set `configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H 1` and add a `freertos_tasks_c_additions.h`. FreeRTOS compiles that header into `tasks.c`, where it can walk the ready, delayed, pending-ready, suspended and waiting-termination lists and read TCB fields at their correct offsets. Set `configRECORD_STACK_HIGH_ADDRESS 1` too, so each TCB records `pxEndOfStack` and each stack scan can stop at that task's exact bounds.
2. **Recover each blocked task's context.** `pxTopOfStack`, which FreeRTOS guarantees is the first TCB member, points at the context saved by PendSV (`GCC/ARM_CM4F/port.c`): r4–r11 and EXC_RETURN, then s16–s31 if EXC_RETURN bit 4 is clear, then the hardware frame (r0–r3, r12, LR, PC, xPSR, plus the FP words). That gives each task's PC (where it was switched out, usually inside a kernel call such as `xQueueReceive`) and its LR. The rest of its stack, up to `pxEndOfStack`, is scanned with the same `BL`/`BLX` filter.
3. **Special cases.** The running task keeps today's live capture, since its saved context is stale. For a fault inside an ISR, the interrupted task's live frame is on the PSP and could be captured from there too, closing today's gap where an ISR fault shows only the main stack. Before the scheduler starts there are no tasks.
4. **Cheap extras per task.** State (Running/Ready/Blocked/Suspended/Deleted, from the list it is on), priority, and the stack high-water mark (untouched `0xA5` fill words from the stack base), which flags a task close to overflowing.
5. **Size.** There are 6 tasks today (Startup, Pump, LinkTx, LinkRx, Tmr Svc, IDLE). At about 80 bytes per task (name, state, high-water mark, 12 addresses), a cap of 8 tasks is about 640 bytes of `.noinit` RAM and about 1 KB on the wire. That still fits in one `CoreDumpMsg`: the F4 send path has no fixed cap, and the GUI's serial receive buffer is 4096 bytes. One message keeps "clear only after the GUI's ACK" simple.
6. **Robustness.** Kernel lists may be corrupt after a crash, and a bad pointer read inside the fault handler locks up the CPU. The IWDG then resets the board and the whole dump is lost. So commit the active-thread record first, with a valid CRC, and then append the task section and recompute the CRC. Check every pointer before reading it (in RAM, aligned), cap every list walk, and check that each list item points back to the list being walked.
7. **Coupling.** The additions header depends on `tasks.c` internals (`pxReadyTasksLists` and so on). Pin it to the kernel version in use (V11.1.0); a kernel upgrade that renames them fails to compile rather than misbehaving.

Changes, all on the F4: `FreeRTOSConfig.h` (the two options), a new `freertos_tasks_c_additions.h` (task walk and context recovery), `CoreDump.cpp` (a tasks section and the two-phase commit), `CoreDumpMsg.h` (a task array), and `gui/system/CoreDumpFile.cpp` (one section and one `addr2line` command per task).

---

## Porting Notes

- **New board.** Implement `board::IBoard` and write a `platform/<board>/main.cpp` that creates the link and calls `ConfigureControllerLink()`. Nothing in `pump/` or `common/` changes.
- **New link.** Any type with NetworkNode-style `Send<T>()`/`Receive<T>()` works with `Topology.h`.
- **UART error recovery.** `Stm32UartTransport` does not restart interrupt-driven reception after a UART error (overrun, framing, noise). `platform/f4/main.cpp` re-arms it from `HAL_UART_ErrorCallback()`. The frame CRC rejects any frame corrupted by the error.
