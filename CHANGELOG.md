# Changelog

All notable changes to DelegateMQ are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags. Changes are from the perspective of library users and integrators.

---

## [Unreleased]

### Added
- `NetworkNode::SetCoreAffinity()`: pins the node's thread (receive and send) to cores, like `Thread::SetCoreAffinity()`. Call it before `Start()`. Pumptron's ESP32-S3 controller uses it to keep the network on core 0 with Wi-Fi and the pump control on core 1.
- **Pumptron: the controller runs on an ESP32-S3-DevKitC-1 over Wi-Fi.** `controller/platform/esp32s3` (ESP-IDF; PlatformIO or `idf.py`) runs the same `PumpController` as the simulator and the STM32F4, with `Esp32Board` (the chip's temperature sensor, the BOOT button as local E-STOP, the RGB LED's colours for the four indicators) and the simulator's UDP `NetworkNode`, pointed at the PC. The pump thread is pinned to core 1 and the network thread to core 0, and the board prints its task table with each task's core at startup. `pumptron_gui --udp <address>` and `run_pumptron.py --udp <address>` connect to it (the script also opens `dmq-spy` on the traffic both ways; `--no-spy` to skip); the GUI's self-test passes against the board over Wi-Fi.
- **`dmq::databus::SpyReporter` (`extras/databus/SpyReporter.h`): a target reports its own DataBus traffic to `dmq-spy`.** It subscribes to `DataBus::Monitor()`, queues each message to its own thread and sends it through any DelegateMQ transport, so an embedded target with a network shows up in `dmq-spy` as itself, with its own clock, instead of only through a PC that relays its messages. No `tools/` code and no iostreams. The queue drops when full and a failed send is dropped, never stalling the application; every drop is counted (`GetDropped()`) and reported (`OnDropped`). Queue size `DMQ_SPY_REPORTER_QUEUE_SIZE` (default 64) or the constructor; thread priority, stack and core through `GetThread()`; optional watchdog in `Start()`. Pumptron's ESP32-S3 controller uses it (sender `Controller`): about 15 KB of flash and 4 KB of static RAM, with its stringifiers.

### Changed
- **`dmq-spy` wire format: one DelegateMQ message per packet.** Each spy packet is now a message header (remote ID `SPY_REMOTE_ID`, 0xFFFE) followed by the serialized `SpyPacket`, as any transport sends it; `dmq-spy` accepts only that. The desktop `SpyBridge` (`tools/bridge/spy`) keeps its API (`Start()`, `StartMulticast()`, `Stop()`) and now runs a `SpyReporter`. **Rebuild apps and `dmq-spy` together:** an app built with an older `SpyBridge` doesn't show up in the new `dmq-spy`, and the reverse.
- Pumptron's stringifiers moved from the GUI to `common/util/Stringifiers.cpp` (`snprintf`, no iostreams), so the GUI and the ESP32-S3 controller show the same text in `dmq-spy`.

### Fixed
- **Serializer documentation described the 2.x streams.** `msg_serialize.h` said `write()` needed a seekable `std::ostringstream`, and its comments, `README.md` (with `std::stringstream` examples that no longer compile) and the `SetStream()`/`ISerializer`/`Dispatcher` comments still spoke of input and output streams. They now describe `dmq::ByteWriter`/`dmq::ByteReader`; the README's "Stream Verification" feature (`is_seekable`, removed in 3.0) is gone.

## [3.0.0] - 2026-11-21

3.0 removes iostreams from the remote path: serializers write into a `dmq::ByteWriter` and read from a `dmq::ByteReader` instead of `std::ostream`/`std::istream`. On an ESP32-S3 node that saved 210 KB of flash (20%); on an STM32F407 sample, 163 KB (78%). Code that only uses delegates, signals, threads, timers and the local DataBus doesn't change. Code with remote messages does, mostly by changing types in signatures.

