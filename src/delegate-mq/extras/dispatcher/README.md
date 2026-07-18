# Dispatcher Layer

This directory contains the dispatch logic for **DelegateMQ**, serving as the critical bridge between the high-level serialization layer and the low-level physical transport.

## Overview

The `dmq::util::Dispatcher` is responsible for taking the serialized payload (function arguments) and preparing it for transmission over the network or IPC link. It decouples the "what" (serialized data) from the "how" (transport mechanism).

## Key Components

* **`dmq::util::Dispatcher.h`**: The concrete implementation of the `dmq::IDispatcher` interface.
* **`dmq::RemoteChannel.h`**: An aggregator that owns a `dmq::util::Dispatcher`, an `dmq::xostringstream` (serialization buffer), and borrows an `dmq::ISerializer` for a single function signature. Use `dmq::RemoteChannel` to configure `dmq::DelegateMemberRemote` endpoints without manually wiring each component.

### Responsibilities

#### dmq::util::Dispatcher
1. **Protocol Framing**: It constructs the `dmq::transport::DmqHeader` for every message, assigning the correct **Remote ID** and generating a monotonic **Sequence Number**.
2. **Stream Validation**: It ensures the output stream (`dmq::xostringstream`) contains valid data before transmission.
3. **Transport Handoff**: It forwards the framed message (Header + Payload) to the registered `dmq::transport::ITransport` instance for physical transmission.

#### dmq::RemoteChannel
`dmq::RemoteChannel<Sig>` owns a `dmq::DelegateFunctionRemote` internally and handles all wiring automatically. Call `Bind()` once to configure the channel (for receiving), or pass the ID to the constructor (for sending), then invoke it with `operator()`.

```cpp
// Create one channel per message signature
dmq::RemoteChannel<void(AlarmMsg&, AlarmNote&)> alarmChannel(transport, alarmSerializer);

// Bind a member function and remote ID — channel owns the delegate internally
alarmChannel.Bind(this, &MyClass::ForwardAlarm, ALARM_MSG_ID);

// Optionally register an error handler
alarmChannel.SetErrorHandler(dmq::MakeDelegate(this, &MyClass::OnError));

// Register the receive endpoint 
dmq::RegisterEndpoint(ALARM_MSG_ID, alarmChannel.GetEndpoint());

// Invoke (fire-and-forget)
AlarmMsg msg; AlarmNote note;
alarmChannel(msg, note);

// Or invoke with blocking wait
bool ok = dmq::RemoteInvokeWait(alarmChannel, msg, note);
```

Key accessors:
- `Bind(obj, func, id)` — binds a member function and configures all internal plumbing.
- `operator()(args...)` — invokes (sends) the remote message.
- `GetEndpoint()` — returns `dmq::IRemoteInvoker*` for `dmq::RegisterEndpoint()`.
- `GetError()` / `GetRemoteId()` — query last error and remote ID.
- `SetErrorHandler(delegate)` — register an error notification callback.

> **Legacy accessors** (`GetDispatcher()`, `GetSerializer()`, `GetStream()`) remain available for code that manually wires a `dmq::DelegateMemberRemote`.
