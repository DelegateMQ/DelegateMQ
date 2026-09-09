# DelegateMQ Zephyr UDP Remote Delegate Example

This sample demonstrates a **remote delegate call over a real network transport** on Zephyr, using the official `native_sim` simulation port (host GCC, no cross-compiler, no target hardware). A `Sender` and `Receiver` both run in this one process, exchanging real UDP packets over the loopback interface (127.0.0.1) — real `socket()`/`sendto()`/`recvfrom()` calls through Zephyr's own networking subsystem, not a direct in-process function call standing in for "the network."

It's the Zephyr counterpart to `linux-udp-serializer`/`win32-udp-serializer`: `sender.h`, `receiver.h`, `data.h`, and `data.cpp` are copied **unchanged** from `linux-udp-serializer`. They reference `dmq::transport::UdpTransport` generically, which resolves to `ZephyrUdpTransport` here purely from `DMQ_TRANSPORT_ZEPHYR_UDP` at compile time — the same portability `dmq::os::Thread` already gives every `DMQ_THREAD_*` port. Only `main_delegate.cpp` (the Zephyr entry point and `Timer::ProcessTimers()` driver) and the Kconfig (`prj.conf`) are Zephyr-specific.

## What This Verifies

Unlike `zephyr-linux` (which exercises DelegateMQ's delegate/thread/timer mechanics but configures `DMQ_TRANSPORT_NONE`), this sample is the first time `ZephyrUdpTransport` — DelegateMQ's `port/transport/zephyr-udp/ZephyrUdpTransport.h` — has actually been built and run. Three real bugs surfaced getting it working, all now fixed:

1. **Every RTOS-linux sample was silently compiling in desktop-only `DataBus`/`NetworkConnect.h` code.** Both `Defaults.cmake` (the CMake-level default) and `DelegateOpt.h` (an internal preprocessor fallback for projects not using `DelegateMQ.cmake`) defaulted `DMQ_DATABUS` on whenever the *host* platform was Windows/Linux/macOS/Unix — true for every RTOS simulator sample here, since `native_sim`/FreeRTOS's POSIX port/ThreadX's Linux-GNU port all compile with a host toolchain despite targeting an embedded thread port. Harmless on its own (unused dead code), but `NetworkConnect.h` unconditionally includes the host's own BSD socket headers (`<netinet/in.h>`, `<arpa/inet.h>`, `<net/if.h>`, ...), which collide outright with Zephyr's own `<zephyr/net/socket.h>` (`struct sockaddr_in`, `IPPROTO_UDP`, etc. redefined incompatibly) the moment a sample — like this one — also needs real target networking headers. Both defaults now key off the selected `DMQ_THREAD_*` instead of the host platform.
2. **`ZephyrUdpTransport.h` called the unprefixed BSD socket names** (`socket()`, `bind()`, `sendto()`, `recvfrom()`, `setsockopt()`, `inet_pton()`) inconsistently alongside already-prefixed calls (`zsock_close()`, `zsock_shutdown()`) elsewhere in the same file — a leftover from `CONFIG_NET_SOCKETS_POSIX_NAMES`, a Zephyr option deprecated in 3.7 and since removed. The unprefixed names now require `CONFIG_POSIX_API=y`, which replaces large parts of the standard header set (pthread types included) with Zephyr's own POSIX compatibility layer — and that layer collides with the *host's* real pthread/libstdc++ headers on `native_sim`, which links against them directly for C++ support. Rewritten to use the `zsock_*`-prefixed calls throughout, which need no such layer and work identically on real hardware.
3. **`Create()` silently failed on every call** — `setsockopt(SO_RCVTIMEO)` returned `ENOPROTOOPT` because `CONFIG_NET_CONTEXT_RCVTIMEO` wasn't enabled (off by default). `Create()`'s return value goes unchecked in `sender.h`/`receiver.h` (matching the other `*-udp-serializer` samples), so this failed silently: both sockets ended up closed, and every send failed with `dmq::DelegateError::ERR_DISPATCH` — root-caused with targeted debug prints tracing the failure back from `Dispatcher::Dispatch()` through `RetryMonitor::SendWithRetry()` into `ZephyrUdpTransport::Create()` itself.

## Prerequisites

Same as `zephyr-linux` — see that sample's README for full `west` workspace setup. No additional host-side networking setup is needed: `CONFIG_NET_LOOPBACK` gives a fully in-process 127.0.0.1 interface, so there's no TAP device, no `net-setup.sh`, nothing to run as root.

## Build Instructions

```bash
export ZEPHYR_BASE=/path/to/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host
cd /path/to/zephyrproject
west build -b native_sim/native/64 /path/to/DelegateMQ/example/sample-projects/zephyr-udp-serializer -d build
./build/zephyr/zephyr.exe
```

Expect 60 `Data Message: ...` lines (one every 50ms for 3 seconds) with no `ErrorHandler`/`TIMEOUT` output — verified across repeated runs.

## Project Structure

* `main_delegate.cpp`: Starts a periodic `k_timer` driving `Timer::ProcessTimers()` (same pattern as `zephyr-linux`), constructs a `Receiver` and `Sender`, lets them run for 3 seconds, then exits.
* `sender.h` / `receiver.h` / `data.h` / `data.cpp`: Unchanged from `linux-udp-serializer` — see that sample for what they do. `Sender` periodically serializes a `Data`/`DataAux` pair and dispatches it through a `dmq::util::ReliableTransport` (ACK + retry) wrapping `ZephyrUdpTransport`; `Receiver` polls for and deserializes incoming calls, invoking `DataUpdate()` locally.
* `prj.conf`: Kconfig additions beyond `zephyr-linux`'s baseline, all networking-related — see the comments in the file for why each one is needed.
* `CMakeLists.txt`: Same `find_package(Zephyr)` / `target_sources(app ...)` pattern as `zephyr-linux`, with `DMQ_TRANSPORT_ZEPHYR_UDP` and `DMQ_SERIALIZE_SERIALIZE` instead of `NONE`.
