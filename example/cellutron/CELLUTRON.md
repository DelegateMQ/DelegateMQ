# Cellutron — Cell Processing System

**Cellutron** (located in `DelegateMQ/example/cellutron`) is a comprehensive demonstration project representing a hypothetical **medical, safety-critical instrument**. It showcases how **DelegateMQ** enables the design of distributed systems that require high reliability, independent hardware interlocks, and rigorous audit trails.

**What it showcases:**

- **Distributed DataBus**: three nodes (GUI, controller and safety CPUs) share named topics, so no node knows where a publisher or subscriber lives.
- **Remote communication over UDP**: RELIABLE topics (ACK + automatic retry) and UNRELIABLE, low-latency topics share one socket per node.
- **Multithreading with active objects**: each subsystem owns its thread, and asynchronous delegates pass data between threads, so the state machines need no locks.
- **Signals and slots**: `dmq::Signal` with RAII `ScopedConnection` for events inside each node.
- **RTOS portability**: the controller and safety nodes run on the FreeRTOS or ThreadX simulator, the GUI on Windows/Linux, and the application source is identical across them.
- **Health monitoring**: cross-node heartbeats (`DeadlineSubscription`) trigger a coordinated system-wide fault; a Last Value Cache gives late joiners the current state; bus monitoring feeds audit logs.
- **Terminal GUI**: an FTXUI operator console.

<img src="cellutron.png" width="1280">

---

## Quick Start

