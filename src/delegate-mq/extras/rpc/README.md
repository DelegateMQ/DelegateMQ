# RPC Layer

This directory contains **DelegateMQ**'s point-to-point RPC pattern: synchronous or fire-and-forget calls to a specific remote endpoint, as opposed to the topic-based publish/subscribe pattern in [`extras/databus`](../databus/README.md).

## RemoteDispatcher vs. DataBus — Remote Function Invoke vs. Data Distribution

DelegateMQ has two distinct patterns for talking across threads/processes/machines. Picking the wrong one for the job shows up as awkward code, not a compile error, so it's worth knowing which is which before starting:

| | **RemoteDispatcher** (this directory) | **DataBus** ([`extras/databus`](../databus/README.md)) |
|---|---|---|
| Pattern | Point-to-point RPC (remote function invoke) | Publish/subscribe (data distribution) |
| Addressing | Remote ID → one specific registered endpoint | Topic string, many-to-many |
| Who receives | Exactly one endpoint per remote ID | Any number of subscribers (0, 1, or many) — the publisher doesn't know or care who |
| Call semantics | `RemoteInvokeWait()` blocks the caller until the remote ACKs or times out, returning success/failure directly — plus a fire-and-forget mode too | `Publish()` is always fire-and-forget from the caller's side; delivery outcome (if any) arrives later via signals (`OnSendStatus`, `OnDeliveryFailed`) |
| How you use it | Subclass: `NetworkMgr : public dmq::rpc::RemoteDispatcher`, override virtual hooks (`OnError`/`OnStatus`/`OnDeliveryFailed`) | Compose: hold an `ITransport&` (`Participant`), or instantiate `NetworkNode<Transport>` — no subclassing required |
| Reliability opt-in | Per-connection — the derived class decides once, at construction, whether to wrap its transport in `ReliableTransport` | Per-message — pass `Reliability::RELIABLE` or `UNRELIABLE` to `Send()` |
| Multi-peer topology | One connection per `RemoteDispatcher` instance; the app manages multiple peers itself if it needs more than one | Built in — `NetworkNode` manages any number of peers |
| Typical use | Commands, remote function calls, request/response where the caller needs to know the call landed | Sensor data, telemetry, status broadcasts, state that should reach whoever's currently interested |

**Rule of thumb:** if you're asking "did that specific call succeed?" reach for RemoteDispatcher. If you're asking "who needs to know this happened?" reach for DataBus.

## Overview

`dmq::rpc::RemoteDispatcher` is a base class that owns the network thread and reliability plumbing ([`TransportMonitor`](../util/TransportMonitor.h), shared with `extras/databus`, not duplicated here) but never constructs or knows the concrete transport type. A derived, application-specific manager owns its own transport (e.g. `Win32UdpTransport`, `ZeroMqTransport`), optionally wraps it in [`ReliableTransport`](../util/ReliableTransport.h)+[`RetryMonitor`](../util/RetryMonitor.h) for ACK/retry reliability, and hands the result to `Attach()`. `RemoteDispatcher` only ever sees `dmq::transport::ITransport`, so it works with any transport that implements it — not a fixed list — and carries no per-transport branching itself.

```cpp
class NetworkMgr : public dmq::rpc::RemoteDispatcher
{
public:
    NetworkMgr() {
        // Construct and open the concrete transport, then hand it up. Wrap in
        // ReliableTransport first if this transport needs ACK/retry reliability.
        m_transport.Create(...);
        Attach(m_transport, m_transport);

        RegisterEndpoint(ALARM_MSG_ID, &m_alarmEndpoint);
    }

protected:
    // ITransport has no Close(); override to close whatever concrete
    // transport(s) this class owns. Called by Stop() before the receive
    // thread is joined.
    void CloseTransports() override { m_transport.Close(); }

public:
    // Fire-and-forget
    void SendAlarm(const AlarmMsg& msg) { m_alarmChannel(msg); }

    // Blocking: waits for ACK or timeout, returns success/failure
    bool SendCommandWait(const CommandMsg& msg) {
        return RemoteInvokeWait(m_commandEndpoint, msg);
    }

private:
    dmq::transport::SomeTransport m_transport;
    dmq::DelegateMemberRemote<NetworkMgr, void(AlarmMsg&)> m_alarmEndpoint;
    dmq::DelegateMemberRemote<NetworkMgr, void(CommandMsg&)> m_commandEndpoint;
};
```

## Key Components

* **`dmq::rpc::RemoteDispatcher.h`**: Manages the internal network thread and marshals calls onto it automatically. `RemoteInvokeWait()` blocks the caller until the remote ACKs or times out — the one capability `DataBus::Publish()` (inherently async/fire-and-forget) doesn't provide.
  * `Attach(sendTransport, recvTransport)` — call once, from the derived constructor's *body* (not its mem-initializer list, since the derived class's own transport member must already be fully constructed), to hand `RemoteDispatcher` the `ITransport&` it sends/receives through.
  * `AttachRetryMonitor(retryMonitor)` — call once, after `Attach()`, only if the derived class layered `RetryMonitor`/`ReliableTransport` on top of its transport. Skip it for a self-reliable transport (e.g. ZeroMQ) that has no `RetryMonitor`.
  * `CloseTransports()` — override to close the concrete transport(s) the derived class owns; `ITransport` itself has no `Close()`.
* **`dmq::rpc::RemoteEndpoint.h`**: Base class for `dmq::DelegateMemberRemote`, used to register receive-side endpoints with `RegisterEndpoint()`.

## Transport Support

Any transport implementing `dmq::transport::ITransport` works — `RemoteDispatcher` never constructs or names a concrete transport type itself, so there's no per-transport list to extend. This is also what `extras/databus`'s `Participant` already does generically; `RemoteDispatcher` now follows the same shape rather than the old `NetworkEngine`'s fixed `#if`/`#elif` chain over five transports.
