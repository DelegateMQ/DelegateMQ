# transport/shm-ring

Remote delegates between two processes over shared memory. It's the desktop version of two cores on an AMP MCU (such as the STM32H755's M7 and M4) talking through a shared SRAM region.

- **Side A**, the "controller", calls `SetSpeed(Command)` on side B every 20 ms.
- **Side B**, the "motor", runs `SetSpeed()` and calls `OnTelemetry(Telemetry)` back on side A.

Both directions go through `ShmRingTransport`'s two lock-free rings in one shared region. Each side's receive thread sleeps until the other side sends.

| Platform | Transport | Shared region | Wake-up |
| :--- | :--- | :--- | :--- |
| Windows | `Win32ShmTransport` (`port/transport/win32-shm`) | file mapping `Local\dmq-shm-sample` | named auto-reset events |
| Linux | `LinuxShmTransport` (`port/transport/linux-shm`) | `/dev/shm/dmq-shm-sample` | named semaphores |

On an MCU the same `ShmRingTransport` sits in a linker-placed SRAM section, and an HSEM interrupt does the waking.

The arguments are copied as raw bytes (`raw::Serializer`), which works because both processes are the same build. The messages use fixed-width fields only.

## Build and run

```bash
cmake -B build .
cmake --build build
build/delegate_app 5          # Windows: build\Release\delegate_app.exe 5
```

| Command | Runs |
| :--- | :--- |
| `delegate_app [seconds]` | side A, and starts side B as a child process (default 5 s) |
| `delegate_app a [seconds]` | side A only; start side B in another terminal |
| `delegate_app b` | side B only; start side A first (it waits up to 10 s for side A) |

## Output

```
Side A: region created (8896 bytes)
Telemetry: seq 0 speed 0 (side B has run 1 commands)
Telemetry: seq 50 speed 500 (side B has run 51 commands)
Side B: attached
Side B: side A closed; ran 94 commands
Side A: sent 94 commands, received 94 telemetry, 0 mismatched
```

The two processes share the console, so their lines can interleave in any order. Side B exits when side A closes its end. Side A exits with status 1 if it received no telemetry, a reply didn't match its command, or side B failed.

## Notes

- **Start order.** Start side A first: its `Create()` sets up the region and empties the rings. Don't restart side A while side B runs. Side B can close and reopen freely.
- **Build settings.** Both processes must use the same `DMQ_SHM_SLOT_COUNT` and `DMQ_SHM_MAX_PAYLOAD`. If they don't, side B's `Create()` fails rather than misreading the region.
- **Full ring.** A full ring fails the send and reports it through the channel's error handler. Nothing waits. Here the reply rate keeps the 16-slot rings far from full.
- **No ACKs.** Both sides call `SetSendTransport(nullptr)`, because neither uses a `TransportMonitor`. To detect a hung peer, attach one as the serializer sample does.
