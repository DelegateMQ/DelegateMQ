# State Machine (Active Object)

An automatic door controller built with [active-fsm](https://github.com/DelegateMQ/active-fsm)'s table-driven `StateMachine` and run as an **active object** on DelegateMQ: the state machine owns a thread, and every event, timer callback and state action runs on it. The door class has no mutex.

```
CLOSED --Open--> OPENING --(travel time)--> OPEN --(auto-close)--> CLOSING
  ^                 ^                                                 |
  |                 +------------------- Obstacle --------------------+
  +---------------------------- (travel time) ------------------------+
```

What each part of DelegateMQ does here:

| Part | Role |
| :--- | :--- |
| `dmq::os::Thread` | The door's own thread (`SetThread()`). `Open()`, `Close()` and `Obstacle()` may be called from any thread: the transition map posts the call to the door's thread and returns. |
| `dmq::util::Timer` | Travel time and auto-close. Each timer's `OnExpired` is connected with the door's thread, so its callback is queued like any other event. An entry action starts a timer and the matching exit action stops it. |
| `dmq::Signal` | `OnTransition` (from `StateMachine`). A control panel subscribes with its own thread, so it gets every transition, in order, on that thread. |
| Async-wait delegate | The test reads the panel's log by running `TakeLog()` on the panel's thread and waiting for the result. |
| `Timer::GetNextExpiry()` | The timer driver sleeps until the next deadline instead of polling, and `Timer::SetScheduleChangedHandler()` wakes it when a timer starts. |

A timer can expire just as the state it was started for is left; its callback is then already queued. `Door` stamps the time it entered the state before starting the timer, and the callback ignores an expiry that came too soon after the stamp.

## Tests

1. `Open()`: the door opens, waits, closes on its own; the panel sees the four transitions in order.
2. `Close()` then `Obstacle()` while open: the door starts closing, reverses, reopens, and closes on its own.
3. For one second, four threads call `Open()`, `Close()` and `Obstacle()` at random, thousands of events in all, over several open/close cycles. The door ends up closed, and the panel's transitions form one unbroken chain of legal transitions: every event was handled one at a time on the door's thread.

The program prints `ALL TESTS PASSED` and returns 0.

## active-fsm

`scripts/01_fetch_repos.py` clones active-fsm into `external/active-fsm` (or `DMQ_DEPS_DIR`). Only its `state-machine/` folder is compiled, against this repository's DelegateMQ (`StateMachine.h` includes `DelegateMQ.h`). To use another checkout, pass `-DACTIVE_FSM_DIR=<path>`.

## Build and Run

```bash
python scripts/01_fetch_repos.py      # once, from the repository root

cmake -B build
cmake --build build --config Release

# Windows
build\Release\delegate_state_machine.exe
# Linux
./build/delegate_state_machine
```
