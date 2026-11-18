# transport/netx-udp — Remote Delegates over NetX Duo on ThreadX

The [transport/serializer](../serializer/) app (a `Sender` calling a remote delegate every 50 ms, a `Receiver` getting it) built for ThreadX and NetX Duo. It uses `NetXUdpTransport` (`DMQ_TRANSPORT_THREADX_UDP`) on ThreadX's Linux/GNU simulation port, with no board needed. Both ends are in one process and talk to 127.0.0.1 through NetX Duo's own loopback interface, so every datagram goes through NetX's UDP/IP code.

The app's files are reused from `transport/serializer`: `sender.h`, `receiver.h`, and the built-in serializer's `data.h` and `data.cpp`. This folder adds only:

| File | Holds |
| :--- | :--- |
| `transport.h` | `NetXUdpTransport` bound to this app's NetX IP instance and packet pool, and the PUB/SUB endpoints |
| `main.c`, `main_delegate.cpp` | ThreadX start-up: the NetX packet pool and IP instance (on NetX's RAM test driver), a thread driving DelegateMQ's timers, and the run itself |
| `nx_user.h` | NetX options. On x86-64, ThreadX's Linux port keeps `ULONG` at 32 bits, so NetX must pass its control blocks through the ThreadX thread and timer blocks rather than a `ULONG` argument (from NetX Duo's own 64-bit test configuration). Without it NetX's IP thread crashes on a truncated pointer. |

Acknowledgments are on, as with UDP: the receiver acknowledges each message and the sender retries what isn't acknowledged in 500 ms.

## Requirements

Linux only. ThreadX and NetX Duo, fetched by `scripts/01_fetch_repos.py` (into `external/` or `DMQ_DEPS_DIR`). NetX Duo builds from its own CMake files against the ThreadX target DelegateMQ creates, with its FileX-based servers off.

## Build and run

```bash
cmake -B build .
cmake --build build
./build/delegate_netx_udp
```

It runs for 5 seconds and exits with status 0 if messages arrived intact and the receiver acknowledged them, else 1:

```txt
--- Starting ThreadX Kernel (Linux simulation) ---
Data Message: 0 0 Aux Message
...
Received 100 messages
Acknowledged 100 messages, 0 timeouts
```

`build_all.py` builds and runs it on Linux.

## What this sample found

`NetXUdpTransport` never sent an acknowledgment. An ACK has an empty payload, and appending zero bytes to a NetX packet fails with `NX_SIZE_ERROR` under NetX's default error checking, so `Send()` gave up before sending. Every message timed out and was retried until reported as failed. The transport now appends a payload only when there is one.