### Migrating from 2.x
- **Message classes** (the built-in serializer's `serialize::I`): change the two signatures; the bodies stay the same.
  ```cpp
  // 2.x
  std::ostream& write(serialize& ms, std::ostream& os) override;
  std::istream& read(serialize& ms, std::istream& is) override;
  // 3.0
  dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override;
  dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override;
  ```
  RapidJSON message classes: `Write(PrettyWriter&, const dmq::ByteWriter&)` and `Read(Document&, dmq::ByteReader&)`.
- **Custom serializers** (`dmq::ISerializer`): `ByteWriter& Write(ByteWriter&, const Args&...)` and `ByteReader& Read(ByteReader&, Args&...)`. Report bad data with `setfail()` instead of `setstate(std::ios::failbit)`.
- **Custom transports** (`ITransport`): `Send(const dmq::ByteWriter& os, const DmqHeader&)` sends `os.data()`/`os.size()`. `Receive(dmq::ByteReader& is, DmqHeader&)` calls `is.assign(nullptr, 0)` first, then `is.assign(data, size)` to view the payload in a buffer that stays valid until the next `Receive()` (a member, not a local).
- **Custom dispatchers and receivers**: `IDispatcher::Dispatch(ByteWriter&, ...)` (call `os.reset()` after sending), `IRemoteInvoker::Invoke(ByteReader&)`, `SetStream(ByteWriter*)`. A `dmq::ByteWriter` replaces the `xostringstream` given to `SetStream()`.
- **Removed**: `dmq::xostringstream`, `dmq::xstringstream` and `extras/allocator/xsstream.h`. Format text with `snprintf` (or `std::ostringstream` where iostreams don't matter).
- **Renamed**: `DelegateError::ERR_STREAM_NOT_GOOD` is `ERR_BUFFER` (same value).
- **`<iostream>`**: no DelegateMQ header includes it any more. Add `#include <iostream>` (and `<sstream>`) to files that use `std::cout` or string streams.
- **`char*` in async calls**: a `const char*` argument of a non-blocking async call no longer compiles; take `std::string`.
- **Message size**: remote messages are capped at `DMQ_MAX_MESSAGE_SIZE` (65535; 4096 under `DMQ_ALLOCATOR`); a larger one reports `ERR_MESSAGE_TOO_LARGE`.
- **UDP**: the embedded UDP transports' default PUB receive timeout is 2 ms (was 50 ms), the same as the desktop ones.

### Added
- **`Participant::OnDuplicateDropped` and `DuplicatesDropped()`.** A message dropped as a duplicate (a retry, or a restarted sender reusing a sequence number within the history window) is now counted and reported, with its remote ID, sequence number and sender node ID; before, it was dropped silently. `SetSeqHistoryExpiry()`'s documentation says what to set it to (the sender's retry window) and why a restarted sender's messages are dropped.
- **`docs/APP-GUIDE.md`: "Pitfalls".** Mistakes that compile and then misbehave quietly, each with its fix: `char*` in async calls, one-way argument copies, async calls outliving their target, a polled UDP SUB socket stalling its thread, a restarted sender's messages dropped as duplicates, ESP-IDF stack sizes in bytes, one `std::cout` linking all of iostreams, the message size cap. Short versions in `llms.txt`; checklist items added. `docs/PORTING.md` gains ESP-IDF notes.
- **`SetCoreAffinity()`: pin a thread to cores.** `dmq::os::Thread::SetCoreAffinity(coreMask)`, before `CreateThread()`, on every thread port (bit 0 = core 0; 0, the default, = any core). The desktop ports pin the thread as it starts (`SetThreadAffinityMask()`, `pthread_setaffinity_np()`); FreeRTOS chooses the core at task creation: `xTaskCreatePinnedToCore()` on ESP-IDF, `xTaskCreateAffinitySet()` on FreeRTOS SMP. ThreadX SMP uses `tx_thread_smp_core_exclude()`, Zephyr `k_thread_cpu_mask_*()` (`CONFIG_SCHED_CPU_MASK`), CMSIS-RTOS2 `osThreadAttr_t::affinity_mask` (API 2.3), NuttX `pthread_attr_setaffinity_np()` (`CONFIG_SMP`), Qt the desktop helper; on a single core every port accepts 0 or a mask with core 0, so portable code can call it unconditionally. A mask the platform can't honor faults. Checked by a new unit test (a pinned thread asked 20 times which core runs it) on Windows and Linux, and by `os/freertos-esp32`'s new Test 8 on both cores of the ESP32 under QEMU. See "Core Affinity" in `docs/PORTING.md`.
- **`example/sensor-node`: a sensor node on the ESP32-S3 and its PC client.** Sampling, control and networking on separate threads (`SampleThread`, `ControlThread`, `NetThread`) that talk only through DelegateMQ async calls, a touch interrupt through `IsrDelegate`, per-thread watchdogs, and DataBus over UDP (Wi-Fi on the board) to a PC client that monitors and commands it. The application code (`node/app`, including the platform loop, `RunNode()`) is platform independent: `node/platform/desktop` runs it on Windows and Linux with stand-ins for the hardware, and `node/platform/esp32s3` (ESP-IDF, PlatformIO or `idf.py`) on an ESP32-S3-DevKitC-1 with the chip's temperature sensor, the RGB LED and the BOOT button's interrupt. Sampling and control are pinned to core 1, the network to core 0. The node publishes only while a client keeps sending `cmd/start` (a lease, which also sets the statistics interval); commands are ACKed and retried; every message carries a sequence number (`MessageBase`, as in Cellutron and Pumptron), and the node applies a command only if it's newer than the last (`MonotonicGuard`), with a client session id so a restarted client is accepted at once. `build_all.py` runs the node and the client together over loopback UDP; the client's self-test also passes against the board. At startup the node measures DelegateMQ's async-wait round trip to the other core and within one. On Linux the same `node/app` code also runs on ThreadX (its Linux port) and Zephyr (`native_sim`), each with only a small `main()`; `build_all.py` runs the PC client against both. `run_sensor_node.py` starts DelegateMQ's diagnostic tools (`dmq-spy`, `dmq-monitor`, `dmq-thread`) with the node and the client, which feed them with `--tools`; every topic has a stringifier, so the spy shows values (and PlotJuggler series), not `?`.
- **lwIP UDP transport: `SetRecvTimeout()`,** like the Linux and Win32 UDP transports, for a thread that polls the socket between other work. The lwIP transport also runs on ESP-IDF (whose network stack is lwIP): `sensor-node`'s ESP32-S3 build uses it over Wi-Fi.
- **`boards/esp32s3-devkitc`: DelegateMQ on an ESP32-S3-DevKitC-1 (N8R8).** A simple board demo under ESP-IDF's FreeRTOS: a synchronous delegate, an async delegate, an async-wait call returning a value, a `Signal` with a synchronous and an async subscriber, a `Timer` blinking the RGB LED, and the BOOT button's interrupt through an `IsrDelegate`. Built with PlatformIO (`framework = espidf`, pinned to ESP-IDF 5.4.1 like `os/freertos-esp32`) or `idf.py`. Run on a v1.1 board (RGB LED on GPIO 38). Not built by `build_all.py`: it needs the board.
- **`patterns/state-machine` sample: an active-object state machine.** An automatic door controller built with [active-fsm](https://github.com/DelegateMQ/active-fsm)'s `StateMachine` (v1.1.0, fetched into `external/` by `01_fetch_repos.py`) runs on its own thread: events from any thread, timer-driven transitions with stale-expiry handling, `OnTransition` observed on another thread, and an event-driven timer driver (`Timer::GetNextExpiry()`). Its self-checks include four threads sending events at once. New `patterns/` sample group.
- **`patterns/async-sqlite` sample: a blocking library on its own thread.** A sensor data logger built with [Async-SQLite](https://github.com/DelegateMQ/Async-SQLite) (v1.1.0, SQLite 3.54.1, fetched into `external/` by `01_fetch_repos.py`), which runs every SQLite call on one DelegateMQ thread. Its self-checks cover four threads writing at once with no lock or `SQLITE_BUSY`, a transaction sent to the SQLite thread as one function (no other write lands inside it), a `std::future` query, a call with a timeout returning `SQLITE_BUSY` while the thread is busy, and a signal delivered to a display thread.
- **No heap after startup: static pools for the fixed-block allocator.** With `DMQ_ALLOCATOR`, define `DMQ_XALLOCATOR_STATIC_POOLS` and set each block size's count (`DMQ_XALLOC_POOL_<size>`, 16 B to 16 KB) in `DelegateMQConfig.h`: every block then comes from static memory, a size with no blocks takes no RAM, and a full pool faults through `BAD_ALLOC()`. `xalloc_print_pool_config()` prints the peak blocks in use per size as config lines: run under the heaviest load, add headroom, rebuild. This replaces the old `STATIC_POOLS` define inside `xallocator.cpp` (a fixed 32 blocks of each of 10 sizes, about 262 KB). The library's immortal locks are now constructed in static storage, not with `new`: `AsyncWaitResultLock()` used to allocate on the first async-wait call. The RTOS thread ports (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX) now fault through `BAD_ALLOC()` when a queued message can't be allocated, like the rest of the async path; before, `DispatchDelegate()` dropped the call, and `ExitThread()` skipped its exit message and waited forever. `os/freertos-bare-metal`'s Cortex-M3 target now runs every test with static pools (about 4 KB), and its new Test 14 counts every `malloc`/`calloc`/`realloc` (`-Wl,--wrap`) while async calls, signals, async-wait calls, timer ticks and ISR posts run: zero. See "No Heap After Startup" in `docs/SAFETY.md`.
- **Pumptron: thread watchdog dumps show the hung thread's own call stack.** The F4 firmware sets a watchdog hook (`dmq::os::SetWatchdogHook()`) that stores the unresponsive thread's registers and stack, read from the context FreeRTOS saved when it was switched out, plus how long it had not responded. Before, the dump held the watchdog checker's stack. `-DPUMPTRON_TEST_HANG=ON` makes the 5 s button test hang the Pump thread; on the STM32F4 Discovery the dump's PC and call stack decode to the hang loop and its callers. The GUI shows the captured registers.
- **Watchdog and fault hooks: `dmq::os::SetWatchdogHook()`, `dmq::util::SetFaultHook()`.** Application functions called on the way down, to capture a failure (core dump, a log in RAM that survives the reset, the hung thread's saved program counter); DelegateMQ captures nothing itself. The watchdog hook gets each hung thread (`WatchdogInfo`: the `Thread`, so its OS handle, its name and how long it stalled). `WatchdogCheckAll()` now reports every hung thread before calling `WatchdogHandler()` once, so a deadlock shows both threads; before, it stopped at the first. The fault hook gets the failed `DMQ_ASSERT`'s file and line, from the default `FaultHandler()`. `os/freertos-bare-metal`'s Test 15 hangs two threads on a Cortex-M3 under QEMU: both are reported, and their saved PCs resolve to where each is stuck. See "Capturing the Failure" in `docs/PORTING.md`.
- **Hardware watchdog guide** ("Hardware Watchdog" in `docs/PORTING.md`). Feed the MCU's watchdog (STM32 IWDG, ESP-IDF task watchdog, nRF WDT, Zephyr) right after `Thread::WatchdogCheckAll()` returns: `WatchdogHandler()` never returns, so a hung thread stops the feed and the hardware resets the MCU, which also covers a stalled checker task or a dead scheduler. Covers where not to feed (ISRs, SysTick, the idle hook), timeout sizing, startup, debugger freeze, low-power sleep limits, reset-cause flags, ESP-IDF's `CONFIG_ESP_TASK_WDT_PANIC`, and bare metal. No library change.
- **Event-driven timers: `Timer::GetNextExpiry()` and `Timer::SetScheduleChangedHandler()`.** Instead of calling `ProcessTimers()` on a fixed period, a task can wait until the next timer is due, woken early by the handler (a plain function pointer that `Start()` calls) when a sooner deadline is added. Timers fire on time without a fast polling period, and the CPU can sleep between deadlines (tickless idle, `WFI`), for battery and low-power devices. `GetNextExpiry()` takes only the timer list's critical section, so an idle hook or ISR can call it. `os/freertos-bare-metal`'s Test 13 runs such a driver (Win32 simulator and Cortex-M3 on QEMU): a 100 ms periodic timer wakes it 11 times a second instead of 105 with a 10 ms poll, and one-shots fire within a tick of their deadline. See "Event-driven processing" in `llms.txt`.
- **RTTI is optional (`DMQ_NO_RTTI`).** DelegateMQ builds and runs with `-fno-rtti` (GCC, Clang) or `/GR-` (MSVC): `DelegateSelect.h` defines `DMQ_NO_RTTI` when the compiler has RTTI off, and `delegate/DelegateCast.h` replaces `dynamic_cast` (delegate equality, async message dispatch), `std::type_index` (DataBus topic types) and `typeid` with per-type IDs. One difference: a delegate made from a `std::function` object equals only its own copies (keep it to remove it); one made from a lambda compares as before. The whole unit-test app passes with RTTI off (new CI jobs: GCC and MSVC; `build_all.py --full` variant `build-no-rtti`), and `bare-metal/qemu`'s Cortex-M0 target builds with `-fno-rtti`. Measured: 5.6% less code on a bare-metal Cortex-M4. `library.json` no longer strips `-fno-rtti` from DelegateMQ's own sources, so they build the same way as the project (mixing the two is undefined).
- **`dmq::util::IsrDelegate`: call a delegate from an interrupt.** `Post()` in the ISR copies the arguments into a fixed ring slot under `dmq::CriticalSection` (no heap, no mutex); `IsrDelegates::ProcessAll()`, from a task or the main loop, calls the target, which may be async to a thread. An optional post handler wakes the task that calls it. One header in `extras/util`, no change to the thread ports: it works on every port, bare metal included. Tested on real timer interrupts under QEMU (`bare-metal/qemu` on all five CPUs, FreeRTOS on a Cortex-M3 and on a dual-core ESP32) and by `test/unit-tests/IsrDelegateTests.cpp`.
- **PlatformIO: ESP32-S3.** The `async-delegates` example and the PlatformIO CI workflow build an `esp32-s3` environment (ESP32-S3-DevKitC-1). `library.json` adds the registry keywords `zephyr`, `esp32`, `stm32`, `signals` and `events`.
- **ESP32 under QEMU: `os/freertos-esp32`.** DelegateMQ's FreeRTOS port on a dual-core ESP32 (Xtensa) under ESP-IDF v5.4, run under Espressif's QEMU: the portable thread and timer tests on ESP-IDF's SMP FreeRTOS (`FreeRTOSCriticalSection`'s `ESP_PLATFORM` spinlock branch), and `Timer` started and stopped from a hardware timer interrupt while a task on each core does the same. `build_all.py --full` runs it on Linux; the Docker image installs ESP-IDF and Espressif's QEMU.
- **Cortex-M33 and AArch64 in the test suite.** `bare-metal/qemu` has an `arm-m33` target (Armv8-M Mainline, QEMU `mps2-an505`, the core of most current MCUs), and `build_all.py --full` runs the unit-test app built for 64-bit Arm Linux (aarch64) under QEMU user mode, as it does for s390x and armhf.
- **Full test suite: `build_all.py --full`.** Runs every sample the platform can run plus the unit-test app in all its builds (embedded `DMQ_ALLOCATOR`/`DMQ_ASSERTS`, stress; clang, ASan, TSan and UBSan on Linux), and fails on a missing tool or unbuilt sample instead of skipping it. `docker/run.sh` runs the Linux half in a container with every tool installed, and `build_all.py --linux-host USER@HOST` runs both halves from Windows with one combined report. See "Full Test Suite" in `docs/BUILD.md`.
- **`os/win32` sample: the `DMQ_THREAD_WIN32` port is now built and tested.** Nothing built that port before. The sample runs the same tests as the other `os/` samples, including the shared ones in `test/portable` (cross-thread dispatch, `FullPolicy`, `StartGate`, `PacedDispatch`, `TimerDelegate`). `build_all.py` runs it on Windows.
- **Unix domain socket build of `transport/serializer`** (`-DSAMPLE_TRANSPORT=uds`, Linux). `LinuxUdsTransport` was used only inside `databus/relay`; `build_all.py` now also runs `uds-builtin` on Linux.
- **`ThreadMonitor` unit tests.** `test/unit-tests/databus/ThreadMonitorTest.cpp` checks a thread's own statistics (`Thread::SnapshotStats()`: queue depth, time waiting in the queue, time in the call, dispatch count, and the per-window reset), the `ThreadStatsPacket` serializer, and `ThreadMonitor` publishing registered threads' stats on the DataBus (and leaving out a deregistered one). `ThreadMonitor` had no tests before.
- **`databus/shapes` reports thread statistics to `dmq-thread`.** The client now handles shape updates on its own thread (`ShapeRx`, `FullPolicy::DROP`) instead of the network receive thread, and with `DMQ_DATABUS_TOOLS` registers it with `ThreadMonitor`, so `dmq-thread` shows its queue depth and latency. Before, only Cellutron used `ThreadMonitor`.
- **lwIP transports built and run: `transport/serializer` `lwip` and `lwip-netconn` builds** (Linux). `ArmLwipUdpTransport` and `ArmLwipNetconnUdpTransport` run on lwIP's own UDP/IP code over lwIP's Unix port and loopback interface, with acknowledgments. Neither transport was compiled anywhere before. `scripts/01_fetch_repos.py` now fetches lwIP 2.2.1.
- **`transport/netx-udp` sample: `NetXUdpTransport` built and run** on ThreadX's Linux simulation port, over NetX Duo's loopback interface, reusing `transport/serializer`'s app. It was never compiled before, and running it found the acknowledgment bug below. `scripts/01_fetch_repos.py` now fetches NetX Duo 6.5.1 (the pinned ThreadX's release).
- **`transport/serializer` fails when acknowledgments never arrive.** With acknowledgments on (UDP, TCP, lwIP, NetX), the app now exits with status 1 if the sender got none back, and prints how many it got and how many timed out. Before, a broken acknowledgment path still passed `build_all.py`, since messages arrived anyway.
- **`integration/cmake-subdirectory` sample: the documented CMake integration is now tested.** It adds DelegateMQ exactly as `docs/BUILD.md` says (`FetchContent`, here from the local checkout, then `DelegateMQ::DelegateMQ`) and checks the documented promises: an option set beforehand reaches the app, no `DMQ_*` definition leaks into a target that doesn't link DelegateMQ, only the library is built, and the library works. Every other sample includes `DelegateMQ.cmake` directly, so this path had never been built.
- **`build_all.py` runs the C# interop wrapper and `example/sample-interop`.** `databus/interop` now also runs with its C# client (Windows), and `sample-interop` runs its C++ server with the Python client and, on Windows, the C# client at once. Before, only the Python wrapper ever ran. `sample-interop`'s server and clients take an optional run time in seconds; a timed client exits with status 1 unless it received sensor data and had a command acknowledged.
- **`build_all.py` runs the diagnostic tools against `dmq-target`.** `dmq-spy --echo` must print its topics, `dmq-monitor --once` must list its node, and `dmq-thread --once` its `SpyBridge` thread, which tests ThreadMonitor end to end. New options for this: `dmq-thread --once <sec>` (print the thread table as text and exit), `dmq-spy --duration <sec>` (stop `--echo`, `--hz` or `--bw` after that long), and `dmq-target [seconds]`. `dmq-target` now enables ThreadMonitor.

### Changed
- **A `char*`/`const char*` argument in a non-blocking async call no longer compiles.** Only its first character was copied for the target thread, which then read past it. The `static_assert` says to take `std::string`; async-wait calls (the caller waits, so the pointer stays valid) are unaffected, as are `signed char`/`unsigned char` (`uint8_t*`). `DMQ_ALLOW_ASYNC_CHAR_PTR` turns the check off.
- **UDP receive timeouts come from the config on every UDP transport.** The Linux and Win32 transports hard-coded them (2 ms PUB, 2 s SUB); they now use `DMQ_UDP_PUB_RECV_TIMEOUT_MS` / `DMQ_UDP_SUB_RECV_TIMEOUT_MS` like the embedded ones. The PUB default drops from 50 ms to 2 ms (the desktop value): `NetworkNode` drains each peer's ACKs in turn and waited the full timeout per idle peer, 50 ms each on lwIP, netconn, Zephyr and NetX. Every UDP `Create()` now documents the SUB timeout and `SetRecvTimeout()`, as do `Participant::ProcessIncoming()` and the FreeRTOS stack size (bytes on ESP-IDF).
- **Breaking: the remote path carries bytes in `dmq::ByteWriter` / `dmq::ByteReader`, not iostreams.** iostreams (and the locale code they pull in) were about 205 KB of an ESP32-S3 image: `example/sensor-node`'s firmware went from 1,060,802 to 851,202 bytes of flash (−19.8%) and 4.5 KB less static RAM, with no C++ stream code left in it. `ByteWriter` is a growable byte buffer (an `xstring`, so fixed-block under `DMQ_ALLOCATOR`); `ByteReader` is a read cursor that views a transport's receive buffer without copying it. Both have the few stream-like members a binary serializer needs (`write()`/`read()`, `tellp()`/`seekp()`, `tellg()`/`seekg()`/`skip()`, `good()`/`fail()`/`eof()`, `setfail()`). What changes for an application:
  - `ISerializer`: `ByteWriter& Write(ByteWriter& os, const Args&...)` and `ByteReader& Read(ByteReader& is, Args&...)`. A serializer reports bad data with `setfail()` instead of `setstate(std::ios::failbit)`.
  - The built-in serializer's message classes (`serialize::I`): `dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os)` and `dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is)`; the bodies don't change. RapidJSON message classes take `const dmq::ByteWriter&` / `dmq::ByteReader&` the same way.
  - `ITransport`: `Send(const ByteWriter& os, const DmqHeader&)` sends `os.data()`/`os.size()`; `Receive(ByteReader& is, DmqHeader&)` points `is` at the payload in the transport's buffer (`is.assign()`), valid until the next `Receive()`. `IDispatcher::Dispatch(ByteWriter&, ...)`, `IRemoteInvoker::Invoke(ByteReader&)` and `SetStream(ByteWriter*)` likewise.
  - `dmq::xostringstream`, `dmq::xstringstream` and `extras/allocator/xsstream.h` are gone. Code that used them to format text can use `snprintf` (or `std::ostringstream` where iostreams don't matter).
  - The serializers no longer need `dynamic_cast` to empty a reused buffer. Cereal needs std streams, so `CerealSerializer` bridges to them with a small `std::streambuf`: only an app that picks Cereal pays for iostreams. MessagePack, RapidJSON and Bitsery now read the received bytes in place; before, each copied them out of the stream.
  - Transports no longer copy the payload out of the stream to send it (`os.str()`), and receiving no longer copies it into one. `ThreadStatsPacketToString()`, `xalloc_stats()` and DataBus's unknown-topic name use `snprintf` instead of a stream.
  - `DelegateError::ERR_STREAM_NOT_GOOD` is now `ERR_BUFFER` (same value, 1), and the new `ERR_MESSAGE_TOO_LARGE` (11) reports a message over `DMQ_MAX_MESSAGE_SIZE`.
  - **Message size cap: `DMQ_MAX_MESSAGE_SIZE`** (default 65535, the header's 16-bit length limit; 4096 under `DMQ_ALLOCATOR`). A `ByteWriter` never grows past it: the write fails before anything is allocated (`fail()`, `overflowed()`) and the remote delegate reports `ERR_MESSAGE_TOO_LARGE`. Before, a large message under `DMQ_ALLOCATOR` faulted once the stream's buffer outgrew the allocator's largest block (16 KB), and on any build a message too big for the transport was serialized in full before `Send()` rejected it. Under `DMQ_ALLOCATOR` a `static_assert` keeps the cap within that block (at most 8181). A `ByteWriter` can also be given a smaller cap of its own (`ByteWriter(maxSize)`).
  - `ByteWriter::reserve(n)` allocates the buffer up front (at startup), so sending never grows it; `reset()` keeps the capacity, so once a remote delegate's buffer has grown, later messages allocate nothing for it.
  - The STM32F407 sample (`boards/stm32-freertos` server, Release) shrank from 209,772 to 46,660 bytes of flash (−78%), after its own `std::cout` lines also became `printf`. With the library converted but the app still printing with `std::cout`, it was 209,764 bytes: a single use of iostreams anywhere in the image brings all of it back.
  - No library header includes `<iostream>` any more. The thread ports print an exception escaping a delegate with `fprintf(stderr, ...)` instead of `std::cerr` (on an RTOS with exceptions on, that alone pulled iostreams into the image), and `WatchdogHandler()` uses `printf`. Code that uses `std::cout` must `#include <iostream>` itself; before, it got it from DelegateMQ's headers.
- **Documented the fixed-block allocator's trade-offs** (`DMQ_ALLOCATOR`, "Trade-offs" in `docs/DETAILS.md`): it prevents fragmentation from the library's allocations, but each allocation and free locks a kernel mutex, so on ESP-IDF it measured about twice as slow as the platform heap per async call; block sizes are powers of two; and only allocations that go through it are covered.
- **FreeRTOS and ThreadX: `dmq::Semaphore` is the RTOS's own semaphore.** Every async-wait call creates and destroys a `dmq::Semaphore`. On FreeRTOS and ThreadX it was the generic one, a mutex plus a condition variable: two kernel objects (on FreeRTOS both from the heap) and three or four mutex locks per call. It is now one binary semaphore, like the Zephyr, CMSIS-RTOS2 and NuttX ports: `FreeRTOSSemaphore` (static storage when `configSUPPORT_STATIC_ALLOCATION` is 1, else the heap) and `ThreadXSemaphore` (`tx_semaphore_ceiling_put()` with a ceiling of 1). Measured on an ESP32-S3 at 160 MHz: creating and destroying one went from 28 us to 2.4 us, and an async-wait round trip to the other core from 200 us to 162 us. `Signal()` stays callable from an interrupt.
- **Documented the rules for calling `Timer::ProcessTimers()` from an interrupt on bare metal.** A signal's subscriber list has no lock there (`NullMutex`), so with `ProcessTimers()` in an ISR: connect and disconnect a timer's `OnExpired` only while that timer is stopped, keep each timer to at most `DMQ_SIGNAL_SBO_COUNT` (8) subscribers (more allocate inside the ISR), and keep synchronous callbacks ISR-safe; otherwise call it from the main loop. `Timer.h` ("Bare Metal: ProcessTimers() in an ISR"), `docs/APP-GUIDE.md`, `llms.txt` and the `bare-metal/qemu` sample, which already followed the rules, now say so. Previously the docs only said bare metal may call it from an ISR.
- **Cellutron uses active-fsm v1.1.0 instead of its own copy of the state machine.** `example/cellutron/common/state-machine/` is gone; the Controller compiles `state-machine/` from `external/active-fsm` (fetched by `01_fetch_repos.py`). The old copy had drifted: `GetCurrentState()` was not atomic, and the hierarchical engine had no depth limit on its exit and ancestor walks.
- **CI checks the embedded ports for libatomic calls; Dependabot updates the workflows' actions.** The compile-only Cortex-M4 jobs (FreeRTOS, ThreadX, CMSIS-RTOS2) now fail if the object file calls an `__atomic_*` function: a 64-bit `std::atomic` compiles but doesn't link on a 32-bit Cortex-M, which is how the ThreadX and CMSIS-RTOS2 watchdog bug passed CI. `.github/dependabot.yml` opens one pull request a month for new GitHub Actions versions.
- **README.md uses relative links again; the PlatformIO package gets tag-pinned ones.** Links and images point into the repository, so they work on branches, forks and in a local clone. `scripts/package_readme.py` packs the PlatformIO library (`pio pkg pack`) with a README whose links point to the release tag on GitHub, and checks none is left relative; publish that tarball. The registry page then links to the docs of the version it shows, not to `master`.
- **The Linux test image builds the Arm bare-metal targets with GCC 15.** `docker/Dockerfile` installs Arm GNU Toolchain 15.2.rel1 (Arm's release, checksum-verified) instead of Ubuntu 24.04's `gcc-arm-none-eabi` packages (GCC 13.2), so `bare-metal/qemu`'s `arm-m4` and `arm-m0` and `os/freertos-bare-metal`'s `arm-m3` are tested with the current Arm compiler on Linux too.
- **Bare-metal samples merged into `example/sample-projects/bare-metal/qemu`.** `bare-metal-arm`, `atfe-armv7m-bare-metal` and `bare-metal-riscv` are now targets of one sample (`-DBM_TARGET=arm-m4|arm-m3-atfe|riscv32`) sharing one set of tests, plus a new `arm-m0` target: Cortex-M0 (Armv6-M: no atomic instructions, unaligned access faults) on QEMU `microbit` (nRF51822, 16 KB RAM), with the tick from the nRF51's TIMER0 interrupt. The tests now check their own results and end QEMU with exit status 0 or 1 through semihosting, instead of always printing `ALL TESTS PASSED` and looping. The Cortex-M3/ATfE target gained the timer test on a real SysTick interrupt. `build_all.py` builds and runs every target whose toolchain is installed.
- **`transport/serialport` and `transport/mqtt` take their settings on the command line and report failure.** `transport/serialport`: `delegate_app [seconds] [sender-port] [receiver-port]` (default `COM1`/`COM2`). `transport/mqtt`: `delegate_pub_app`/`delegate_sub_app [broker-uri] [seconds]` (default 60 s). The receiving app exits with status 1 if nothing arrived. `build_all.py --full` now runs both on Linux, over a `socat` pseudo-terminal pair and a mosquitto broker it starts, and runs `os/qt`.
- **Unit tests on big-endian and 32-bit CPUs, and a cross-endian check.** `build_all.py --full` also runs the unit-test app cross-built for s390x (big-endian) and armhf (32-bit Arm) under QEMU user mode, and runs `system-architecture/no-deps` between a native x86 peer and an s390x peer, both ways round. Both pass: `DmqHeader` is big-endian on the wire and the built-in serializer writes little-endian on every host.
- **Zephyr and NuttX samples run in `build_all.py`.** `os/zephyr`, `transport/zephyr-udp`, `os/cmsis-rtos2` and `databus/zephyr` (Zephyr `native_sim`, configured with plain CMake `-DBOARD`) build and run on Linux when `ZEPHYR_BASE` is set, and `os/nuttx` when `NUTTX_SRC` points at NuttX and nuttx-apps checkouts. The Docker image has Zephyr v4.1.0 and NuttX 12.4. They used to be manual-only.
- **libserialport builds on Linux.** `02_build_libs.py` builds it statically with its autotools build. To open the pseudo-terminal pair, it builds libserialport 0.1.2 (the latest release) with `NO_PORT_METADATA` and a small patch so ports without modem-control lines aren't an error; real serial ports behave as before.

- **FreeRTOS and CMSIS-RTOS2 on a real Cortex-M, with a hardware interrupt.** `os/freertos-bare-metal` has a new `arm-m3` target (`-DBM_TARGET=arm-m3`: FreeRTOS `GCC/ARM_CM3` on QEMU `mps2-an385`), and `os/cmsis-rtos2` builds for Zephyr's `mps2/an385` board. Each adds a test in which a real timer interrupt uses `dmq::CriticalSection` and calls `Timer::Start()`/`Stop()`, verifying `FreeRTOSCriticalSection`'s ISR path and `CmsisRtos2CriticalSection` (until now unverified). `build_all.py --full` runs both; the Docker image adds Zephyr's `cmsis` module and the Zephyr SDK's Arm toolchain.
- **One set of portable thread and timer tests for every OS port sample.** The OS port samples (`os/posix`, `os/qt`, `os/freertos`, `os/threadx`, `os/zephyr`, `os/cmsis-rtos2`, `os/nuttx`) each had their own drifting copy of `DelegateThreadsTests.cpp`/`TimerDelegateTests.cpp`. They now all run `test/portable/` (`PortableTests::DelegateThreadsTests()`/`TimerDelegateTests()`, the superset of the copies), with per-port differences passed as `PortableTests::Options` (consumer-thread priority, and whether several worker threads may run at once). `StartGatePortableTest.h` moved there from `test/unit-tests/`. `os/cmsis-rtos2` raises `CONFIG_CMSIS_V2_MUTEX_MAX_COUNT` to 64: the shared timer tests keep 21 timers alive at once, each with its own mutex.
- **Transport and serializer samples merged into `example/sample-projects/transport/serializer`.** One sender/receiver app, built for the transport and serializer chosen at configure time: `-DSAMPLE_TRANSPORT=udp|tcp|pipe|zeromq|nng` and `-DSAMPLE_SERIALIZER=builtin|bitsery|cereal|msgpack|rapidjson`, in any combination. The transport-specific code is in `transport/<name>/transport.h` and the argument classes in `serializer/<name>/data.h`. It replaces `linux-tcp-serializer`, `linux-udp-serializer`, `win32-tcp-serializer`, `win32-udp-serializer`, `win32-pipe-serializer`, `zeromq-serializer`, `zeromq-bitsery`, `zeromq-cereal`, `zeromq-msgpack-cpp`, `zeromq-rapidjson` and `nnq-bitsery`, which are removed; each maps to one `build-<transport>-<serializer>` configuration (listed in the sample's README). The app now exits with status 1 if no message arrived intact, and the Bitsery, Cereal and MessagePack builds send a second argument (`DataAux`) as the built-in serializer's did. `build_all.py` builds and runs each former sample's combination.
- **Samples grouped into folders under `example/sample-projects`.** The samples now sit in seven groups, each with a README listing its samples: `os/` (one sample per OS port: `freertos`, `freertos-bare-metal`, `threadx`, `zephyr`, `cmsis-rtos2`, `nuttx`, `posix`, `qt`, and the new `win32`; formerly `freertos-linux`, `threadx-linux` and so on), `bare-metal/` (`qemu`, formerly `bare-metal`, and `keil`, formerly `keil-bare-metal`), `boards/` (`stm32-freertos`), `transport/` (`serializer`, formerly `transport-serializer`; `in-process`, formerly `bare-metal-remote`, which is a desktop app; `serialport`, `mqtt` and `zephyr-udp`, formerly `serialport-serializer`, `mqtt-rapidjson` and `zephyr-udp-serializer`), `databus/` (`basic`, formerly `databus`, and `freertos`, `interop`, `mqtt-gateway`, `multicast`, `relay`, `shapes` and `zephyr`, formerly `databus-<name>`), `system-architecture/` (`full`, `no-deps`, `python`) and `integration/` (`clang-native`, `no-cmake`, formerly `no-cmake-integration`). The test scripts name a sample `<group>/<sample>` (`full_suite.SAMPLE_GROUPS`, `list_samples()`, `sample_name()`).

### Fixed
- **RTOS ports: a thread's watchdog could expire falsely.** `WatchdogExpired()` read the clock, then the thread's last heartbeat. If the thread stored a heartbeat in between (from the other core, or by preempting the checking task) with a tick newer than the clock reading, the unsigned 32-bit difference wrapped to about 49 days and a healthy thread was reported as hung. FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX now read the heartbeat first, and treat a heartbeat newer than the clock as alive. Found on an ESP32-S3: an idle `sensor-node` faulted after about 45 minutes. `os/freertos-esp32`'s new Test 9 checks it on both cores under QEMU (a busy thread on core 1, `WatchdogCheckAll()` in a tight loop on core 0): the old code failed it in 6 runs of 6, within 23,000 checks; on the board, within 50,000, and the fix ran 6.7 million checks with no false expiry. The desktop ports compare signed durations and weren't affected.
- **`os/freertos-esp32` Test 2 failed now and then under QEMU** ("queue full"): it posts 100 calls in a row to a worker with the default 20-message queue and `FullPolicy::FAULT`. With the worker on the other core, QEMU could run the whole burst while that core waited for its time slice. The worker's queue now fits the burst; full-queue behavior is Test 4's subject.
- **ESP-IDF: `FaultHandler()` and `WatchdogHandler()` now call `abort()`** instead of spinning. ESP-IDF's panic handler then prints a backtrace and reboots or halts as the project configures. Spinning starved the idle task, and ESP-IDF's task watchdog repeated a warning every few seconds that buried the one line saying what failed.
- **CMSIS-RTOS2: `dmq::ThisThread::GetCurrent()` from an interrupt returned the interrupted thread.** On Cortex-M the port's current-thread key is now 0 inside an interrupt (IPSR, through CMSIS-Core), so `GetCurrent()` returns `nullptr` there, as on FreeRTOS (Cortex-M, ESP-IDF), Zephyr and NuttX. Other cores keep the old behavior; `detail::IsInsideCmsisRtos2Interrupt()` in `CmsisRtos2CriticalSection.h` is the place to add one. `os/cmsis-rtos2`'s Cortex-M3 Test 11 checks it with the interrupt landing on a busy CMSIS-RTOS2 thread, and fails without the fix.
- **Porting docs and `@TODO` notes brought up to date.** `docs/PORTING.md`'s checklist lists what each `@TODO` is for, what a new OS port provides (including `CriticalSection`, the interrupt-masking lock `Timer` takes from interrupts), the bare-metal rules for `ProcessTimers()` in an interrupt, and `IsrDelegate` for reaching a thread from an interrupt. The fault-handling `@TODO` in `DelegateOpt.h` no longer says to edit the macros (use `SetFaultHook()` or `DMQ_CUSTOM_FAULT_HANDLER`), the heap `@TODO` mentions static pools, `xallocator.cpp`'s `CHECK_ALIGNMENT` note says to uncomment it (it read "comment out"), and the FreeRTOS current-thread `@TODO` no longer lists ESP-IDF, which is already handled.
- **`SpyBridge::Stop()` left a dangling thread in ThreadMonitor.** `Start()` registers the bridge's thread with `ThreadMonitor`, but `Stop()` destroyed it without deregistering, so with the monitor enabled (as in Cellutron) its next poll read a freed thread. `Stop()` now deregisters it first.
- **`NetXUdpTransport` never sent an acknowledgment.** An ACK has an empty payload, and NetX's checked API rejects a zero-byte `nx_packet_data_append()` (`NX_SIZE_ERROR`), so `Send()` returned before sending. With NetX's default error checking, every reliable message timed out, was retried, and was finally reported as failed. The payload is now appended only when there is one.
- **lwIP builds failed to configure with lwIP 2.x.** `External.cmake` required lwIP 1.4's `src/include/ipv4` directory, which lwIP 2.x doesn't have.
- **The CMSIS-RTOS2 port didn't link on 32-bit Cortex-M.** `CmsisRtos2Thread` kept its watchdog times in 64-bit `std::atomic`s, which need libatomic there, and arm-none-eabi toolchains have none. It now keeps 32-bit counts, as `FreeRTOSThread` does.
- **The ThreadX, Zephyr and NuttX ports didn't link on 32-bit Cortex-M either.** `ThreadXThread`, `ZephyrThread` and `NuttXThread` had the same 64-bit `std::atomic` watchdog members (`undefined reference to __atomic_load_8`). They now keep 32-bit millisecond counts with wrap-safe differences. `os/zephyr` also builds for Zephyr's `mps2/an385` board (Cortex-M3) and runs under QEMU, which `build_all.py --full` now does on Linux.
- **`AllocatorPool` overflowed its memory for types smaller than a pointer.** Its buffer was `sizeof(T) * Objects` bytes, but each block is at least pointer-sized, so e.g. `AllocatorPool<int, 5>` on 64-bit used 40 bytes of a 20-byte buffer; odd-sized blocks also misaligned the free-list pointer. New `Allocator::BlockSize()` sizes both.

## [2.4.1] - 2026-11-14

Documentation only; no code changes.

### Fixed
- **README on the PlatformIO registry page.** The registry renders the packaged README without the repository, so the layer diagram and screenshot didn't load and relative links went nowhere; they now use absolute GitHub URLs. Also: bold text containing inline code showed literal `**` (now HTML `<b>`), links labelled with inline code showed in a small monospace font, links to sections of the page did nothing (headings have no IDs there), and `bash` code blocks weren't highlighted (now `shell`).

## [2.4.0] - 2026-11-14

### Upgrading to 2.4.0
- **Rename `dmq::Priority::HIGH` to `dmq::Priority::URGENT`.** Arduino cores `#define HIGH`, so any code that included `Arduino.h` before DelegateMQ failed to compile. `NORMAL` is unchanged, and messages are ordered the same way.
- **Qt port: default queue size is now 1000, up from 20.** A Qt `Thread` created with `maxQueueSize` 0 (the default) now uses `dmq::THREAD_DESKTOP_QUEUE_SIZE` (`DMQ_THREAD_DESKTOP_QUEUE_SIZE`, 1000) like the stdlib, Win32 and POSIX ports, not the RTOS default `dmq::DEFAULT_QUEUE_SIZE` (20). A burst that used to fault, drop or time out at 20 queued messages is now queued. To keep the old cap, pass `maxQueueSize` 20 to the `Thread` constructor.

### Added
- **PlatformIO library.** `library.json` at the repo root; add `lib_deps = endurodave/DelegateMQ@^2.4.0`. See `example/platformio/PLATFORMIO.md` for supported boards, the C++17/RTTI flags and where to put `DelegateMQConfig.h`. `example/platformio/async-delegates` is one sketch built for ESP32, ESP32-C3, STM32 Nucleo-F401RE, STM32F4 Discovery, Raspberry Pi Pico and the desktop, and run on an STM32F4 Discovery board (FreeRTOS from the STM32duino FreeRTOS library, async delegates included).
- **ESP32 (ESP-IDF and Arduino-ESP32) on the FreeRTOS port.** Kernel headers come from `freertos/` (new `FreeRTOSHeaders.h`), `FreeRTOSCriticalSection` uses an SMP spinlock (`portENTER_CRITICAL_SAFE`), and ISR detection uses `xPortInIsrContext()`. Selected by `ESP_PLATFORM`. Compiled for ESP32 with PlatformIO; not yet run on hardware.
- **`qt-linux` sample and CI for the Qt port.** Runs the same thread, FullPolicy and timer tests as the other ports on `DMQ_THREAD_QT`. Built and run by the new Qt workflow; needs Qt 6 Core, so `build_all.py` skips it.
- **PlatformIO CI.** A workflow builds the PlatformIO example for every board and runs the desktop build.

### Fixed
- **FreeRTOS port: build failed without static allocation.** `CreateThread()` called `xTaskCreateStatic()` unconditionally, which FreeRTOS declares only when `configSUPPORT_STATIC_ALLOCATION` is 1 (STM32duino FreeRTOS leaves it 0). It's now compiled only then; otherwise threads use the heap and `SetStackMem()` with a buffer faults.
- **Qt port: queue count only dropped with an event loop on the creating thread.** The worker's "message processed" signal was queued to the thread that created the `Thread`, so without a running Qt event loop there, `GetQueueSize()` never fell and a bounded queue filled up. A `FullPolicy::TIMEOUT` dispatch also blocked that loop while waiting for space, so it always timed out. It now runs directly on the worker thread.
- **Qt port: a `FullPolicy::TIMEOUT` dispatch was dropped behind a long-running message.** A queue slot was freed only after its message finished running, so a running message kept its slot and a full queue stayed full until it returned. The slot is now freed when the message is taken off the queue, as on the other ports.
- **Qt port: exit hung with a static `Thread`.** A `Thread` destroyed after `main()` returns (a static or global) outlives the `QCoreApplication`, and Qt no longer delivers the queued quit that `ExitThread()` posts, so `ExitThread()` waited forever. With no `QCoreApplication`, it now stops the thread's event loop directly; messages still queued then are not run.
- **Qt port: `ExitThread()` could race a producer blocked on a full queue.** A `FullPolicy::TIMEOUT` dispatch woken during exit checked only `QThread::isRunning()`, so it could wait again, or queue a message behind the quit that never ran (leaving the queue count high for a re-created thread), and could read `m_thread` while `ExitThread()` deleted it. It now stops on the exit flag before touching the thread, and `CreateThread()` resets the queue count.
- **FreeRTOS port: the static-task control block is compiled only with `configSUPPORT_STATIC_ALLOCATION`,** saving its RAM per `Thread` on heap-only configurations.
- **Qt port: `ExitThread()` wrote to freed memory.** On every exit not made from the thread itself, `ExitThread()` cleared the worker's back-pointer after the worker thread had already deleted the worker. It now does that only on a self-exit, where the worker is still alive.
- **Qt port: a message sent while the thread exited could be lost.** A message dispatched during `ExitThread()`, including by a producer blocked on a full `FullPolicy::TIMEOUT` queue, could be queued behind the thread's quit and never run; an async-wait sender then waited out its whole timeout. It is now either run ahead of the quit or dropped, and a blocked producer returns when the thread exits. A re-created thread also starts with an empty queue count. `qt-linux` Test 11 covers this.
- **`RemoteDispatcher` didn't compile on the Qt and NuttX ports.** `RemoteDispatcher.h` left out the Qt and NuttX thread headers, and relied on the thread header to include `Timer.h`, which `QtThread.h` doesn't. `NetworkNode.h` also left out NuttX.

## [2.3.0] - 2026-11-07

### Upgrading to 2.3.0
Check these before upgrading; each can change the behavior, wire format, or build of an existing application. Details are in the Fixed, Changed and Removed entries below.

1. **Wire format break (10-byte header with node ID and protocol version; big-endian serial CRC16).** The message header grows from 8 to 10 bytes (`marker`, `nodeId`, `version`, `id`, `seqNum`, `length`) and serial/STM32 UART framing now sends the CRC16 big-endian. Upgrade all communicating nodes together, and set a unique node ID (`DMQ_NODE_ID` 1–255 or `DmqHeader::SetLocalNodeId()`) on each node when multiple nodes send the same remote IDs. Custom transports should encode/decode headers with `DmqHeader::Encode()`/`Decode()`/`IsValid()`.
2. **On RTOS ports, `Timer::ProcessTimers()` must run in a task, not an ISR.** `ProcessTimers()` now takes a mutex (`Timer::GetProcessLock()`) outside its critical section when snapshotting expired timers' subscribers. Call it from a timer task (the highest-priority one when using the thread watchdog); bare metal can still call it from an ISR.
3. **`ITransport::Receive()` returns `ITransport::RECEIVE_NO_DATA` (1) on a receive timeout** instead of `-1` (negative values now mean a transport error). Callers testing `== 0` / `!= 0` are unaffected; custom `ITransport` implementations should return `RECEIVE_NO_DATA` when a receive times out with no data.
4. **`RemoteInvokeWait()` now waits for the full retry window** when a `RetryMonitor` is attached (`GetInvokeWaitLimit()`, 9.4 s with default settings, up from 2 s), returning only after an ACK or final `OnDeliveryFailed`. Use `RemoteInvokeWaitFor(channel, timeout, args...)` if a shorter wait is needed.
5. **Deleting a ThreadX or Zephyr `dmq::os::Thread` from one of its own callbacks now faults (`DMQ_ASSERT`).** Call `ExitThread()` from the callback and destroy the `Thread` object from another thread.
6. **`dmq::make_tuple_heap()` is removed.** Code that called it directly can use `dmq::store_async_arg<Param>()` per argument (see `make_tuple_heap.h`).

### Added
- **`DMQ_WATCHDOG_HEARTBEAT_DIVISOR`** (default 4, `dmq::WATCHDOG_HEARTBEAT_DIVISOR`): a watched thread's queue wait, and Qt's heartbeat timer, is its watchdog timeout divided by this. It was a literal in each port, 10 on stdlib, POSIX and Qt and 4 elsewhere; every port now uses 4.
- **Start gate (synchronized start).** Optional `dmq::os::StartGate` with `Thread::SetStartGate()` holds a group of threads after `CreateThread()`, ready to receive messages but processing none at any priority, until `gate.Open()` starts them all together. It's for startups where a thread that runs early would post to threads not yet created, or publish before subscriptions are wired. A thread exiting while held runs nothing, and its queued messages are canceled. A held thread isn't reported by its watchdog. Available on every thread port: stdlib, Win32, POSIX, Qt, FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX (`DMQ_HAS_START_GATE`; not bare metal). `test/unit-tests/StartGatePortableTest.h` runs the same gate tests in the desktop unit tests and in each RTOS sample. Cellutron's controller now holds its System, Process, Actuators and Sensors threads until setup is done; before, the Sensors poll timer could publish before subscriptions and the network were connected. See `docs/PORTING.md`.
- **Node IDs.** `DMQ_NODE_ID` (1-255, default 0 = unset) or `DmqHeader::SetLocalNodeId()` sets the ID a process sends in every header; the interop C API adds `DmqInterop_SetNodeId()` (`SetNodeId` in C#, `set_node_id` in Python). Give every node a unique ID when more than one sends the same remote IDs. Cellutron, Pumptron and the interop samples now set one per node.
- **`Participant::SetSeqHistoryExpiry()`** overrides `DMQ_SEQ_HISTORY_EXPIRY_MS` per participant, for a sender whose retry window differs from the default.
- **`serialize` supports `std::array<T, N>`**, the allocation-free choice for message fields on no-heap / fixed-block targets. It's sent exactly like a vector, so one node can use `std::vector<T>` and the other `std::array<T, N>` for the same field; reading fails with `INVALID_INPUT` if the received count isn't `N`. Arrays work nested in other containers and vice versa. `N` above `DMQ_SERIALIZE_MAX_CONTAINER_SIZE` is a compile error. The `serialize` README has a new table on choosing message field types for embedded targets (`std::array`, `xlist`/`xmap`/`xset`, `xstring`; `std::vector` host-side only).
- **`serialize` limits are config settings**: `DMQ_SERIALIZE_MAX_STRING_SIZE` (default 1024), `DMQ_SERIALIZE_MAX_CONTAINER_SIZE` (64) and `DMQ_SERIALIZE_MAX_NESTING` (5), exposed as `dmq::SERIALIZE_MAX_*`. They were fixed in `msg_serialize.h`, so a vector of more than 64 elements couldn't be sent. Lengths are 16 bits on the wire, so the first two are checked at compile time to be at most 65535.
- **Allocation check for `DMQ_ALLOCATOR` builds** (`test/AllocTrack.cpp`, Windows/MSVC): replaces global `new`/`delete`, records the call stack of every global `new` during the test app's 2nd and 3rd pass, and reports them grouped by the library or test code that made them, to find library heap use outside the fixed-block allocator. Off unless `DMQ_ALLOC_TRACK` is defined; see BUILD.md.
- **Thread stack sizes and default priorities on the RTOS ports are now config settings**: `DMQ_FREERTOS_STACK_WORDS` (4096), `DMQ_THREADX_STACK_BYTES` (2048), `DMQ_ZEPHYR_STACK_BYTES` (2048), `DMQ_CMSIS_RTOS2_STACK_BYTES` (2048), `DMQ_NUTTX_STACK_BYTES` (8192), and `DMQ_THREADX_THREAD_PRIORITY` (10), `DMQ_ZEPHYR_THREAD_PRIORITY` (5), `DMQ_NUTTX_THREAD_PRIORITY` (100). Previously these were hard-coded; except for FreeRTOS's `SetStackMem()`, changing a stack size meant editing the library. Defaults are unchanged.
- **`DelegateMQ::DelegateMQ` CMake target.** Pull DelegateMQ in with `FetchContent` or `add_subdirectory`, set any `DMQ_*` options first, then `target_link_libraries(app PRIVATE DelegateMQ::DelegateMQ)`. It's a static library of the selected port and extras sources, and it carries the `DMQ_*` definitions, include directories (third-party ones as `SYSTEM`) and the dependencies DelegateMQ can resolve (ZeroMQ, ThreadX, Qt, `Threads::Threads`). Projects that include `DelegateMQ.cmake` directly can call `dmq_add_library(<name>)` for the same target, one per configuration, and set `DMQ_NO_GLOBAL_DEFINITIONS ON` to keep definitions off the calling directory. See BUILD.md "Consuming as a Dependency".
- **`DMQ_USER_CONFIG_DIR`** adds the directory holding the user config header to the target's include path.
- **Building without CMake: one source file or the whole folder.** For Keil, IAR, STM32CubeIDE, PlatformIO or Makefiles, add `src/delegate-mq` to the include path and either the single file `src/DelegateMQ.cpp` or every `.cpp` under `src/delegate-mq` (but not both). Each library `.cpp` now compiles to nothing unless the options select it, so there's no list of port files to pick from and nothing to exclude. See BUILD.md "Without CMake".
- **`DelegateMQConfig.h` is picked up automatically** when it's on the include path (C++17 `__has_include`), and it can now select ports and options (`DMQ_THREAD_*`, `DMQ_ALLOCATOR`, ...) as well as tune constants, so a build without CMake needs no `-D` flags. `-DDMQ_USER_CONFIG=` still works; define `DMQ_NO_USER_CONFIG` to ignore the file. `DelegateMQConfig_Template.h` lists the options.
- **stdlib thread names on Linux.** `StdlibThread` now names its native thread with `pthread_setname_np()` on Linux (truncated to 15 characters), as the POSIX port does, so `top -H`, `ps -L`, gdb and perf show it. Before, only Windows threads were named.
- **`DMQ_CUSTOM_FAULT_HANDLER`.** Define it when the application provides its own `FaultHandler()`/`WatchdogHandler()`; `port/fault/Fault.cpp` then compiles to nothing instead of having to be left out of the build.

### Fixed
- **`xmalloc()` of a huge size returned a block too small for it.** For a size within the header of `SIZE_MAX`, rounding up to a power of two wrapped to 0, so `xmalloc()` returned a minimum-size block the caller would overrun. `xallocator_get_allocator()` and `xmalloc()` now return `NULL` for such sizes, and `XALLOCATOR`/`stl_allocator` report it with `BAD_ALLOC()`.
- **`xmalloc()` no longer lets `std::bad_alloc` escape its `extern "C"` boundary.** The heap block and a new `Allocator` were allocated with a throwing `new`; they now use `new (std::nothrow)`, and a failure returns `NULL` (reported by the callers' `BAD_ALLOC()`).
- **`DECLARE_ALLOCATOR`: `delete` of a null pointer.** A delete-expression on a null pointer may call `operator delete(NULL)`, which pushed `NULL` onto the free-list and wrote through it. `Allocator::Deallocate()` now ignores `NULL`.
- `xallocator.cpp` used `char_BIT` for `CHAR_BIT` (a type-rename slip; it worked only because the file defines its own fallback). `xqueue.h` includes `<deque>`, which it uses, instead of `<list>`.
- **Start gate lifetime.** A gate destroyed after `Open()` could still be used: `ExitThread()` called `StartGate::Wake()` on it, and a released thread that hadn't run yet still locked it in `Wait()` (in a Debug build `ExitThread()` hung on the freed gate's mutex). A gate may now be destroyed right after `Open()`, e.g. as a local in a startup function: each thread attaches to it in `CreateThread()` and detaches on leaving the gate, `~StartGate()` waits until none is attached, and `ExitThread()` wakes a held thread through the thread's own semaphore instead of the gate. Destroying a gate that is still holding a thread faults, as before. The per-port gate state (`m_startGate`, `m_gated`, `m_gateOpenCount`) is now one shared `dmq::os::StartGateClient`. New portable gate tests cover destroying a gate before and after its threads run.
- **Self-exit from the idle or start handler.** On FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX a self-exit (`ExitThread()` from the thread itself) was handled only from a dispatched message; from the idle handler the loop ended and the code after it wrote `m_selfExitPtr` and signalled the exit semaphore on a `Thread` the handler may have deleted (allowed on FreeRTOS). On every port, a start handler that exited its thread left the thread running code that read the `Thread` after it had signalled `CreateThread()`, by which time the caller may have destroyed it; and a gated thread that exited there never detached from its gate. Both paths now return at once without touching the object, and `ExitThread()` detaches a self-exiting thread from its gate. New tests: freertos-bare-metal Test 10 (idle handler) and a portable gate test (start handler).
- **Win32: leaked start event when `CreateThread()` failed.** The start event created before `::CreateThread()` is now closed on that failure path.
- **Qt: timer intervals over ~24 days overflowed** the `int` passed to `QTimer`; they are now clamped.
- **Thread watchdog false reports.** Three cases reported a healthy thread as hung (`WatchdogHandler()`, a fault by default):
  - **An idle watched thread.** On every port except FreeRTOS, `CreateThread()` set the watchdog timeout after the thread was running, so a thread that read it first saw no watchdog and waited forever on an empty queue. Its heartbeat stopped, and it was reported one timeout later. A thread that received messages regularly reread the timeout and was fine. The timeout is now set before the thread starts.
  - **A thread after `ExitThread()`.** It stayed on the watchdog list until its `Thread` object was destroyed, so `WatchdogCheckAll()` reported it once it stopped. `ExitThread()` now stops the checks once the thread has finished (on a self-exit, at once), on every port; a thread that hangs while `ExitThread()` waits for it is still reported.
  - **Qt.** The Qt port never refreshed the heartbeat when it processed messages or sat idle, so every watched Qt thread was reported after its timeout. The worker now refreshes it on each message and from a heartbeat timer while idle.

  Also, a thread re-created without a timeout no longer keeps its previous watchdog, a `CreateThread()` that fails to create the OS thread leaves the watchdog off, and the timeout and heartbeat members are initialized; a default-constructed `std::atomic` is indeterminate in C++17. New unit tests cover the idle, exited and re-created cases. The Qt fix hasn't been built.
- **RTOS threads wrote to a destroyed `Thread` after `ExitThread()`.** On FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX, the thread cleared an internal pointer after signalling `ExitThread()`, so a `Thread` destroyed as soon as `ExitThread()` returned (e.g. a local) could be written to after it was gone, corrupting the caller's stack or heap. A thread that destroyed itself from its own callback did the same. Seen as stack smashing on cmsis-rtos2-linux.
- **threadx-linux, zephyr-linux and cmsis-rtos2-linux called `Timer::ProcessTimers()` from timer/ISR context**, which it no longer allows on RTOS ports (it takes OS mutexes). threadx-linux faulted at startup in `tx_mutex_create`, and cmsis-rtos2-linux asserted at boot. The timer callback now gives a semaphore and a highest-priority thread calls `ProcessTimers()`. cmsis-rtos2-linux also raises its CMSIS thread limit, as the StartGate tests create about 45 threads that Zephyr's CMSIS layer never reclaims.
- **freertos-linux hung intermittently in `FullPolicy_Timeout_DeliversAll`** (about 1 run in 25). The test sent from a `std::thread`, a host thread FreeRTOS doesn't know about; when it blocked on the full queue, FreeRTOS suspended whichever task was current instead. The test now sends from its own task, as the Zephyr, CMSIS-RTOS2 and NuttX versions already did. threadx-linux had the same `std::thread` and gets the same change.
- **`char_traits` deprecation warnings with GCC and libc++ 17+** (e.g. the NuttX build). libc++ deprecates the generic `std::char_traits` template, and GCC warns on any use of `std::char_traits<char>` even though that specialization is standard. `xstring`, `xstringstream` and `xostringstream` no longer name it: without `DMQ_ALLOCATOR` they are now plain `std::string`/`std::stringstream`/`std::ostringstream` (the same types as before), and with it they use `std::string::traits_type`.
- **cmsis-rtos2-linux ran out of CMSIS thread slots** after the StartGate tests grew to about 106 threads; the cap is now 160. The comment on the mutex/semaphore/message-queue pools wrongly said those slots are never reused.
- **`build_all.py` with missing or old tools.** Without git or CMake a step died with a Python traceback; with a CMake too old for a third-party library or the installed Visual Studio, the run stopped on an error buried in that library's output. A new `scripts/preflight.py` check runs first (in `build_all.py`, and in each step script run on its own): it requires git and CMake 3.20+ and, on Windows, a CMake with a generator for an installed Visual Studio (3.21+ for 2022, 4.2+ for 2026), and says what to install. `03_generate_samples.py` now prints a summary of failed configures with their CMake error and exits non-zero, so `build_all.py` shows that step `FAILED` (it always exited 0). The documented CMake minimum in `docs/BUILD.md` was 3.15, below what the samples need; it now gives the real requirements.
- **Linux NNG and MQTT came from a system install, not `DMQ_DEPS_DIR`.** `External.cmake` hard-coded `/usr/local` on Linux and ignored the copies `02_build_libs.py` installs under `external/<lib>/install`; it also pointed `NNG_LIBRARY_DIR` at the headers folder and `NNG_INCLUDE_DIR` at `include/nng` (the code includes `<nng/nng.h>`), which only worked through default system paths. Linux now uses `DMQ_DEPS_DIR` like Windows, with `<prefix>/include` and `<prefix>/lib` (or `lib64`), and sets `MQTT_LIBRARY_DIR` for the MQTT samples. To use another install, set the new `DMQ_NNG_PREFIX` / `DMQ_MQTT_PREFIX`.
- **Visual Studio startup project for Cellutron, Pumptron and `sample-interop/cpp-server`.** Their solutions defaulted to `ALL_BUILD`, so F5 had nothing to run. The top-level Cellutron and Pumptron solutions now start the GUI, each node's own solution starts that node, and the interop server's starts `InteropServer`. Every other sample already set one.
- **TCP client transports spun a core after a disconnect or bad frame.** `LinuxTcpTransport`/`Win32TcpTransport` in client mode only shut the socket down, which leaves it readable at EOF, so every later `Receive()` failed at once and `RemoteDispatcher`'s receive thread looped at 100% CPU reporting `ERR_TRANSPORT_RECEIVE`. The socket is now closed, and while it's closed `Receive()` waits one receive timeout before returning -1. `Close()` takes the send mutex so it can't race that close. `Win32TcpTransport::Create()` now closes any existing sockets first.
- **Serial and STM32 UART marker sync.** `SerialTransport` missed a frame whose marker followed a stray 0xAA (`AA AA 55`); it now syncs with a sliding previous byte like `Stm32UartTransport`. `Stm32UartTransport::Receive()` returned an error when a stray non-marker byte was followed by an idle line; the frame timeout now applies only to the byte after a 0xAA. `SerialTransport::Create()` closes an already-open port first.
- **`Participant::ProcessIncoming()` reported `ERR_NO_DISPATCHER` for every ACK** arriving on its transport. ACKs are handled inside the transport and are now ignored there, as `RemoteDispatcher` already does.
- **`NetXUdpTransport` ignored `DMQ_UDP_PUB_RECV_TIMEOUT_MS`/`DMQ_UDP_SUB_RECV_TIMEOUT_MS`**; it kept a 1-tick receive timeout. `Create()` now applies them like the other embedded UDP transports.
- **`databus-interop` sample: with both clients running, only one received data.** The server sent `DataMsg` by unicast to port 8000, which reaches only one of several sockets bound there. It now publishes to the multicast group the clients join (best-effort, since the interop clients don't ACK received data; the unused retry layer on it is gone), and its command socket listens on all interfaces.
- **ZeroMQ transport could read past its receive buffer** on a message larger than 4096 bytes (`zmq_recv` returns the full length of a truncated message). Such messages are now dropped; NNG gets the same guard.
- **RELIABLE messages over NetworkNode were never ACKed with the Zephyr and lwIP UDP transports** (and netconn, NetX, NNG, ZeroMQ, STM32 UART, Win32 TCP): they only ACKed when a `TransportMonitor` was set, which NetworkNode's receive socket never has. They now ACK whenever they have a send transport, like Linux/Win32 UDP and serial.
- **NetworkNode on Linux only accepted traffic from the same machine.** Its receive socket was bound to 127.0.0.1; it now listens on all interfaces, as it already did on Windows.
- **TCP transports never recovered from a bad header**: the stream stayed misaligned and every later read parsed mid-frame. A bad header or oversized payload now drops the connection. TCP client `Send()` is now locked (concurrent senders could corrupt frames) and writes in full.
- **UDP-style transports delivered and ACKed truncated or mismatched datagrams.** The header length must now match the datagram exactly (Linux/Win32 UDP and multicast, lwIP, netconn, Zephyr, NetX, NNG, ZeroMQ, MQTT, named pipe), so a damaged message isn't ACKed and is retried.
- **lwIP netconn: any incoming datagram, even a malformed one, redirected where the socket sent** (PUB included). Win32 UDP and NetX updated their reply address before validating the frame. Only a SUB's reply address is updated now, and only from a valid frame.
- **UDP transports held their send lock while waiting in `Receive()`**, so a socket used for both sending and draining ACKs (NetworkNode's peer sockets) delayed every send by up to the receive timeout (50 ms on lwIP/Zephyr). Receive now uses its own mutex (Linux/Win32 UDP, lwIP, netconn, Zephyr, NetX).
- **STM32 UART resynced on a single `0xAA` byte** and waited forever for the rest of a cut-short frame. It now checks both marker bytes and drops a frame whose bytes stop arriving (`DMQ_UART_FRAME_TIMEOUT_MS`). RX ring-buffer overflows are counted (`GetRxOverflowCount()`).
- **Serial transport: `Close()` could free the port under a `Receive()` on another thread.**
- **Socket leaks** when `Create()` failed part-way or was called twice (Linux/Win32 UDP, Linux TCP, lwIP, netconn, Zephyr, NetX).
- **`RemoteInvokeWait()` reported failure for messages its retries then delivered.** It ended on the first TIMEOUT, and waited only 2 s, the same as the ACK timeout, so with a RetryMonitor attached a retry could succeed after the caller was told it failed. It now waits for the final outcome: `true` on the ACK, `false` once the RetryMonitor gives up (`OnDeliveryFailed`); without a RetryMonitor the first TIMEOUT is still final. The default wait covers the whole retry window, `GetInvokeWaitLimit()` (9.4 s with the defaults, up from 2 s), so a caller can block longer than before. New `RemoteInvokeWaitFor(channel, timeout, args...)` sets a shorter limit. `RemoteDispatcher`'s ACK timeout now comes from `DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC` instead of a fixed 2000 ms, and can be set per instance with its new constructor parameter.
- **Timer: `ProcessTimers()` took OS mutexes inside its critical section.** Copying an expired timer's subscribers (`Signal::GetSnapshot()`) locks the signal's mutexes and can allocate, and it ran with interrupts masked. On RTOS ports that's not allowed (a contended mutex hangs). The copy now runs after the critical section, under a separate `Timer::GetProcessLock()` mutex that also keeps the timer from being destroyed in between. On RTOS ports `ProcessTimers()` must therefore run in a task (the highest-priority one for the thread watchdog), not an ISR; bare metal can still call it from an ISR.
- **Timer: a periodic timer that fell behind fired again immediately.** It now skips the missed periods and expires one full period later.
- **`Participant` send thread: a queued send held a raw pointer to the `Participant`.** If the `Participant` was destroyed first, the send called through it. The send now holds only the channel.
- **`crc16.h`'s lookup table was copied into every translation unit that used it** (512 bytes each). It's now `inline constexpr`, one shared copy.
- **`TransportMonitor::Process()` could report the wrong timeouts when called from two threads.** The expired list was a member read after the lock was released; it's now local.
- **`RemoteChannel`: delegates made with `MakeDelegate(..., channel)` could interleave their sends.** They share the channel's stream but didn't take its send mutex. Remote delegates now hold an optional send mutex (`SetSendMutex()`) while serializing and dispatching, and every delegate made from a channel uses the channel's.
- **`RetryMonitor`: a reused sequence number lost the earlier message's retry data**, and a failed re-registration with `TransportMonitor` left the message tracked forever with no further status. The second send is now rejected (`SendWithRetry()` returns -1, so the sender sees `ERR_DISPATCH`; `OnDeliveryFailed` isn't fired, since its (remote ID, sequence number) would name the earlier message), and a failed re-registration abandons and reports the message (once).
- **DataBus duplicate filter dropped real messages from other nodes or a restarted node.** It tracked recent sequence numbers per remote ID only, but each process numbers its own sends. It now tracks them per (sender node ID, remote ID), and entries expire after `DMQ_SEQ_HISTORY_EXPIRY_MS` (default: the sender's retry window), so a restarted node's reused numbers aren't taken for retries. Needs a node ID per node; see the header change under Changed.
- **`serialize`: nested containers didn't compile**, e.g. `std::vector<std::vector<int>>`, `std::map<std::string, std::vector<int>>`, `std::list<std::vector<std::string>>`. Container elements and map values that are themselves vectors, lists, sets or maps now go through the container functions. Each inner container is checked against `DMQ_SERIALIZE_MAX_CONTAINER_SIZE` on its own.
- **`serialize`: a `const char*` read into a `std::string` kept its terminator** (`"hello"` read back as 6 characters ending in `'\0'`). `write(const char*)` no longer sends a terminator, the same as `write(std::string)`, so a string reads back the same whichever way it was sent, and a `std::string` whose data ends in a zero byte keeps it. Reading into a `char[]` adds the terminator (and still accepts one from an older sender). Wire change: a receiver from before this release reading a `const char*` into a `char[]` gets no terminator; the new 10-byte header already requires both ends on this release.
- **`serialize`: a container couldn't be a remote delegate argument**, e.g. `void(std::vector<int>)`. `DmqSerializer` passes arguments as `const`, and the container `write()` overloads only took non-const references, so the call fell to the generic template and failed to compile. They now take `const` references, with non-const forwarders. Containers inside a `serialize::I` message already worked.
- **`serialize`: a `const char*` of 65535 characters or more was sent as an empty string.** The length was cut to 16 bits before the size check. It's now checked first, and the write fails with `STRING_TOO_LONG`.
- **RapidJSON serializer: a message shorter than the previous one failed to parse.** `Write()` rewound the reused stream but didn't empty it, and transports send the whole buffer, so bytes left from the longer message followed the JSON and the receiver reported `ERR_DESERIALIZE`. It now empties the stream like the other serializers.
- **Bitsery serializer didn't build without exceptions** (`-fno-exceptions`), the setting it's recommended for. The `try`/`catch` and `throw` are removed; a read error now sets the stream's failbit, which `DelegateRemote` reports as `ERR_DESERIALIZE`.
- **MessagePack serializer: an empty payload was treated as success**, so the target was called with default-constructed arguments. It now fails the stream (`ERR_DESERIALIZE`).
- **`serialize` (msg_serialize.h): containers of strings.** `std::vector`/`std::list`/`std::set` of `std::string` or `std::wstring`, and `std::map` with string keys or values, didn't compile on write, and on read cast the string to `serialize::I*` (undefined behaviour). Elements and keys that are strings now go through the string functions. Reading any class type that doesn't derive from `serialize::I` is now a compile error, as writing already was.
- **`serialize` (msg_serialize.h): reading into a `char[]` could leave it unterminated.** A string sent as `std::string` has no terminator on the wire, and an empty one left the buffer untouched. The read now always terminates the buffer, and fails with `STRING_TOO_LONG` if there's no room for the terminator.
- **Without `DMQ_ALLOCATOR`, `dmq::xlist` couldn't be serialized by `serialize`.** Its fallback was a class derived from `std::list`, which the list functions don't accept; it's now an alias of `std::list`, as `xmap`/`xset` already were and as `xlist` is under `DMQ_ALLOCATOR`.
- **Bare metal: reading the clock re-enabled interrupts inside the timer's critical section.** `BareMetalClock::now()` ended with an unconditional `cpsie i` (ARM) / `csrsi mstatus` (RISC-V), but `Timer` calls it while holding its interrupt-masking lock (`Timer::Start()`, `ProcessTimers()`), so a timer interrupt could run `ProcessTimers()` against a half-updated timer list. It now saves and restores the previous interrupt state.
- **FreeRTOS: `Timer::ProcessTimers()` couldn't run from an interrupt.** `FreeRTOSClock::now()` used the task-only `taskENTER_CRITICAL()` and `xTaskGetTickCount()`, although `ProcessTimers()` is documented as callable from a hardware ISR on Cortex-M. It now uses `FreeRTOSCriticalSection` and `xTaskGetTickCountFromISR()` in interrupt context. Calls from a task, as in the samples, behave as before.
- **CMSIS-RTOS2: `Priority::HIGH` messages weren't delivered first.** The port relied on `osMessageQueuePut`'s `msg_prio`, which Zephyr's and FreeRTOS's CMSIS-RTOS2 layers ignore. It now uses two queues and a counting semaphore, like the FreeRTOS and ThreadX ports.
- **CMSIS-RTOS2: timeouts and the clock assumed a 1 kHz kernel tick.** Every CMSIS-RTOS2 timeout is in kernel ticks, but the port passed milliseconds straight through, and `dmq::Clock` returned the raw tick count as milliseconds. On a kernel with any other tick rate, sleeps, semaphore waits, `FullPolicy::TIMEOUT` dispatch timeouts, the watchdog heartbeat, idle-handler wakeups and `dmq::util::Timer` all ran fast or slow by the ratio. The port now converts using `osKernelGetTickFreq()` (new `CmsisRtos2Ticks.h`), rounding timeouts up so a short timeout never becomes a non-blocking call. Unchanged on 1 kHz kernels.
- **CMSIS-RTOS2: each `ExitThread()`/`CreateThread()` cycle leaked an RTOS semaphore.** The exit semaphore is now created once and reused.
- **ThreadX: a thread calling `ExitThread()` on itself corrupted the kernel's thread list.** It destroyed its queue and zeroed its own live `TX_THREAD` while still running. The cleanup is now deferred to a later `ExitThread()` from another thread (normally the destructor), which waits for the thread to finish, as on Zephyr.
- **ThreadX: a failed queue creation could leave a queue registered with the kernel**, and a retry then reused its memory. `Create()` now cleans up on failure.
- **FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX:** in builds with exceptions, an invoke failure during a self-exit no longer asserts, matching the no-exceptions branch and the desktop ports.
- **Concurrent calls on one `DelegateAsyncWait` delegate could return each other's values.** Each call's result was stored on the shared delegate with no lock, so when several threads called the same instance (for example a `Signal` emitted from several threads), one caller could get another's return value and the stored `std::any` writes were a data race. Each call's return value is now kept to that call; `operator()` and `AsyncInvoke()` return it directly. `IsSuccess()`/`GetRetVal()` still report the last call on the instance, now under a lock, and a copied delegate starts without a last-call result.
- **`DelegateAsyncWait` reported failure when the timeout expired mid-call.** If the target function had already started when the timeout expired, the caller still waited for it to finish but then returned failure and a default value, discarding the real return value. It now reports success with the return value. A call that hasn't started when the timeout expires is still skipped and reported as failed.
- **`DelegateFunction`'s move constructor and move assignment copied the `std::function`.** Both are `noexcept`, so a copy that had to allocate (a lambda with a large capture) and failed called `std::terminate`. They now move it. `DelegateMember` also moves its object pointer instead of copying it.
- **`TimerDelegate` use-after-free when its connection was dropped with a tick still queued.** The queued message called back into the `TimerDelegate` through a raw pointer, but disconnecting the `Signal` connection (or destroying the delegate) deletes it, so the worker thread then ran on freed memory. Queued ticks now hold only a `weak_ptr` to the target; once the `TimerDelegate` is destroyed, cleared or reassigned, a tick still waiting in the queue is skipped and the target is not called. A tick the worker has already started, or one `ProcessTimers()` queues while `Disconnect()` runs on another thread, still runs, so drain the worker thread before freeing a raw-pointer target. Copy assignment no longer shares state between copies, and moving a `TimerDelegate` no longer copies it.
- **`FetchContent_MakeAvailable(delegatemq)` no longer builds DelegateMQ's test app** inside the consuming project. The tests, samples and tools are only added when DelegateMQ is the top-level project.
- **`DMQ_USER_CONFIG` as a CMake variable now takes effect.** BUILD.md documented `-DDMQ_USER_CONFIG=...`, but nothing read it; it's now passed to the compiler. Samples that already set the `DMQ_USER_CONFIG` compile definition themselves are unaffected.
- **`DMQ_FORCE_OPTIMIZE_DEBUG` on the top-level build reached nothing.** The root `CMakeLists.txt` added it after `test/` had already been configured. `DelegateMQ.cmake` now applies it.
- **`set(DMQ_DATABUS "OFF")` had no effect on desktop builds.** `DelegateOpt.h` turns DataBus on by default for Windows/Linux/macOS unless `DMQ_DATABUS_OFF` is defined, and CMake never defined it. Setting `DMQ_DATABUS` or `DMQ_DATABUS_TOOLS` to OFF now defines `DMQ_DATABUS_OFF` / `DMQ_DATABUS_TOOLS_OFF`. Builds that leave them at their defaults are unchanged.
- **Including `DelegateMQ.cmake` overrode the project's C++ standard.** `Common.cmake` always set `CMAKE_CXX_STANDARD` to 17, so a project that chose C++20 (`set(CMAKE_CXX_STANDARD 20)` or `-DCMAKE_CXX_STANDARD=20`) was compiled as C++17. It now keeps C++17 or newer as chosen, and still raises an unset or older standard to C++17. Projects that set none are unchanged.
- **RTOS ports failed to compile with some bare-metal GCC toolchains** (e.g. Arm GNU Toolchain 14): their standard library has `<mutex>` but no `std::scoped_lock` without thread support, so `dmq::ScopedLock` didn't exist. `dmq::ScopedLock` now falls back to its own implementation (locks in address order) when `std::scoped_lock` isn't available.
- **Including `DelegateMQ.cmake` twice in one directory, or in a parent and then a subdirectory, mixed the two configurations.** Source lists, third-party include paths and libraries, and defaults DelegateMQ filled in (such as `DMQ_DATABUS`, derived from `DMQ_THREAD`) carried over from the first include. Each include now starts from its own options. Sibling directories, as in Cellutron and Pumptron, were not affected.
- **`bare-metal-arm` failed to link with newlib 4.4** (`undefined reference to '_getentropy'`, via libstdc++'s `std::random_device`). `retarget.c` now provides a `_getentropy` stub.
- **`stm32-freertos`: Release configuration didn't build, and `RUN_NETWORK_TESTS` ran out of newlib heap.** The STM32CubeIDE Release configuration now matches Debug's include paths, C17/C++17 standard, RTTI and DelegateMQ defines, and global `operator new`/`delete` route to `pvPortMalloc`/`vPortFree`.
- **`atfe-armv7m-bare-metal` only configured on Windows.** The ATfE tool paths no longer hardcode `.exe`; the README shows the Linux toolchain.
- **`-Wsign-compare` in `ZephyrUdpTransport.h`**, plus `-Wreorder`/`-Wdouble-promotion` warnings in the `zephyr-udp-serializer` and `databus-zephyr` samples.
- **`DMQ_STRICT` on MSVC failed in third-party headers** (msgpack under `/analyze`). Strict mode now adds `/analyze:external-`, so code analysis covers DelegateMQ's own code, not external headers.

### Changed
- **`ITransport::Receive()` returns `ITransport::RECEIVE_NO_DATA` (1) when nothing arrived** within the receive timeout, instead of -1. Negative still means an error, so RemoteDispatcher no longer reports idle timeouts as `ERR_TRANSPORT_RECEIVE`. Callers that test `== 0` / `!= 0` are unaffected; a custom transport should return `RECEIVE_NO_DATA` for a timeout.
- **Serial framing (serial and STM32 UART transports): the CRC16 is now sent big-endian**, like the header. Update both ends together.
- **Embedded transport sizes and timeouts are config settings**: `DMQ_UDP_BUFFER_SIZE`, `DMQ_UDP_PUB_RECV_TIMEOUT_MS`, `DMQ_UDP_SUB_RECV_TIMEOUT_MS` (lwIP, netconn, Zephyr, NetX) and `DMQ_UART_RX_RING_SIZE`, `DMQ_UART_MAX_PAYLOAD`, `DMQ_UART_TX_TIMEOUT_MS`, `DMQ_UART_FRAME_TIMEOUT_MS` (STM32 UART). The netconn SUB receive timeout changes from 200 ms to the shared 2 s default. Unused `<iostream>`/`<sstream>` includes are gone from the lwIP, netconn and Zephyr transports.
- **Wire format: the message header is now 10 bytes, not 8, and carries the sender's node ID and a protocol version** (`marker`, `nodeId` (1 byte), `version` (1 byte), `id`, `seqNum`, `length`). Not compatible with earlier releases: upgrade every node, and any custom code that builds or parses headers (the Python sample client and the Wireshark dissector are updated). Set each node's ID with `DMQ_NODE_ID` (1-255) or `DmqHeader::SetLocalNodeId()`; 0 means unset. Receivers drop frames with another protocol version. Transports now encode and decode the header with `DmqHeader::Encode()`/`Decode()`/`IsValid()` instead of each doing it by hand, so a custom transport should do the same.
- **C# interop: MessagePack NuGet package 2.5.302 → 2.5.303** in `interop/csharp` and the `databus-interop` C# client, for [GHSA-qhrr-8q5h-9q3h](https://github.com/advisories/GHSA-qhrr-8q5h-9q3h) (nested arrays could get past buffer length limits when reading untrusted data). The C++ library and other interop languages don't use this package.
- **`serialize`: reading a `std::vector` reserves the received count first**, so it makes one allocation of the final size instead of regrowing per element (a 64-element vector previously reallocated several times as it grew, each time a larger block size under `DMQ_ALLOCATOR`).
- **Serializer adapters no longer catch and print errors.** The cereal, MessagePack, RapidJSON and `serialize` adapters caught each exception, printed it to `std::cerr` (RapidJSON parse errors to `std::cout`) and rethrew it. `DelegateRemote` already reports these as `ERR_SERIALIZE`/`ERR_DESERIALIZE`/`ERR_DESERIALIZE_EXCEPTION`, so the adapters now just let the stream state or exception through. `msg_serialize.h` includes `<istream>`/`<ostream>` instead of `<iostream>`.
- **DataBus async subscribers no longer allocate from the global heap on every publish (GCC/Clang toolchains).** A subscription with a thread was wrapped in a `std::function`, and every publish copies the subscriber's delegate to queue the call. libstdc++ and libc++ keep a callable inside `std::function` only if it's trivially copyable, which DelegateMQ's delegates aren't, so each publish to each async subscriber made one global-heap allocation outside `DMQ_ALLOCATOR`. MSVC keeps it inline, which is why desktop builds didn't show it. The subscriber is now held once per subscription in a fixed-block-allocated object, and each publish copies only a shared pointer to it. Measured on the Pumptron STM32F4 firmware during its self-test: 22 global allocations (one per command) before, 0 after; no heap allocations at all once the allocator pools have warmed up. Cost: about 750 bytes of flash per async-subscribed topic type. Applies to the DataBus spy (monitor) subscription too. Filtered (`SubscribeFilter`) and rate-limited (`minSeparation`) subscriptions now bind their filter and rate limiter directly as member delegates instead of through a lambda in a `std::function`, which saves 808 bytes of flash in Pumptron.
- **`DelegateAsyncWait` no longer allocates from the global heap for large return values.** Return values were kept in `std::any`, which heap-allocates anything over its small buffer with global `operator new`, bypassing `DMQ_ALLOCATOR` on every call. They're now stored typed (`std::optional<RetType>`) in the delegate. Found with the new allocation check.
- **Deleting a ThreadX or Zephyr `dmq::os::Thread` from one of its own callbacks now faults** (`DMQ_ASSERT`). The destructor would otherwise free the stack and control block the thread is still running on; unlike FreeRTOS, these kernels can't free them afterwards. Call `ExitThread()` from the callback and delete the object from another thread.
- The RTOS ports and the embedded transports (`arm-lwip-udp`, `arm-lwip-netconn-udp`, `zephyr-udp`) use `dmq::LockGuard` instead of `std::lock_guard`, as the rest of the library does, and no longer include `<mutex>`.
- **Async delegates copy each argument once.** A non-blocking async call used to heap-copy every argument, by-value ones too, then copy it again into the message, and the destination thread copied the argument tuple once more before the call: 4 copies of a by-value argument (9 for two), 2 of a `const T&` one. By-value arguments are now stored in the message and moved into the call, and only the data behind reference and pointer arguments is heap-copied: 1 copy per argument, the same as a synchronous call, and no heap allocation for by-value arguments. `DelegateAsyncWait` calls drop from 2 copies to 1. Reference and pointer arguments are still copied when the call is made, so later changes by the caller don't reach the target.
- **Smaller code for async delegates.** Building the argument tuple no longer instantiates a recursive template per argument list, and the destination thread calls the target directly instead of through `std::tuple_cat` and a virtual call (which also removes the `m_sync` flag that call needed). In the `stm32-freertos` sample (`-O0`, `DMQ_ALLOCATOR`), flash shrinks by 8,304 bytes (3.0%) with `RUN_SIMPLE_TESTS` and 44,256 bytes (8.5%) with `RUN_NETWORK_TESTS`; optimized builds shrink less.
- **Library `.cpp` files no longer stop the build when compiled without their option.** The OS port files (`FreeRTOSThread.cpp`, `StdlibThread.cpp`, ...) used to `#error` unless their `DMQ_THREAD_*` was defined; they, the allocator, `RemoteDispatcher.cpp` and `ThreadMonitor.cpp` now compile to nothing instead. An existing project that lists exactly the right files builds the same code as before. `Win32Thread.cpp` still errors if `DMQ_THREAD_WIN32` is selected on a non-Windows target.
- **Option selection moved to `delegate/DelegateSelect.h`**, included first by `DelegateOpt.h`. It holds the user config include, port auto-detection and derived macros (`DMQ_HAS_THREADING`, `DMQ_THREAD_IS_EMBEDDED_RTOS`, ...), and nothing else. Code that includes `DelegateOpt.h` is unaffected.
- **A transport is now auto-selected when none is defined**, matching CMake's defaults: `DMQ_TRANSPORT_WIN32_UDP` on Windows, `DMQ_TRANSPORT_LINUX_UDP` on Linux, and `DMQ_TRANSPORT_NONE` for RTOS, bare-metal and other targets. Previously a build without CMake that defined no transport hit `#warning "Transport implementation not found."`, which MSVC rejects as an error. CMake builds always pass a transport and are unaffected.
- **Defining two thread ports, serializers or transports is now a compile error** (`More than one DMQ_THREAD_* is defined...`). This catches a `DelegateMQConfig.h` that picks a different port than CMake's `DMQ_THREAD`/`DMQ_SERIALIZE`/`DMQ_TRANSPORT`, which used to build one port's sources while the headers used another.
- **Builds without CMake now default to `DMQ_THREAD_NONE` on bare-metal compilers** (previously no thread port was selected, and `DelegateMQ.h` hit `#warning "Thread implementation not found."`). DataBus is no longer turned on by default under `DMQ_THREAD_NONE`, matching CMake; before, a `DMQ_THREAD_NONE` build on a desktop compiler without CMake failed to compile `ThreadMonitor.cpp`.
- The root `CMakeLists.txt` now requires CMake 3.16, matching `test/` and the samples. Under 3.10 policy, `option()` ignored a parent project's plain `set(DMQ_STRICT ON)`.

### Removed
- **`dmq::make_tuple_heap()`.** It heap-copied every argument and is no longer used by the library; `DelegateAsyncMsg` uses `dmq::store_async_arg()` instead. Code that called `make_tuple_heap()` directly can build its tuple with `store_async_arg<Param>()` per argument (see `make_tuple_heap.h`). The helper `dmq::tuple_append()` is now `dmq::heap_copy_arg()`, which returns the heap copy itself rather than a tuple holding it.

## [2.2.0] - 2026-10-19

### Upgrading to 2.2.0
Check these before upgrading; each can change the behavior or build of an existing application. Details are in the Fixed and Changed entries below.

1. **`DataBus::AddRelayTopic` no longer sends relayed data back over the link it arrived on.** If an application relied on that to send a relayed topic back out the inbound participant, call `AddRemoteTopic` on that participant explicitly.
2. **`MqttTransport::Create(type)` now connects to `tcp://localhost:1883`** instead of a public broker. Pass the broker URI to `Create(type, address, topic, clientId)` to keep using another broker.
3. **Serializer header paths changed.** Code that includes a serializer header directly must use the new name (e.g. `port/serialize/msgpack/MsgpackSerializer.h`). Nothing to do if you get the serializer through `DelegateMQ.h` and `DMQ_SERIALIZE`.
4. **Example ecosystem only:** the scripts moved to `scripts/` and third-party repos now live in `<repo>/external/`. Rerun `01_fetch_repos.py` and `02_build_libs.py`, then delete the old folders next to the repo.

### Added
- **`databus-relay` sample** (`example/sample-projects/databus-relay/`). Three nodes across two different links: sensor ↔ gateway over UDP, gateway ↔ monitor over local IPC (Windows: named pipe / Linux: Unix domain socket). The gateway relays topics both ways with `AddRelayTopic` and uses two transports in one process. `03`/`04`/`05_*.py` now also handle projects with more than two node apps (`MULTI_NODE`).
- **`LinuxUdsTransport`** (`port/transport/linux-uds/`, `DMQ_TRANSPORT_LINUX_UDS`). Local IPC over a Unix domain datagram socket (`AF_UNIX`, `SOCK_DGRAM`), the Linux counterpart to `Win32PipeTransport`. Duplex, so one participant can send and receive over it. Each end binds its own socket path and sends to the peer's, so there's no start order. Receive is non-blocking by default (`SetRecvTimeout()` to wait) and never holds up a concurrent `Send()`. A stale socket file from a crashed process is reused, but `Create()` fails rather than take over a path a live process is still bound to, and never removes a non-socket file. The socket file is removed on `Close()`. Oversized or malformed datagrams are dropped rather than truncated.
- **Link health in `dmq-monitor`.** A new Health column shows each node's last 10 seconds as OK, WARN (ACK timeouts and retries, a growing retry backlog, or DataBus errors) or FAIL (messages abandoned after all retries, or sends rejected at the unacknowledged-message cap), with totals, recent counts and the latest event in the detail panel. `NodeBridge` counts DataBus errors automatically; `NodeBridge::Watch()` adds link health from a `NetworkNode`, `TransportMonitor` or `RetryMonitor`. The counters travel in a new `NodeHealthPacket` (`PacketType::Health`), so older monitors and nodes keep working with newer ones. Cellutron watches its `NetworkNode` on all three nodes. Also new: `dmq-monitor --once <sec>` prints a text snapshot and exits, `r` resets health counts, and `dmq-target --health-demo` cycles through OK, WARN and FAIL.
- **`NetworkNode::Receive()` can relay** (`Delivery::RELAY`). A gateway node with several links (for example Ethernet peers and serial MCUs on one DataBus) can now forward a received topic out every other link that `Send()`s it, without dropping down to bare `Participant`s and `AddRelayTopic`. The default, `Delivery::LOCAL`, is unchanged: this node's subscribers only. Pumptron's `SerialLink::Receive()` takes the same option. DATABUS.md "Relay Nodes" and "Topologies" now cover multi-link nodes, global topic names, the shared `DMQ_MAX_PARTICIPANTS` budget (two per peer), and where to find a serial link wrapper.

### Fixed
- **FreeRTOS Windows simulator ran ~15× slow in the background** (Cellutron controller/safety, Pumptron sim controller, `databus-freertos` server). Windows 11 ignores `timeBeginPeriod(1)` for a process whose window is hidden or minimized, so the port's `Sleep(1)` tick took ~15.6 ms. Heartbeats arrived every ~15 s and peers reported the node lost until its console was brought to the front. Each simulator `main()` now opts out of timer-resolution and EcoQoS throttling.
- **`dmq-monitor` keeps same-named nodes on different machines apart.** `NodeBridge` never filled in its IP address, so the IP column was blank and nodes with the same name on two machines shared one row, alternating between them. It now reports the multicast interface or the machine's first non-loopback IPv4 address.
- **`dmq-target` now shows up in `dmq-monitor`.** It never called `Timer::ProcessTimers()`, so `NodeBridge`'s heartbeat timer never fired.
- **Transports reject oversized payloads instead of sending them with a wrapped length.** `Send()` narrowed the payload length to `uint16_t` before the buffer-size check, so a payload over 64 KB wrapped (e.g. 65,540 bytes became 4) and went out as a truncated message. The size is now checked first in the Linux/Win32 UDP and TCP, Win32 pipe, ZeroMQ, NNG, MQTT, lwIP and Zephyr transports. Both `MulticastTransport`s and `Stm32UartTransport` had no size check and now reject payloads larger than their buffer (for the STM32 UART, payloads its own receiver already rejected).
- **`DataBus::AddRelayTopic` no longer echoes relayed data back to its source.** It also registered the topic as outgoing on the participant the data arrived on. On a duplex link (one participant that both receives and is registered with `AddParticipant`), every relayed message was sent back out the link it came in on. It now forwards only to participants that list the topic as outgoing with `AddRemoteTopic`. If an application relied on the old behavior to send a relayed topic back over the inbound link, it must call `AddRemoteTopic` on that participant explicitly.
- **`Win32PipeTransport` client (`Type::PUB`) can now receive.** The client handle was left in byte-read, blocking mode. `Receive()` could merge two queued messages into one read and lose the second, and an idle read blocked a concurrent `Send()`. The client now uses the same message-read, non-blocking mode as the server.

### Changed
- **Serializer headers renamed after their library**, matching `port/os/` and `port/transport/`: `port/serialize/<lib>/Serializer.h` is now `BitserySerializer.h`, `CerealSerializer.h`, `MsgpackSerializer.h`, `RapidJsonSerializer.h`, and `serialize/DmqSerializer.h` for the built-in serializer. Class and namespace names are unchanged (`dmq::serialization::<lib>::Serializer`). Each header also has its own include guard; four of them shared `SERIALIZER_H`, so including two serializers in one file silently dropped the second. **Upgrading:** nothing to do if you get the serializer through `DelegateMQ.h` and `DMQ_SERIALIZE`; update the path if you include a serializer header directly.
- **Diagnostic tools are copied to `tools/bin/` after every build.** `dmq-spy`, `dmq-monitor`, `dmq-thread` and `dmq-target` are now always at `tools/bin/<tool>` (gitignored), whether they're built in `tools/build`, from the repo root with `DMQ_TOOLS=ON`, or in any configuration; the last build wins. The Cellutron and Pumptron run scripts launch them from there. Build output itself is unchanged.
- **`MqttTransport` no longer uses a public broker by default.** The broker address (`tcp://broker.hivemq.com:1883`), topic (`Delegate_MQTT`) and client IDs (`DelegatePub`/`DelegateSub`) were hardcoded, so every user's traffic went to a public broker where anyone could read it and publish into it. `Create(type, address, topic, clientId)` now takes all three. The defaults are a local broker (`tcp://localhost:1883`), the topic `dmq/delegate`, and a random client ID per instance, so two apps no longer disconnect each other by sharing one. **Upgrading:** existing `Create(type)` calls still compile but now connect to `localhost`; pass the broker URI explicitly to keep using another broker. A failed `Create()` now also releases the Paho client instead of leaking it. The `mqtt-rapidjson` sample takes the broker URI as an optional argument.
- **Example-ecosystem scripts moved to `scripts/`; new `build_all.py` at the repo root runs them in order.** Run them as `python3 scripts/01_fetch_repos.py` and so on, or `python3 build_all.py` (`--from N` resumes at step N; `--clang`, `--clean` and `--deps-dir` are passed through). `01_fetch_repos.py` and `02_build_libs.py` now exit non-zero when a clone or build fails, and `05_run_samples.py` takes `--yes` to skip its prompt.
- **Third-party repos now live in `external/` inside the repo.** `01_fetch_repos.py` clones into `<repo>/external/` (gitignored) instead of next to the repo, and `02_build_libs.py` builds there. All CMake files find them through one variable, `DMQ_DEPS_DIR` (new `src/delegate-mq/DepsDir.cmake`, included by `DelegateMQ.cmake`), which can be overridden with `-DDMQ_DEPS_DIR=<path>`, the `DMQ_DEPS_DIR` environment variable, or `--deps-dir` on the scripts, for example to share one copy between clones. The old layout is no longer found: rerun `01_fetch_repos.py` and `02_build_libs.py`, then delete the old folders. Note that `git clean -xdf` now removes `external/`; use `git clean -xdf -e external` to keep it.
- **Documented: a slot can still run after `Disconnect()` returns.** `Signal`, `MulticastDelegateSafe` and `UnicastDelegateSafe` invoke outside their lock, so a call already in progress on another thread can still invoke a slot once after `Disconnect()` (or removal) returns, and an async delegate's message already queued on its thread still runs. Disconnecting alone does not make a slot's target safe to destroy in multithreaded code; [SIGNALS.md](docs/SIGNALS.md#disconnect-and-calls-already-in-progress) shows how to guard it. Behavior is unchanged; the 1.1.5 entry below is reworded to match.

## [2.1.0] - 2026-09-28

### Upgrading to 2.1.0
Check these before upgrading; each can change the behavior of an existing application. Details are in the Changed and Added entries below.

1. **`ExitThread()` now runs queued messages first (all ports except stdlib).** Win32, POSIX, Qt, FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX previously skipped messages still queued at `ExitThread()`; they now invoke them first (`ExitPolicy::DRAIN`, as stdlib always did). This compiles unchanged but runs differently at shutdown. If an application destroys objects before calling `ExitThread()`, a queued message could now run against a destroyed object. Either call `ExitThread()` before destroying what its messages target, or pass `ExitThread(dmq::ExitPolicy::DISCARD)` to keep the old skip-pending behavior.
2. **RTOS ports fault above 16 concurrent threads.** `dmq::ThisThread::GetCurrent()` registers every running `dmq::os::Thread` in a fixed table on FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX. An application with more than 16 running at once faults when the 17th thread starts; raise `DMQ_MAX_CURRENT_THREADS` in your `DelegateMQConfig.h`. Desktop ports are unaffected.
3. **Interop C API (source and binary break).** Code calling `DmqInterop.dll` directly must pass `uint16_t* seqNum` to `DmqInterop_Send()` and add a `void* context` first parameter to every callback, registered with `DmqInterop_Register*(..., context)`. The Python and C# wrappers are already updated; their `send()` now returns the sequence number.
4. **`tools/bridge` source paths.** Projects that compile `SpyBridge.cpp` or `NodeBridge.cpp` must use `tools/bridge/spy/` and `tools/bridge/node/` and add those plus `tools/net` to their include directories. `#include` lines are unchanged.
5. **Tools bind UDP ports exclusively.** A second `dmq-spy`, `dmq-monitor` or `dmq-thread` on the same port now exits with an error instead of silently receiving nothing. Pass `--multicast` where several listeners must share a port.

Custom `IThread` implementations need no changes. Two optional additions: create a `dmq::CurrentThreadScope` in the worker loop so `ThisThread::GetCurrent()` finds the thread, and call `DelegateMsg::Cancel()` on any queued message the thread drops so a blocked sender is released (see [PORTING.md](docs/PORTING.md#dmqexitpolicy-shutdown)).

### Added
- **Pumptron example** (`example/pumptron/`) — pump controller on a real STM32F4 Discovery (FreeRTOS) or the FreeRTOS simulator, with an FTXUI console over a serial DataBus link. Includes end-to-end error handling, an IWDG hardware watchdog, and crash dumps kept in `.noinit` RAM and delivered to the console on the next boot.
- **`dmq-spy` command-line modes** — `--echo`, `--hz`, `--bw` print to stdout for SSH sessions and pipelines. `--bw` measures Spy-feed bytes, not transport bytes.
- **`dmq-spy --log-format text|csv|json`** — CSV and JSON Lines records carry both host and source timestamps.
- **`dmq-spy --plotjuggler`** — streams numeric values to PlotJuggler's UDP Server for live plots. `run_cellutron.py --plotjuggler` passes it through.
- **Pumptron `--spy`** — the GUI mirrors its DataBus, including all traffic received from the controller over serial, to `dmq-spy` (and PlotJuggler). `run_pumptron.py --spy` / `--plotjuggler`.
- **JSON bridges** — `tools/bridge/common/JsonTopics`, a shared layer where an application declares once which DataBus topics to expose as JSON (`Expose<T>`) and which commands to accept (`Accept<T>`); each bridge serves that same set. First bridge: **MQTT gateway** (`tools/bridge/mqtt/MqttBridge`) — per-topic MQTT topics with JSON payloads, opt-in inbound commands (`<topic>/set`), retained state re-published on reconnect, `<prefix>/online` Last Will and background reconnect. `MqttBridge.cmake` builds Paho MQTT C from the workspace. New `example/sample-projects/databus-mqtt-gateway` demo; Pumptron `--mqtt` / `--mqtt-control` puts the pump on a broker.
- **Wireshark dissector** (`tools/wireshark/dmq.lua`) — decodes the DelegateMQ header on UDP/TCP, flags ACKs and malformed frames, and labels remote IDs from a per-project topic table (Cellutron example included).
- **Interop reliable sends** — `DmqInterop.dll` now uses the `ReliableTransport`/`RetryMonitor`/`TransportMonitor` stack (retry on by default). New `DmqInterop_SetReliability()` and `DmqInterop_RegisterStatusCallback()` (`ACKED`/`TIMEOUT`/`DELIVERY_FAILED` per sequence number). Python and C# wrappers expose both.
- **`Stm32UartTransport::OnRxError()`** — ISR-safe recovery after a UART error aborts interrupt-driven reception.
- **`DelegateMQ.cmake` cross-compiled FreeRTOS support** — caller-selected `FREERTOS_PORT_DIR`/`FREERTOS_HEAP` (e.g. `GCC/ARM_CM4F`).
- **Thread start, exit and idle handlers** — every `dmq::os::Thread` port gains `SetStartHandler()`, `SetExitHandler()` and `SetIdleHandler()`, run on the worker thread: before the first message (`CreateThread()` returns after it), after the last message (`ExitThread()` returns after it), and when the queue has been empty for an interval (`DMQ_THREAD_IDLE_INTERVAL`, default 100 ms). Setters fault if the thread is running. See [PORTING.md](docs/PORTING.md#start-exit-and-idle-handlers).
- **`dmq::ThisThread::GetCurrent()`** — returns the `IThread` whose worker is the calling thread, or `nullptr`. Every `dmq::os::Thread` port registers its worker; a custom `IThread` registers with `dmq::CurrentThreadScope`. Desktop ports use `thread_local`; RTOS ports use a fixed registry sized by `DMQ_MAX_CURRENT_THREADS` (default 16; exceeding it faults). New `ReplyToCaller` sample shows an async service replying on the requesting thread. See [PORTING.md](docs/PORTING.md#current-thread), including ISR limitations on ThreadX, CMSIS-RTOS2 and non-ARM FreeRTOS.
- **`dmq::ExitPolicy`** — `ExitThread(ExitPolicy::DRAIN)` (default) invokes every queued message before exiting; `ExitThread(ExitPolicy::DISCARD)` finishes only the running message. See [PORTING.md](docs/PORTING.md#dmqexitpolicy-shutdown).
- **`DelegateMsg::Cancel()`** — a thread that discards a queued message calls it; an async-wait sender blocked on the message is released and sees the call fail.

### Changed
- **`ExitThread()` drains the queue on every port (behavior change)** — previously only stdlib ran messages still queued at `ExitThread()`; Win32 and POSIX skipped pending normal-priority messages, the RTOS ports stopped as soon as exit was requested, and Qt dropped pending events. All ports now default to `ExitPolicy::DRAIN`, so queued messages run during shutdown: call `ExitThread()` before destroying objects they target, or pass `ExitPolicy::DISCARD` for the old skip-pending behavior.
- **`dmq::ThisThread` is defined per port** — each `port/os/<os>/<Os>ThisThread.h` is now the complete type (new `port/os/stdlib/StdlibThisThread.h` for the desktop ports) and `DelegateOpt.h` aliases it. `sleep_for()` accepts any `std::chrono::duration` on every port.
- **Interop C API (breaking)** — `DmqInterop_Send()` takes a `uint16_t* seqNum` out parameter; every callback now receives a caller-supplied `void* context` as its first argument (registered with `DmqInterop_Register*(..., context)`); the error callback is `(context, code, remoteId, msg)` with `DmqErrorCode` values. Python/C# `send()` return the sequence number.
- **Tools bind UDP ports exclusively** unless `--multicast` is given, and open sockets before the TUI starts: a second `dmq-spy`/`dmq-monitor`/`dmq-thread` on a busy port now exits with an error instead of silently receiving nothing.
- **Unknown `dmq-spy` options are rejected** (previously ignored).
- **`tools/bridge` reorganized into subdirectories (breaking paths)** — `spy/` (`SpyBridge`), `node/` (`NodeBridge`, `NodeInfoPacket.h`), `mqtt/` (`MqttBridge`), `common/` (shared JSON layer). Projects that add `tools/bridge/SpyBridge.cpp` or `tools/bridge/NodeBridge.cpp` must use the new paths and include directories (`tools/bridge/spy`, `tools/bridge/node`, plus `tools/net`); header names and `#include "SpyBridge.h"` are unchanged. All in-repo examples and samples are updated (the DataBus samples' stale `tools/src` include is also corrected to `tools/net`).

### Fixed
- **RTOS threads could not be restarted** — on FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2 and NuttX, `CreateThread()` after `ExitThread()` started a worker that exited immediately; the exit flag is now reset.
- **Blocked senders hung on messages dropped at thread exit** — an async-wait caller (e.g. `WAIT_INFINITE`) whose message was still queued when its thread exited waited out its full timeout. Dropped messages are now canceled, releasing the caller with a failure.
- **Interop sends were never tracked** — since transports stopped calling `TransportMonitor::Add()`, the DLL's monitor was inert and ACKs on the send socket were never read, so Python/C# sends had no delivery feedback or retry.
- **Interop C# callbacks could terminate the process** — an exception (e.g. MessagePack deserialization) escaping a native callback now reports `DMQ_ERR_CALLBACK` instead. Python callback exceptions are reported the same way instead of being discarded.
- **`ThreadMonitor` failed to compile in embedded DataBus builds** with tools disabled; now gated on `DMQ_DATABUS_TOOLS`.
- **`dmq-spy` log lost its tail** when the window was closed; logs now flush every second.

## [2.0.4] - 2026-09-20

### Added
- **New `DMQ_THREAD_POSIX` port** (`port/os/posix/PosixThread.h`/`.cpp`) — a raw POSIX (`pthread_create`/`pthread_mutex_t`/`pthread_cond_t`) implementation of `dmq::IThread`, filling the gap where `stdlib` (std::thread) and `win32` (native Win32 API) existed but no native-API port for Linux/POSIX did. `dmq::Mutex`/`ConditionVariable`/`Clock`/`ThisThread` are not reimplemented — they still resolve to `std::mutex`/`std::condition_variable`/`std::chrono::steady_clock`/`std::this_thread`, already thin wrappers over the same primitives under glibc; `PosixThread` is the only file this port adds. Condition variables use `pthread_condattr_setclock(CLOCK_MONOTONIC)` so timed waits are immune to wall-clock jumps — a glibc/musl extension, not implemented on Darwin/macOS, so this port is Linux-only for now (its CMakeLists.txt enforces this). New `example/sample-projects/posix-linux/` sample runs the same test battery as `threadx-linux`/`freertos-linux`/`zephyr-linux`/`cmsis-rtos2-linux`/`nuttx-sim` — including the full `FullPolicy`/`TimerDelegate` suite ported unchanged from `test/unit-tests/` — and, unlike those RTOS simulators, exits normally (status 0) instead of looping forever, since POSIX has no persistent kernel to keep alive. Also fixes a real gap this surfaced: `DelegateOpt.h`'s "no threading model defined" auto-detection fallback didn't know about `DMQ_THREAD_POSIX`, so it would also auto-define `DMQ_THREAD_STDLIB` alongside it, pulling in both `StdlibThread.h` and `PosixThread.h` in the same translation unit and conflicting on the `dmq::os::Thread` alias.
- **Cellutron ThreadX support** — the Controller and Safety nodes now build against ThreadX as well as FreeRTOS, selected with one switch (`cmake -DCELLUTRON_RTOS=THREADX`, or `run_cellutron.py --threadx`; Linux only). Only the RTOS kernel-init/task-creation glue differs (`main.cpp` vs. new `main_threadx.cpp`) and one `#ifdef` in `Constants.h` for ThreadX's inverted thread-priority scale — `Process`, `System`, `Actuators`, `Sensors`, and the state machines are unchanged, demonstrating DelegateMQ's OS isolation end-to-end. `03_generate_samples.py`/`04_build_samples.py` configure and build the `build-threadx/` variant automatically on Linux. Also fixes a `src/delegate-mq/External.cmake` bug where two executables in one CMake project (Controller + Safety) both linking ThreadX collided on the same `add_subdirectory()` binary directory. Note: the vendored ThreadX Linux/GNU simulation port has a known kernel deadlock once 2+ ThreadX threads are concurrently alive, which Cellutron's Controller (7 threads) reliably triggers — see `CELLUTRON.md`'s "Known Limitation" note; this is a vendor bug, not a DelegateMQ one.
- **`docs/SAFETY.md`** — an informational, MISRA-C++-style self-assessment of the library. Explicitly not a compliance claim (DelegateMQ isn't designed or certified to any safety standard); documents what's already handled well (allocator gating, no banned CRT functions, MI shape, etc.) versus areas a safety-conscious integrator would need to address (`dynamic_cast` in delegate equality, C-style casts in the allocator core and in-scope embedded ports, `xallocator`'s default heap-backed pool mode), including a concrete approach for eliminating the `dynamic_cast` dependency if a target requires `-fno-rtti`.
- **Unit test coverage for `NetworkNode` and `ReliableTransport`** (`test/unit-tests/databus/NetworkNodeTests.cpp`, `test/unit-tests/dispatcher/ReliableTransportTests.cpp`) — both previously had zero dedicated tests. Writing the `NetworkNode` test surfaced a real use-after-free (see Fixed, below).
- **DataBus topology documentation** (`docs/DATABUS.md`) — a `## Topologies` section covering the range of network shapes `NetworkNode` supports (point-to-point, star, mesh, broadcast/multicast, heterogeneous-transport gateways, broker-mediated), grounded in the actual `ITransport` implementations available and the `cellutron` sample's own mesh topology.

### Fixed
- **`NetworkNode::Stop()` use-after-free** — it disconnected `m_recvConn` (freeing the `TimerDelegate` connected in `Start()`) *before* `m_thread->ExitThread()` drained the worker thread's queue. If `Timer::ProcessTimers()` fired `m_recvTimer.OnExpired` and queued an async dispatch of that `TimerDelegate` in the narrow window just before `Stop()` ran, the worker thread later executed it after it was already freed. Confirmed via AddressSanitizer (3 clean iterations after the fix) and repeated plain-Debug runs. Fixed by disconnecting `m_recvConn` only after `ExitThread()` fully drains the queue. Found via the new `NetworkNodeTests.cpp` (see Added, above).
- **`strncpy` in `NetworkNode::AddPeer`** flagged MSVC C4996 — replaced with an explicit-bounds `memcpy` (portable; doesn't require a non-standard `*_s` variant).
- **`CELLUTRON.md`'s "Hardware Topology" section described a "Hybrid TCP/UDP" transport** that doesn't match the code — `NetworkTypes.h` defines `Network` as a single `NetworkNode<Win32UdpTransport>`/`NetworkNode<LinuxUdpTransport>` (one UDP transport per node), with "guaranteed" delivery for commands/faults actually coming from `Reliability::RELIABLE` (ACK + retry over that same UDP transport), not a real TCP socket. Corrected the doc to match.

### Changed
- **Cellutron `CellProcess`/`PumpProcess`/`Centrifuge` state declarations and state maps changed from `protected` to `private`** — `STATE_DECLARE` binds each state handler via a non-type template parameter (`StateAction<SM, Data, Func>`), not a virtual call, so a subclass override would never actually be invoked by the state engine; `protected` implied subclass extensibility that doesn't exist, and no class in the tree derives from any of the three.
- **Removed stale `UNVERIFIED` doc-comment markers** for `NuttX` (all primitives), Zephyr's `CriticalSection`/`Semaphore`, and CMSIS-RTOS2's `Semaphore` — these have since been built and run on a Linux machine (matching what `CLAUDE.md`'s ISR-safe locking table already documented for several of them). `CmsisRtos2CriticalSection` is untouched: its CMSIS-Core intrinsics still need real Cortex-M hardware/QEMU, which this did not use.
- **`tools/` restructured** — consolidated four subdirectories that each held exactly one file (`monitor/`, `spy/`, `target/`, `thread/`, one per standalone console executable) into `tools/apps/`, and renamed `tools/src/` (a name that read as a leftover placeholder) to `tools/net/` — it's the shared `UdpSocket.h` wrapper used by three of the four console apps and both `bridge/` headers. `tools/` is now three purposeful subdirectories (`apps/`, `bridge/`, `net/`) instead of six. Also fixes `TOOLS.md`'s stale "three executables"/"both bridges" wording and updates the three `example/cellutron/*/CMakeLists.txt` files that referenced `tools/src` via `TOOLS_SRC_DIR`.

## [2.0.3] - 2026-09-11

### Added
- **`DMQ_FORCE_OPTIMIZE_DEBUG`** CMake option (off by default) forces GCC/Clang's `-Os` on DelegateMQ's template-heavy headers even inside an otherwise unoptimized Debug build, via `#pragma GCC optimize` push/pop around each header's `namespace dmq`. At `-O0`, none of the library's thin wrapper/accessor calls get inlined, so a meaningful chunk of per-instantiation cost is otherwise left on the table with no way to recover it short of building the whole application at a higher optimization level. Measured ~2.7% flash reduction (`stm32-freertos`, GCC `-Os`) — roughly 6x the combined savings of the per-signature consolidation work below. No effect on MSVC: `#pragma optimize` cannot elevate above the command-line `/Od` baseline and is unsupported on x64 targets, so it's a documented no-op there (confirmed byte-for-byte identical on `cellutron_controller`) rather than a partial workaround.
- **`dmq::CriticalSection`** — new portable ISR-safe lock type for the narrow set of call sites (currently `dmq::util::Timer`) that must work from both thread and hardware ISR context, where OS mutexes cannot. Implemented for all RTOS ports (ThreadX, FreeRTOS, Zephyr, CMSIS-RTOS2, NuttX) and bare metal; desktop stays aliased to `RecursiveMutex`. See `CLAUDE.md`'s "ISR-Safe Locking" section for usage rules and per-port verification status.
- **NuttX port** (`DMQ_THREAD_NUTTX`, `port/os/nuttx/`) — `NuttXThread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`/`ThisThread`/`CriticalSection`, using NuttX's real POSIX primitives directly (`pthread_create`/`pthread_join`, POSIX `mqueue`, `sem_t`) rather than bespoke wrappers, plus `up_irq_save()`/`up_irq_restore()` for the ISR-safe critical section. Wired into `DelegateOpt.h`/`DelegateMQ.h`/`Defaults.cmake`/`Port.cmake`.
- New `example/sample-projects/nuttx-sim/` sample — verifies the NuttX port above against NuttX's own `sim` board (a native host process, no cross-compiler or hardware), the same low-cost verification model as `zephyr-linux`/`cmsis-rtos2-linux`. All 10 tests pass, including cross-thread `FullPolicy`/`PacedDispatch` stress tests and a one-shot `Timer` genuinely exercising `NuttXCriticalSection`'s `up_irq_save()`/`up_irq_restore()` from application code. See the sample's README for the toolchain gap this surfaced (Ubuntu's own NuttX libc++ default, uClibc++, has an unreachable upstream source archive) and two NuttX build-system quirks worked around (not DelegateMQ bugs).
- **`<Platform>DelegateQueue` wrapper classes** (FreeRTOS/ThreadX/CMSIS-RTOS2/Zephyr) isolate each port's native OS queue API behind a small RAII type, keeping `<Platform>Thread.cpp` focused on policy/dispatch logic rather than OS plumbing.
- New RTOS simulator samples — each builds and runs as an ordinary host process, no cross-compiler or target hardware required: `example/sample-projects/threadx-linux/`, `freertos-linux/`, `zephyr-linux/`. Includes ported unit test coverage for cross-thread dispatch/`FullPolicy` (Test 9) and `Timer`/`PacedDispatch` (Test 10) on each.
- New `example/sample-projects/zephyr-udp-serializer/` sample — remote delegate call over real UDP loopback via `ZephyrUdpTransport`, the first time this transport has been built and run.
- New `example/sample-projects/databus-zephyr/` sample — Zephyr DataBus server/client counterpart to `databus-freertos`, verified with real bidirectional UDP traffic (sensor readings, alarm state, rate-control commands).
- New `example/sample-projects/cmsis-rtos2-linux/` sample — verifies `DMQ_THREAD_CMSIS_RTOS2` (`CmsisRtos2Thread`/`Mutex`/`Semaphore`/`DelegateQueue`/`Clock`) against a real CMSIS-RTOS2 implementation (Zephyr's own `CONFIG_CMSIS_RTOS_V2` compatibility layer) on `native_sim`, without any ARM cross-toolchain, QEMU, or hardware. `CmsisRtos2CriticalSection` itself remains unverified (its CMSIS-Core intrinsics need real Cortex-M); see the sample's README and `CLAUDE.md`'s "ISR-Safe Locking" section.
- **`example/sample-projects/bare-metal-arm/` (Test 6)** — a real Cortex-M4 SysTick interrupt now drives `dmq::util::Timer::ProcessTimers()`, verifying `dmq::os::BareMetalCriticalSection` from genuine ISR context (QEMU `mps2-an386`) for the first time. See `CLAUDE.md`'s "ISR-Safe Locking" section and the sample's README for the two real bugs this surfaced (below).
- **First RISC-V port**: `dmq::os::BareMetalCriticalSection`/`BareMetalClock` (`DMQ_THREAD_NONE`) now branch for RISC-V (`mstatus.MIE` via `csrrci`/`csrs`) alongside the existing ARM implementation. New `example/sample-projects/bare-metal-riscv/` sample (RV32IMC, QEMU `virt` machine, direct machine-mode boot via the xPack RISC-V Embedded GCC toolchain — Ubuntu's `gcc-riscv64-unknown-elf` apt package has no libstdc++ for this target, which DelegateMQ's `dynamic_cast`-based `Delegate::Equal()` genuinely needs) verifies both, driven by the CLINT machine timer through a hand-written trap vector (RISC-V, unlike ARM's NVIC, saves nothing automatically on trap entry). See the sample's README for two real bootstrapping issues this surfaced (the toolchain's generic `crt0.o` assumes a hosted-semihosting boot convention incompatible with a direct `-bios none` boot; `libc.a` unconditionally pulls in `_getentropy_r`).

### Fixed
- **Zephyr port silently ignored `dmq::Priority::HIGH`** — `k_msgq_put()` has no priority/front-of-queue argument; `ZephyrDelegateQueue` now keeps two `k_msgq` instances (mirroring the desktop two-queue model) and always drains the high-priority one first.
- **`Timer::ProcessTimers()` could not safely run from a genuine ThreadX ISR** — `Timer::GetLock()` now uses `dmq::CriticalSection` instead of a real `TX_MUTEX` (ThreadX forbids mutex create/acquire from ISR context). `llms.txt`'s ISR call-site guidance corrected to match: ThreadX and bare-metal are genuinely ISR-safe; FreeRTOS/Zephyr/CMSIS-RTOS2 still need thread/task context.
- **`ThreadXClock` tick/millisecond mismatch** — `now()` returned raw `tx_time_get()` ticks unscaled by `TX_TIMER_TICKS_PER_SECOND`, making every `Timer` duration run 10x slow at the default 100Hz tick rate.
- **ThreadX port 64-bit pointer truncation** — `this` was passed to the worker thread entry point via a 32-bit `ULONG`, silently truncating the pointer on 64-bit hosts. `Process()` now looks up the owning `Thread*` via `tx_thread_identify()` against a registry instead.
- **ThreadX Linux/GNU build serialized every kernel critical section through one debug mutex** (`TX_LINUX_DEBUG_ENABLE`) — now stripped from the vendored build via `External.cmake`.
- **`ThreadXCriticalSection` referenced a macro-local variable name that differs per ThreadX port** and isn't valid across separate `lock()`/`unlock()` calls — now uses the portable `tx_interrupt_control()` service instead.
- **`ZephyrThread::ExitThread()` could free thread/stack memory before the kernel finished teardown**, leaving a dangling scheduler reference — fixed by adding `k_thread_join()` after the exit semaphore wait.
- **`.AsyncInvoke()` (`DelegateAsyncWait`) never compiled on Zephyr or CMSIS-RTOS2** — missing from `DelegateMQ.h`'s include guard despite both ports already having the required `dmq::Semaphore` backend.
- **RTOS simulator samples silently compiled in desktop-only `DataBus`/`NetworkConnect.h` code** because the default keyed off the *host* build platform rather than the selected `DMQ_THREAD_*` — caused header collisions with a target's native network stack (e.g. Zephyr's socket headers) once a sample also needed real networking.
- **`ZephyrUdpTransport`** — rewritten to use Zephyr's `zsock_*` API throughout (mixing it with unprefixed BSD names required a POSIX compatibility layer that collided with the host's real pthread headers on `native_sim`); `Create()` also failed unconditionally until `CONFIG_NET_CONTEXT_RCVTIMEO` was enabled in `prj.conf`.
- **`DelegateMQ.h`'s `DMQ_TRANSPORT_THREADX_UDP` include pointed at a nonexistent directory** (`threadx-udp` instead of `netx-udp`) — never caught because nothing in the repo had built it before.
- **`databus-freertos/client/Client.h` hardcoded `Win32UdpTransport`**, failing to compile on Linux despite its own doc comment — now uses the portable `UdpTransport` alias; also reused for `databus-zephyr/client`.
- **`databus-interop`'s Python and C# clients never updated their `.start()`/`.Start()` call after it gained a required `multicastGroup` parameter** — both now pass the multicast group address. Python verified end-to-end; C# correct by inspection but unbuilt (no matching .NET SDK here).
- A handful of smaller issues found while bringing up the new samples: missing `#include <optional>` in `ZephyrThread.h`; `External.cmake`'s hard sibling-directory check for Zephyr (incompatible with its `west`-based workflow) relaxed to a warning; a copy-paste test bug binding `delegateAsyncWait2` to the wrong worker thread in `DelegateThreadsTests.cpp`.
- **`CmsisRtos2Clock::now()` was not ISR-safe** — it called `osKernelLock()`/`osKernelRestoreLock()` to guard its rollover-tracking static state, but a CMSIS-RTOS2 `osTimer` callback runs in genuine ISR context under Zephyr's CMSIS-RTOS2 compatibility layer (a `k_timer` `expiry_fn`), and Zephyr's `osKernelRestoreLock()` unconditionally writes its argument into the *interrupted* thread's scheduler-lock-nesting count before checking for ISR context — corrupting live scheduler state on every tick. Symptom: a one-shot `dmq::util::Timer` fired its callback forever instead of once. Fixed by using `dmq::CriticalSection` instead, the same ISR-safe primitive `Timer::GetLock()` already uses. Found and verified fixed via `example/sample-projects/cmsis-rtos2-linux`.
- **`dmq::util::Timer` was unreachable under `DMQ_THREAD_NONE` (bare metal)** — `DelegateMQ.h` excluded `extras/util/Timer.h`'s include, and `Port.cmake`'s bare-metal source glob explicitly excluded `Timer.cpp`, both under the stale assumption that `Timer` needs real OS mutexes. It doesn't: `Timer::GetLock()` uses `dmq::CriticalSection`, and `BareMetalCriticalSection.h`/`BareMetalClock.h` exist specifically to make `Timer` usable with no RTOS at all. Both now include `Timer` for `DMQ_THREAD_NONE`; `AsyncInvoke.h`/`TimerDelegate.h`/`ThreadMonitor.cpp` (which do need a real `dmq::IThread`) stay excluded. Found while adding a `Timer` test to `example/sample-projects/bare-metal-arm`.
- **`bare-metal-arm/CMakeLists.txt` never linked any `extras/`/`port/` `.cpp` file** — it globbed `DMQ_PORT_SOURCES` into an unused `SOURCES` variable and never passed it (or `DMQ_EXTRAS_SOURCES`) to `add_executable()`, silently relying on the fact that nothing this sample previously used actually needed a compiled `.cpp` outside `DMQ_LIB_SOURCES`. Now adds `${DMQ_EXTRAS_SOURCES}` specifically (not the full `${DMQ_PORT_SOURCES}`, which also includes `port/fault/Fault.cpp` — that would collide at link time with this sample's own `FaultHandler()`/`WatchdogHandler()` definitions).

### Changed
- **Reduced per-signature flash footprint of the delegate containers** — `DelegateAsync`/`DelegateAsyncWait`, `MulticastDelegate`/`MulticastDelegateSafe`, `Signal`, and `UnicastDelegate`/`UnicastDelegateSafe` each generated a separate `shared_ptr` control block (or, for `Signal`, a separate nested `State`/`Snapshot` type) per bound function signature — even where the underlying data was already fully erased to the non-templated `DelegateBase`. Hoisted the signature-independent logic into non-templated `detail::` free functions/structs in each file. No public API changes. Measured per-signature `.text` reduction (GCC, `-Os`): ~24% (`DelegateAsync`/`Wait`), ~50%/~41% (`MulticastDelegate`/`Safe`), ~70% (`Signal`), ~40%/~33% (`UnicastDelegate`/`Safe`). See `CLAUDE.md`'s "Template Instantiation Cost" section for the established pattern this follows.
- **Reduced per-topic-type duplication in `DataBus`/`Participant`** — `DataBus` (a singleton, not a class template) had the same underlying issue via template *member functions*: a "look up/establish topic type" block copy-pasted in four methods, and `InternalPublish<T>`'s signature-independent bookkeeping (LVC lookups, monitor/stringifier/signal/serializer map lookups, the participant-snapshot loop) regenerated per topic type. Hoisted into ordinary (non-template) `DataBus` member functions. Separately, `Participant::GetOrCreateChannelLocked<T>`'s `RemoteChannel<T>` allocation and `DataBus::GetOrCreateSignal<T>`'s `Signal<Sig>` allocation both had the same `shared_ptr`-control-block issue as above, fixed via the same "erase before constructing the shared_ptr" principle even though neither type has a non-templated base to erase to (erases to `void*` via a template-instantiated deleter *function*, not a lambda, since only a function's address decays to a uniform non-templated type). No public API changes. Measured impact of the `DataBus`/`RemoteChannel` fixes is much smaller than the delegate-container work above (~0.9% and ~1.4% respectively); the `GetOrCreateSignal` fix applies to essentially every `DataBus` application (most publish more than one topic type) but wasn't separately re-measured. Investigation showed the dominant per-topic-type cost actually lives in `InternalPublish<T>` unconditionally instantiating the full `Participant`/`RemoteChannel`/async-send stack for every `Publish<T>` call regardless of whether an application uses any remote participants at all, which is a separate, larger architectural question not addressed here.
- **`dmq::FullPolicy` consolidated** — was duplicated verbatim across seven port headers; now defined once in `DelegateOpt.h`, with each port aliasing it (existing `FullPolicy::DROP`-style call sites unaffected).
- **`dmq::os::ThreadMsg` consolidated** into a single `port/os/common/ThreadMsg.h`, replacing near-identical copies in six port directories.
- **Thread port files renamed** to match each platform's existing naming convention — `port/os/<platform>/Thread.{h,cpp}` → `<Platform>Thread.{h,cpp}` — with a `using Thread = <Platform>Thread;` alias so existing `dmq::os::Thread` references keep compiling unchanged.
- `test/stress_test*.cpp` moved to `test/stress-tests/`, now its own static library matching the `unit-tests`/`sample-code` convention.

### Known Issues
- **`threadx-linux`: two concurrently active `dmq::os::Thread` instances deadlock ThreadX's own Linux/GNU simulation kernel.** Root-caused to ThreadX's own internal mutex, not DelegateMQ code; affected tests are disabled in that sample pending upstream investigation (see the file's header comment for details).

---

## [2.0.2] - 2026-07-22

### Added
- New GitHub Actions workflows for CMake embedded and CMake stress tests.
- `embedded_ports_compile.yml` — compiles the FreeRTOS, ThreadX, and CMSIS-RTOS2 thread ports against their real vendor headers (ARM GNU toolchain, compile-only, no execution) on every push/PR. Previously none of these ports were exercised by any CI workflow.

### Fixed
- Fixed Node 20 deprecation warnings in GitHub Actions by updating `actions/checkout` to `v7`.
- DataBus QoS bugs and associated unit tests.
- Build errors in `NetworkEngine.h` and the Cellutron safety example CMake configuration.
- Addressed multiple code review findings across `DelegateAsyncWait`, `DelegateRemote`, fixed-block allocator (`xnew.h`), `NetworkEngine`, and embedded thread ports (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2).
- **RTOS thread self-exit dangling pointer** — `m_selfExitPtr` was never reset to `nullptr` before `Thread::Run()` returned in the FreeRTOS, ThreadX, Zephyr, and CMSIS-RTOS2 ports, leaving it pointing at a destroyed stack variable (caught by GCC `-Wdangling-pointer` on a real embedded build). Now matches the pattern already used correctly in the stdlib port.
- **ThreadX port compile correctness** — `port/os/threadx/Thread.cpp` referenced `ULONG_PTR` and a `TX_THREAD::tx_thread_user_data` member, neither of which exist in the real ThreadX API; `this` is now passed through the standard `tx_thread_create` `entry_input` / `Process(ULONG instance)` mechanism instead. Also fixed `TX_NULL` not implicitly converting to `CHAR**` under C++ in `tx_queue_info_get`.
- **`DelegateOpt.h` include order** — `extras/util/Fault.h` (defining `ASSERT_TRUE`) is now included before `ThreadXMutex.h` / `ThreadXConditionVariable.h`, which call it from their constructors.
- **CMSIS-RTOS2 port** — `DispatchDelegate()` called `ThreadMsg::SetEnqueueTime()` unconditionally, but that method only exists under `DMQ_DATABUS_TOOLS`; now guarded consistently with the other RTOS ports.
- Test fixes: `AllocatorTests.cpp`'s xnew leak-on-throw regression check now only runs when the library actually guards against a throwing constructor (`__cpp_exceptions && !DMQ_ASSERTS`); `DataBusQosTest.cpp`'s `ThrowOnMoveOnce` helper gained a default constructor, fixing a GCC 14 template-instantiation compile failure.

### Changed
- Hardening updates across the core library and transport layer.
- Minor documentation updates in `README.md` and `llms.txt`.

---

## [2.0.1] - 2026-07-18

### Added
- `RemoteChannel` constructor accepts an optional `DelegateRemoteId` — send-only channels no longer require a placeholder `Bind()`; construct with the ID and invoke.
- `DataBus::EnableContinuousErrors(bool)` — DataBus errors are now latched per topic and error code (reported once); continuous mode re-reports every occurrence.
- `DelegateError::ERR_TYPE_MISMATCH` — a publish/subscribe topic type mismatch is now reported through the error signal for diagnosability before faulting.

### Fixed
- **Remote delegate initialization** — fixed a bug across test and sample projects where `MakeDelegate` assignments on a `DelegateMemberRemote` inadvertently overwrote explicitly configured stream, serializer, and error handlers, causing `ERR_NO_SERIALIZER` runtime crashes.
- **`MulticastDelegate` reentrancy** — broadcast now invokes a snapshot (SBO) of the delegate list; adding or removing delegates from within a callback no longer causes infinite loops. `Remove()` now erases all duplicate targets instead of only the first occurrence.
- **`RemoteArg<Arg*>` dangling pointer** — added missing copy and move constructors, fixing a dangling pointer when remote argument wrappers were copied.
- **DataBus remote topic registration** — registering topics now records the topic-to-remote-ID mapping on the participant (`AddRemoteTopic`), fixing inter-node topic forwarding.
- Thread-unsafe `printf` output in Windows sample projects.
- Linux build error and clang strict-warning findings (`-Wunsafe-buffer-usage` in the `Signal` snapshot buffer).
- **bare-metal-remote sample** — Fixed compile error by updating `BareMetalDispatcher` to match the new `IDispatcher::Dispatch` signature.

### Changed
- **Exception Handling & Documentation** — explicitly document `std::runtime_error` and `std::invalid_argument` usage in the library. All OS port `Thread::Process()` loops now explicitly catch these exceptions alongside `std::bad_alloc` when `DMQ_ASSERTS` is disabled.
- `std::shared_ptr` argument rules on asynchronous delegates refined: `const std::shared_ptr<T>&` and `const std::shared_ptr<T>*` are now accepted; non-const reference and pointer forms are rejected at compile time with a clearer diagnostic. Remote delegates still require `std::shared_ptr` by value.
- DataBus internals simplified: `TopicForwarder` helper removed in favor of thin lambda captures on `Participant::RegisterHandler`.
- `ISerializer::Read()` documentation now explains the `const_cast` requirement when deserializing `const T*` arguments.
- Documentation overhaul: README condensed (Motivation/Advantages rewritten, Modular Architecture folded into Overview) and gains a Remote Delegates example; `llms.txt` corrected (`DMQ_TOOLS` option name, updated remote delegate pattern).
- `IDispatcher::Dispatch` signature updated to accept `dmq::xostringstream&` and an optional `uint16_t* outSeqNum` to retrieve transport sequence numbers.
- `Thread` priority queue refactored: replaced `std::priority_queue` with two `std::deque`s (`m_highQueue` and `m_normalQueue`) to guarantee strict FIFO execution of messages within the same priority level. `Priority::LOW` has been removed.

---

## [2.0.0] - 2026-06-17

### Fixed
- **ThreadMonitor deadlock** — `Disable()` no longer holds `m_mutex` while joining the monitor thread; `MonitorLoop` acquires that mutex after its sleep, causing a deadlock in the prior implementation.
- **RetryMonitor stranded entry** — when `TransportMonitor` fires a TIMEOUT while the initial `Send()` is still executing, the sequence number is now re-registered so monitoring continues rather than leaking the entry permanently.
- **NetworkEngine shutdown race** — `Stop()` now sets the exit flag before closing transports (to unblock a blocking `Receive()`) and before joining the receive thread, preventing a narrow window where the receive thread could loop after the transport was closed.
- **Signal `Clear()` correctness** — `Clear()` now marks the old state `alive = false` under the lock, preventing a `ScopedConnection` destructor from accessing a dead delegate list.
- **`DelegateAsync::m_sync` data race** — changed from `bool` to `std::atomic<bool>` to prevent a UB data race between the caller thread and the target thread.
- Compile error in `NetworkEngine.h` under `DMQ_TRANSPORT_SERIAL_PORT` builds (missing `//` on `Reliability Layers` comment).
- `printf` thread safety in sample projects.
- `dmq-monitor` crash when message count is high.

### Changed
- `Thread` (stdlib): statistics (`m_queueDepthMax*`, latency, invoke metrics) are now protected by a dedicated `m_statsMutex` separate from the queue mutex, reducing lock contention on the dispatch hot path.
- `DataBus::RegisterSerializer` gains a `std::shared_ptr<ISerializer<void(T)>>` overload so callers can transfer ownership of heap-allocated serializers; the existing reference overload is preserved.
- `NetworkNode`: receive thread storage changed from `std::unique_ptr<Thread>` to `std::optional<Thread>`, eliminating one heap allocation per node lifetime.
- All `std::lock_guard` usages in `extras/` replaced with `dmq::LockGuard<T>` and direct `#include <mutex>` removed from `extras/` headers, restoring embedded-target build compatibility.
- MSVC parallel compilation (`/MP`) enabled by default.
- `xallocator` reference count is now `std::atomic<int32_t>`.
- `ThreadMonitor::Enable()` now holds `m_mutex` while constructing the monitor thread, closing a data race with concurrent `Disable()` calls.

---

## [1.1.8] - 2026-05-29

### Added
- `[[nodiscard]]` attribute on `ScopedConnection` returned by `Signal::Connect()` — discarding the connection now produces a compiler warning.
- Unit tests for fixed-block allocator paths and DataBus scenarios.

### Fixed
- Deadlock in `Signal` / DataBus path when a subscriber callback triggered re-entrant signal operations.
- Excessive heap usage in the fixed-block allocator path.
- Linux and Cellutron sample app build issues.

### Changed
- Lambda usage eliminated from DataBus internals; replaced with member delegates to keep captures small and allocation-free.
- Fixed-block allocator (`xmake_shared`, `xlist`, `xmap`) usage expanded across more internal structures.
- Improved error reporting and `FaultHandler` debug output.
- Cellutron sample app now builds and runs on Linux.

---

## [1.1.7] - 2026-05-19

### Added
- `dmq::ScopedLock` RAII helper.
- `DelegateMQConfig.h` — user-editable configuration header that separates tuneable constants from library internals; reduces per-thread stack usage.
- Sanitizers CI workflow (AddressSanitizer, UBSan).
- dmq-spy tool: improved data capture and filtering.

### Fixed
- Cross-thread race in `Timer` that could cause missed or double-fired expirations.
- FreeRTOS queue sizes tuned to reduce memory pressure.
- Sanitizer build errors.

### Changed
- DataBus error reporting improved with per-participant error callbacks.
- STM32 FreeRTOS sample app updated to use DataBus.

---

## [1.1.6] - 2026-05-07

### Added
- **`ThreadMonitor`** — new tool that publishes per-thread queue depth, latency, and invoke-time metrics to a DataBus topic; consumed by the `dmq-thread` dashboard.
- **`MakeTimerDelegate()`** — timer-safe async delegate factory guaranteeing at most one in-flight dispatch per timer tick, preventing queue flooding on slow handlers.
- **`MonotonicGuard`** — sequence-counter utility for detecting and discarding stale LVC rewind deliveries at the subscriber.
- TCP transport and example.
- C++ interop library for Python and C# with DataBus support.
- More thread invoke timing metrics (average, max-window, max-all).

### Fixed
- Multiple DataBus deadlock paths when a delegate was invoked while holding internal locks.
- Timer flooding: a slow timer handler could enqueue unbounded messages to the target thread; `MakeTimerDelegate` solves this.
- Watchdog reliability improvements.
- Linux interop build and execution.

### Changed
- `FullPolicy::BLOCK` replaced by `FullPolicy::TIMEOUT` across all thread ports; blocking indefinitely on a full queue is no longer a supported policy.

---

## [1.1.5] - 2026-04-24

### Added
- **DataBus** — topic-based publish/subscribe middleware layer (major feature). Supports local and remote topics, Last Value Cache (LVC), Lifespan, Min Separation, and Deadline QoS policies.
- `IsCurrentThread()` API on `IThread`.
- Win32 native thread port (`port/os/win32/`).
- DataBus multicast support.
- Cellutron multi-node demo (GUI / Controller / Safety nodes over DataBus).
- dmq-spy and dmq-monitor DataBus diagnostic tools.
- `FullPolicy` per-thread queue-full handling (DROP / TIMEOUT / FAULT).
- More unit tests.

### Fixed
- Signal/slot: fixed races when disconnecting under concurrent access. This does not stop a call already in progress on another thread from invoking the slot after `Disconnect()` returns; see the note in `Signal.h`.
- Various Linux and FreeRTOS build fixes.

### Changed
- `xmake_shared` introduced for allocator-aware `shared_ptr` construction.
- Fixed-block allocator coverage expanded across DataBus internals.
- Namespace consolidation.

---

## [1.1.4] - 2026-04-03

### Added
- New DataBus features and namespace updates.
- Linux UDP example.
- Automated sample project build scripts.
- Interop DataBus support for C# and Python.

### Fixed
- Linux build errors introduced by namespace changes.

---

## [1.1.3] - 2026-03-25

### Fixed
- Minor Linux build and lambda handling corrections.

---

## [1.1.2] - 2026-03-22

### Added
- `dmq::Duration` type alias for `std::chrono::milliseconds`; standardises time units across the API.

### Fixed
- `DelegateAsyncWait`: return value was not reliably propagated back to the caller under certain thread scheduling conditions.

---

## [1.1.1] - 2026-02-10

### Added
- `RemoteEndpoint` class simplifies binding a local handler to a remote delegate ID.

### Fixed
- Remote delegate bugs affecting argument forwarding on certain compilers.
- Linux build.

---

## [1.1.0] - 2026-01-15

### Added
- **`Signal<Sig>` / slot** — thread-safe multicast signal with RAII `ScopedConnection` disconnect handles.
- **`ReliableTransport`** — wraps any `ITransport` to add acknowledgement and retry logic.
- **`RetryMonitor`** — automatic retransmission manager with configurable retry budget and timeout detection.
- CRC16 validation for serial-port transports.
- ARM Cortex-M bare-metal sample application.
- STM32 Discovery board FreeRTOS sample application.
- Stress tests for remote delegates under concurrent load.
- FreeRTOS max thread queue size configuration.
- Thread queue size limit enforcement.
- Precompiled header support to speed compile times.

### Fixed
- `WAIT_INFINITE` constant value on Windows vs. Linux.
- Remote delegate serialization edge cases.
- Various Linux and FreeRTOS build issues.

### Changed
- Cross-platform compatibility improvements across all thread and transport ports.
- Documentation expanded with TCP and serial port examples.

---

## [1.0.9] - 2025-12-26

### Fixed
- Linux build errors in network and sample code.
- ZeroMQ transport: separated send and receive sockets to prevent cross-thread interference.

### Changed
- `NetworkMgr` refactored to expose a generic `RemoteInvoke()` interface.
- `ITransport::Receive` interface updated for cleaner integration with the dispatcher layer.
- `msgpack` dependency pinned to a specific version for reproducible builds.

---

## [1.0.8] - 2025-04-13

### Added
- Watchdog feature built into `dmq::os::Thread`: detects deadlocked or unresponsive threads and calls a user-provided `WatchdogHandler`.
- `spdlog` integration for optional structured debug logging.
- Async delegate message priority queue support.
- Python interoperability example.
- Safe timer example demonstrating `MakeTimerDelegate` usage.
- `dmq::SharedDelegate` classes for shared-ownership delegate scenarios.
- NNG (nanomsg-next-gen) transport and example.
- Cereal and Bitsery serialization sample projects.

### Fixed
- `chrono::system_clock` replaced with `chrono::steady_clock` in `Timer` — eliminates skew from wall-clock adjustments.
- `DelegateRemote` now handles arbitrary argument counts (previously limited to 5).
- Delegates can safely be removed from a container during a callback without iterator invalidation.
- Multi-threaded timing corner cases in `DelegateAsyncWait`.

### Changed
- Improved serializer compile-time type checking.
- Documentation: new porting guide, API reference updates.

---

## [1.0.7] - 2025-03-31

### Fixed
- Remote delegate bugs affecting return-value forwarding.
- Linux build.

---

## [1.0.6] - 2025-03-27

### Added
- `dmq-monitor` and `dmq-spy` DataBus tools initial integration.
- Cellutron example: dmq-spy and dmq-monitor support added.

### Fixed
- Linux build errors from namespace changes.
- Sample project build issues.

---

## [1.0.5] - 2025-03-18

### Fixed
- Linux build errors.

### Changed
- Namespace updates across transport and serializer layers.
- Sample app fixes.

---

## [1.0.4] - 2025-03-10

### Added
- New ARM bare-metal demo project (Keil).
- C# and Python interop DataBus support.
- `add_compile_options(/MP)` for parallel MSVC builds (restored for desktop builds).

### Changed
- Directory structure reorganised; tools moved into the DelegateMQ repository.
- Build scripts automated for sample projects.
- Documentation badges and images updated.

---

## [1.0.3] - 2025-03-08

### Fixed
- Minor Linux build corrections.

---

## [1.0.2] - 2025-03-05

### Added
- `UnicastDelegateSafe` — thread-safe single-slot delegate container.
- Timer `once` flag: `Start(timeout, once=true)` fires exactly once then self-disables.

### Fixed
- Thread `ExitThread()` reliability on shutdown.
- Build errors on Linux and Windows.

---

## [1.0.1] - 2025-02-24

### Fixed
- `RemoteDelegate` error handling: transport errors now surface through the error handler delegate rather than being silently dropped.

### Changed
- Documentation improvements.

---

## [1.0.0] - 2025-02-17

Initial public release.

### Added
- `dmq::Delegate<Sig>` — synchronous direct-call delegate.
- `dmq::DelegateAsync<Sig>` — non-blocking async delegate (fire-and-forget).
- `dmq::DelegateAsyncWait<Sig>` — blocking async delegate with optional timeout and return value.
- `dmq::DelegateRemote<Sig>` — remote delegate serializing arguments over a transport.
- `dmq::MulticastDelegate` / `dmq::MulticastDelegateSafe` — broadcast containers.
- `dmq::UnicastDelegate` — single-slot delegate container.
- Thread ports: stdlib, Win32, FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, Qt, bare-metal.
- Transport ports: ZeroMQ, NNG, MQTT, TCP (Win32/Linux), UDP (Win32/Linux/multicast), serial, Win32 named pipe.
- Serializer adapters: MessagePack, RapidJSON, Cereal, Bitsery, custom serialize.
- Fixed-block allocator (`DMQ_ALLOCATOR`) for embedded/RTOS targets.
- CMake build system with platform auto-detection.
- CI: Ubuntu, Windows, Clang, sanitizers workflows.
- MQTT, ZeroMQ, NNG, TCP, UDP, serial example projects.
