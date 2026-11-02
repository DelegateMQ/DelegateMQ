# Changelog

All notable changes to DelegateMQ are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags. Changes are from the perspective of library users and integrators.

---

## [Unreleased]

### Added
- **Allocation check for `DMQ_ALLOCATOR` builds** (`test/AllocTrack.cpp`, Windows/MSVC): replaces global `new`/`delete`, records the call stack of every global `new` during the test app's 2nd and 3rd pass, and reports them grouped by the library or test code that made them, to find library heap use outside the fixed-block allocator. Off unless `DMQ_ALLOC_TRACK` is defined; see BUILD.md.
- **Thread stack sizes and default priorities on the RTOS ports are now config settings**: `DMQ_FREERTOS_STACK_WORDS` (4096), `DMQ_THREADX_STACK_BYTES` (2048), `DMQ_ZEPHYR_STACK_BYTES` (2048), `DMQ_CMSIS_RTOS2_STACK_BYTES` (2048), `DMQ_NUTTX_STACK_BYTES` (8192), and `DMQ_THREADX_THREAD_PRIORITY` (10), `DMQ_ZEPHYR_THREAD_PRIORITY` (5), `DMQ_NUTTX_THREAD_PRIORITY` (100). Previously these were hard-coded; except for FreeRTOS's `SetStackMem()`, changing a stack size meant editing the library. Defaults are unchanged.
- **`DelegateMQ::DelegateMQ` CMake target.** Pull DelegateMQ in with `FetchContent` or `add_subdirectory`, set any `DMQ_*` options first, then `target_link_libraries(app PRIVATE DelegateMQ::DelegateMQ)`. It's a static library of the selected port and extras sources, and it carries the `DMQ_*` definitions, include directories (third-party ones as `SYSTEM`) and the dependencies DelegateMQ can resolve (ZeroMQ, ThreadX, Qt, `Threads::Threads`). Projects that include `DelegateMQ.cmake` directly can call `dmq_add_library(<name>)` for the same target, one per configuration, and set `DMQ_NO_GLOBAL_DEFINITIONS ON` to keep definitions off the calling directory. See BUILD.md "Consuming as a Dependency".
- **`DMQ_USER_CONFIG_DIR`** adds the directory holding the user config header to the target's include path.
- **Building without CMake: one source file or the whole folder.** For Keil, IAR, STM32CubeIDE, PlatformIO or Makefiles, add `src/delegate-mq` to the include path and either the single file `src/DelegateMQ.cpp` or every `.cpp` under `src/delegate-mq` (but not both). Each library `.cpp` now compiles to nothing unless the options select it, so there's no list of port files to pick from and nothing to exclude. See BUILD.md "Without CMake".
- **`DelegateMQConfig.h` is picked up automatically** when it's on the include path (C++17 `__has_include`), and it can now select ports and options (`DMQ_THREAD_*`, `DMQ_ALLOCATOR`, ...) as well as tune constants, so a build without CMake needs no `-D` flags. `-DDMQ_USER_CONFIG=` still works; define `DMQ_NO_USER_CONFIG` to ignore the file. `DelegateMQConfig_Template.h` lists the options.
- **`DMQ_CUSTOM_FAULT_HANDLER`.** Define it when the application provides its own `FaultHandler()`/`WatchdogHandler()`; `port/fault/Fault.cpp` then compiles to nothing instead of having to be left out of the build.

### Fixed
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
- **`atfe-armv7m-bare-metal` only configured on Windows.** The ATfE tool paths no longer hardcode `.exe`; the README shows the Linux toolchain.
- **`-Wsign-compare` in `ZephyrUdpTransport.h`**, plus `-Wreorder`/`-Wdouble-promotion` warnings in the `zephyr-udp-serializer` and `databus-zephyr` samples.
- **`DMQ_STRICT` on MSVC failed in third-party headers** (msgpack under `/analyze`). Strict mode now adds `/analyze:external-`, so code analysis covers DelegateMQ's own code, not external headers.

### Changed
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