The Cellutron system is cross-platform and supports both **Windows** and **Linux**. On both platforms, the Controller and Safety nodes operate within an RTOS simulation to emulate embedded hardware behavior — **FreeRTOS** by default, or **ThreadX** with a single build switch (Linux only). See [RTOS Portability](#rtos-portability-freertos--threadx) below.

1.  **Initialize Workspace**: Ensure `DelegateMQ` is placed inside a workspace directory (e.g., `DelegateMQWorkspace`). From the repository root, run the setup scripts to fetch dependencies and build tools:
    ```bash
    python3 01_fetch_repos.py
    python3 02_build_libs.py
    python3 03_generate_samples.py
    ```
2.  **Build Cellutron**: From this directory (`example/cellutron/`), run:
    ```bash
    cmake -B build .
    cmake --build build --config Release
    ```
    To build the ThreadX variant instead (Linux only), add `-DCELLUTRON_RTOS=THREADX`:
    ```bash
    cmake -B build-threadx -DCELLUTRON_RTOS=THREADX .
    cmake --build build-threadx --config Release
    ```
3.  **Run Cellutron**: Launch all three processors and spy tools:
    ```bash
    python3 run_cellutron.py
    ```
    Or, to launch the ThreadX build:
    ```bash
    python3 run_cellutron.py --threadx
    ```
    *Note: On Linux, ensure you have a terminal emulator like `gnome-terminal`, `xterm`, or `konsole` installed for multi-window simulation support.*

---

## Why Cellutron?

The Cellutron project serves as a "Real-World" demonstration of DelegateMQ in a multi-processor, safety-critical context. It showcases how DelegateMQ middleware runs on top of Windows/Linux, FreeRTOS, and ThreadX using async signals/slots, DataBus, and multithreading to solve the challenges inherent in modern instrument engineering.

It is also a concrete proof point for DelegateMQ's central design claim: **application code is isolated from the underlying OS/RTOS.** The Controller and Safety nodes' `Process`, `System`, `Actuators`, `Sensors`, and state-machine sources are byte-for-byte identical whether the node runs on FreeRTOS or ThreadX — flipping `CELLUTRON_RTOS` changes only a thin, clearly-delimited kernel-init layer (`main.cpp` vs. `main_threadx.cpp`) and one build define (`DMQ_THREAD`). See [RTOS Portability](#rtos-portability-freertos--threadx).

---

## RTOS Portability: FreeRTOS ↔ ThreadX

The Controller and Safety nodes build against either **FreeRTOS** (default) or **ThreadX** (Linux only, via the vendored `threadx` sibling repo fetched by `01_fetch_repos.py`), selected with one CMake switch:

```bash
cmake -B build .                                  # FreeRTOS (default)
cmake -B build-threadx -DCELLUTRON_RTOS=THREADX . # ThreadX
```

`run_cellutron.py --threadx` launches the ThreadX build instead of the default FreeRTOS one; both nodes always move together — there is no mixed-RTOS configuration.

**What actually changes when you flip the switch:**

| Layer | FreeRTOS | ThreadX | Shared? |
|:---|:---|:---|:---|
| `DMQ_THREAD` build define | `DMQ_THREAD_FREERTOS` | `DMQ_THREAD_THREADX` | No — this is the entire switch |
| Kernel-init / task-creation glue | `main.cpp` (`xTaskCreate`, `xTimerCreate`, `vTaskStartScheduler`) | `main_threadx.cpp` (`tx_thread_create`, `tx_timer_create`, `tx_kernel_enter`) | No — RTOS-specific by nature, isolated to exactly this one file per node |
| Thread priority scale | `PRIORITY_*` in `Constants.h`, higher = more urgent | `PRIORITY_*` in `Constants.h`, lower = more urgent (`#ifdef DMQ_THREAD_THREADX`) | No — FreeRTOS and ThreadX use opposite priority conventions; each branch encodes the same *relative* ordering in its own scale |
| `Process`, `System`, `Actuators`, `Sensors`, state machines | identical | identical | **Yes — zero changes** |
| DataBus, Signals, Delegates, `dmq::os::Thread`, Timer, Heartbeat, Network | identical | identical | **Yes — zero changes** |

That last row is the point of the demo: every line of application logic — process sequencing, hardware I/O, fault handling, the DataBus wiring, all of it — is unaware of which RTOS it's running on. DelegateMQ's `dmq::os::Thread`/`dmq::Mutex`/`dmq::Clock`/`dmq::util::Timer` abstractions (see the library's own `DelegateOpt.h`) absorb the RTOS difference; only the handful of files above that talk directly to the kernel API even know which RTOS is in play.

The one place this isolation isn't perfectly seamless is thread priority: FreeRTOS and ThreadX number their priority levels in opposite directions (FreeRTOS: higher number = more urgent; ThreadX: 0 = highest urgency). `Constants.h` handles this with a small `#ifdef DMQ_THREAD_THREADX` block so both builds preserve the same *relative* urgency ordering (Network/System threads most urgent, Hardware/Process next, Low least) — this is the one spot in the app where the underlying RTOS's convention leaks through, and it's deliberately localized to one file rather than threaded through the app.

### Known Limitation: ThreadX Build May Deadlock on the Linux Simulator

The vendored **ThreadX Linux/GNU simulation port** (the desktop dev/test scaffolding used for `CELLUTRON_RTOS=THREADX` here — not real ThreadX kernel scheduling) has a genuine, root-caused kernel deadlock triggered whenever **two or more ThreadX application threads are concurrently alive**: thread startup hangs forever inside ThreadX's own `_tx_thread_interrupt_control()` on a global `_tx_linux_mutex`. This is documented in detail in `example/sample-projects/threadx-linux/DelegateThreadsTests.cpp`, whose own test suite has to keep 4 of its 10 tests disabled for exactly this reason.

The Controller node alone runs 7 concurrent ThreadX threads (Controller Task, Watchdog Task, `Controller_SystemThread`, `Controller_NetworkThread`, `ProcessThread`, `ActuatorsThread`, `SensorsThread`), so the ThreadX build of Cellutron **will typically hang** partway through a run — symptomatically, as repeated `Queue post timed out ... possible deadlock` warnings on one of the worker threads (e.g. `ActuatorsThread`) once enough threads are active together, most often during the centrifuge spin-up sequence.

This is a bug in vendored ThreadX's own Linux-simulation port, not in DelegateMQ or in any Cellutron application code — the project's policy is to keep that vendored tree unpatched (see `External.cmake`), and a real embedded ThreadX target (actual hardware or an RTOS-accurate simulator, not this pthread-based Linux dev port) would not be expected to hit it. Treat the ThreadX build here as a correctness/portability proof for the *application code* (it compiles, links, and starts identically to the FreeRTOS build with zero source changes) rather than as a build you can run end-to-end reliably on this simulator today.

---

## Architecture Overview

### Hardware Topology
![Cellutron Architecture](cellutron_architecture.svg)

The system is distributed across three independent processors (CPUs) communicating over a single UDP-based distributed **DataBus** (`NetworkNode<Win32UdpTransport>`/`NetworkNode<LinuxUdpTransport>`, see `NetworkTypes.h`) with per-topic reliability tiers — not two separate transports.

- **RELIABLE (Guaranteed)**: Used for critical control commands (Start, Abort) and Fault events to ensure zero data loss — `NetworkNode::Send()` passed `Rel::RELIABLE`, which adds ACK + automatic retry via `RetryMonitor` on top of UDP.
- **UNRELIABLE (Low Latency)**: Used for high-frequency telemetry (RPM, Sensors) and Heartbeats where speed is prioritized over reliability — plain fire-and-forget UDP, the `Send()` default.

| CPU Node | Operating System | Primary Responsibility |
|:---|:---|:---|
| **GUI CPU** | Windows/Linux (stdlib) | HMI, Data Visualization, and System-wide Logging. |
| **Controller CPU** | FreeRTOS (Win32/POSIX Sim), or ThreadX (Linux Sim) | Process orchestration and hardware sequencing. |
| **Safety CPU** | FreeRTOS (Win32/POSIX Sim), or ThreadX (Linux Sim) | Independent hardware monitoring and interlock enforcement. |

Controller/Safety RTOS is a single build-time choice (`CELLUTRON_RTOS=FREERTOS` default, or `THREADX`) applied identically to both nodes — see [RTOS Portability](#rtos-portability-freertos--threadx).

### Thread Topology

Every CPU uses a standardized thread architecture for network I/O and Active Object dispatch. Most `dmq::os::Thread` instances are protected by the **DelegateMQ Watchdog** (30-second timeout); exceptions are noted below. The raw OS tasks (Controller/Safety) are RTOS-managed and listed separately — they exist twice in source (`main.cpp` for FreeRTOS, `main_threadx.cpp` for ThreadX) since kernel-init/task-creation glue is inherently RTOS-specific; everything below that layer in these tables (`*_SystemThread`, `*_NetworkThread`, `ProcessThread`, `ActuatorsThread`, `SensorsThread`) is a single, RTOS-agnostic `dmq::os::Thread` shared by both builds.

#### GUI CPU
| Thread | Role | Description |
|:---|:---|:---|
| **Main Thread** | UI Event Loop | Runs FTXUI `screen.Loop()` — handles keyboard/mouse input and terminal rendering. |
| **TickThread** | System Tick | Calls `System::Tick()` every 50 ms from `main()` while the UI loop blocks. No watchdog. |
| **Watchdog** | Watchdog Loop | Calls `Thread::WatchdogCheckAll()` every 100 ms to detect and report deadlocks. |
| **GUI_SystemThread** | Active Object SM | Runs system-level logic, coordinates heartbeat, and dispatches DataBus callbacks. |
| **GUI_TimerThread** | Timer Dispatch | Drives `Timer::ProcessTimers()` and the 100 ms system tick. No watchdog. |
| **GUI_NetworkThread** | Network Poller | Polls the UDP `Network` socket, carrying both UNRELIABLE telemetry and RELIABLE (ACK + retry) command/fault topics; owned by the `Network` singleton. |
| **UIThread** | DataBus Callbacks | Receives DataBus subscription callbacks and posts FTXUI screen refresh events. |
| **AlarmsThread** | Alarm Monitor | Processes fault events and `DeadlineSubscription` watchdog callbacks. |
| **LogsThread** | File I/O | Writes the `logs.txt` audit trail. Uses a 20-second watchdog timeout. |

#### Controller CPU
| Thread | Role | Description |
|:---|:---|:---|
| **Controller Task** | OS Orchestration | FreeRTOS task `vControllerTask` (`main.cpp`), or ThreadX thread `ControllerThreadEntry` (`main_threadx.cpp`); calls `System::Initialize()` then ticks at 100 ms. |
| **SysTimer** | Timer Dispatch | FreeRTOS software timer, or ThreadX `TX_TIMER` (10 ms period either way); calls `Timer::ProcessTimers()`. |
| **Watchdog Task** | OS Watchdog | FreeRTOS task `vWatchdogTask`, or ThreadX thread `WatchdogThreadEntry`; calls `Thread::WatchdogCheckAll()` every 100 ms. |
| **Controller_SystemThread** | Active Object SM | Runs system-level logic and dispatches DataBus callbacks (start/stop/fault). |
| **Controller_NetworkThread** | Network Poller | Listens for RELIABLE (ACK + retry) commands and broadcasts UNRELIABLE telemetry, both over the same UDP socket; owned by `Network` singleton. |
| **ProcessThread** | Process State Machine | Runs `CellProcess` and `PumpProcess` state machines for instrument sequencing. |
| **ActuatorsThread** | Hardware Output | Executes valve and pump commands via blocking synchronous calls. |
| **SensorsThread** | Hardware Input | Queries pressure and air-in-line sensors. |

#### Safety CPU
| Thread | Role | Description |
|:---|:---|:---|
| **Safety Task** | OS Orchestration | FreeRTOS task `vSafetyTask` (`main.cpp`), or ThreadX thread `SafetyThreadEntry` (`main_threadx.cpp`); calls `System::Initialize()` then ticks at 100 ms. |
| **SysTimer** | Timer Dispatch | FreeRTOS software timer, or ThreadX `TX_TIMER` (10 ms period either way); calls `Timer::ProcessTimers()`. |
| **Watchdog Task** | OS Watchdog | FreeRTOS task `vWatchdogTask`, or ThreadX thread `WatchdogThreadEntry`; calls `Thread::WatchdogCheckAll()` every 100 ms. |
| **Safety_SystemThread** | Active Object SM | Monitors centrifuge speed and publishes fault events RELIABLE (ACK + retry) when limits are exceeded. |
| **Safety_NetworkThread** | Network Poller | Listens for RPM telemetry (UNRELIABLE) and command updates (RELIABLE), both over the same UDP socket; owned by `Network` singleton. |

### Pneumatics System
![Cellutron Pneumatics](pneumatics.svg)

The instrument controls a fluid path using a manifold of valves and a single peristaltic pump:
- **Valves V1-V3**: Divert fluids (Solution A, B, or Cells) into the main process line.
- **Pump (ID 1)**: Precise bi-directional flow control (Forward for filling, Reverse for draining).
- **Centrifuge**: High-speed separation chamber for cell processing.
- **Valve V4**: Controls the final drain path to waste.
- **Sensors**: Real-time pressure (P) and air-in-line (A) monitoring at the inlet and outlet of the pump manifold.

---

## Communication Model (Distributed DataBus)

Cellutron uses a "DDS-Lite" approach where data is exchanged via named **Topics**. The DataBus abstracts the network, allowing publishers and subscribers to interact seamlessly across CPU boundaries.

### Topic Mapping

| Topic | Publisher | Subscribers | Description |
|:---|:---|:---|:---|
| `Command` | GUI CPU | Controller CPU | Commands to start or abort the cell processing sequence. |
| `Status` | Controller CPU | GUI CPU | High-level system state (Idle, Processing, Aborting, Fault). |
| `Control` | Controller CPU | GUI CPU, Safety CPU | Real-time centrifuge RPM setpoints. |
| `Hardware`| Controller CPU | GUI CPU (logs) | Feedback on valve toggles, pump speed, and sensor snapshots. |
| `Fault` | Safety CPU | Controller CPU, GUI CPU | Critical safety violation event. |

---

## Controller State Machines

The Controller CPU orchestration is driven by nested state machines. The high-level process manages the overall sequence, while sub-processes handle atomic hardware actions.

### CellProcess State Machine
The `CellProcess` state machine (Active Object) manages the high-level instrument sequence. It coordinates with the `PumpProcess` and the `Centrifuge` actuator to move fluid and process cells.

![CellProcess State Machine](cell_process_sm.svg)

### PumpProcess State Machine
The `PumpProcess` is a sub-state machine used by `CellProcess` to perform a standardized fluid transfer. It ensures that a valve is opened, the pump reaches speed, a duration expires, the pump is stopped, and the valve is closed in a deterministic, synchronous sequence.

![PumpProcess State Machine](pump_process_sm.svg)

---

## Implementation Details

- **Serialization**: Uses the `msg_serialize` port for compact binary transmission.
- **Shared Constants**: Centralized in `common/util/Constants.h`.
- **Async Marshalling**: Cross-thread and cross-processor logic is unified via DelegateMQ delegates.

---

## Namespace Organization

The project uses a strict nested namespace architecture for clarity and to prevent collisions:
- `cellutron::process`: High-level process logic and state machines (Controller).
- `cellutron::gui`: User interface terminal logic and visualization (GUI).
- `cellutron::safety`: Independent safety monitoring and fault logic (Safety).
- `cellutron::actuators`: Hardware driver abstractions (Valve, Pump, Centrifuge).
- `cellutron::sensors`: Hardware sensor abstractions (Pressure, Air).
- `cellutron::util`: Networking, data serialization, and common system utilities.
- `cellutron::hw`: Shared hardware data types.
