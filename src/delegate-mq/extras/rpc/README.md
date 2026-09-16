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
| How you use it | Compose: hold a `dmq::rpc::RemoteDispatcher` as a member (`NetworkMgr`), connect to its `OnError`/`OnStatus`/`OnDeliveryFailed` Signals — no subclassing required | Compose: hold an `ITransport&` (`Participant`), or instantiate `NetworkNode<Transport>` — no subclassing required |
| Reliability opt-in | Per-connection — the owning class decides once whether to wrap its transport in `ReliableTransport` | Per-message — pass `Reliability::RELIABLE` or `UNRELIABLE` to `Send()` |
| Multi-peer topology | One connection per `RemoteDispatcher` instance; the app manages multiple peers itself if it needs more than one | Built in — `NetworkNode` manages any number of peers |
| Typical use | Commands, remote function calls, request/response where the caller needs to know the call landed | Sensor data, telemetry, status broadcasts, state that should reach whoever's currently interested |

**Rule of thumb:** if you're asking "did that specific call succeed?" reach for RemoteDispatcher. If you're asking "who needs to know this happened?" reach for DataBus.

## Overview

`dmq::rpc::RemoteDispatcher` is designed to be held as a member (composition, not inheritance — matching `extras/databus`'s `Participant`/`NetworkNode` shape), and it owns the network thread and reliability plumbing ([`TransportMonitor`](../util/TransportMonitor.h), shared with `extras/databus`, not duplicated here) but never constructs or knows the concrete transport type. The owning, application-specific manager owns its own transport (e.g. `Win32UdpTransport`, `ZeroMqTransport`), optionally wraps it in [`ReliableTransport`](../util/ReliableTransport.h)+[`RetryMonitor`](../util/RetryMonitor.h) for ACK/retry reliability, and hands the result to `Attach()`. `RemoteDispatcher` only ever sees `dmq::transport::ITransport`, so it works with any transport that implements it — not a fixed list — and carries no per-transport branching itself.

```cpp
class NetworkMgr
{
public:
    NetworkMgr() {
        // ITransport has no Close(); SetCloseHandler() supplies the callback
        // that closes whatever concrete transport(s) this class owns. Called
        // by m_dispatcher.Stop() before the receive thread is joined.
        m_dispatcher.SetCloseHandler(MakeDelegate(this, &NetworkMgr::CloseTransports));
    }

    int Create() {
        // Construct and open the concrete transport, then hand it up. Wrap in
        // ReliableTransport first if this transport needs ACK/retry reliability.
        m_transport.Create(...);
        m_dispatcher.Attach(m_transport, m_transport);

        m_alarmChannel.emplace(m_dispatcher.GetSendTransport(), m_alarmSer);
        m_alarmChannel->Bind(this, &NetworkMgr::OnAlarm, ALARM_MSG_ID);
        m_dispatcher.RegisterEndpoint(ALARM_MSG_ID, m_alarmChannel->GetEndpoint());
        return 0;
    }

    // Fire-and-forget
    void SendAlarm(AlarmMsg& msg) { (*m_alarmChannel)(msg); }

    // Blocking: waits for ACK or timeout, returns success/failure
    bool SendCommandWait(CommandMsg& msg) {
        return m_dispatcher.RemoteInvokeWait(*m_commandChannel, msg);
    }

private:
    void CloseTransports() { m_transport.Close(); }
    void OnAlarm(AlarmMsg& msg) { /* handle incoming alarm */ }

    // Composed, not inherited.
    dmq::rpc::RemoteDispatcher m_dispatcher;
    dmq::transport::SomeTransport m_transport;
    dmq::serialization::serializer::Serializer<void(AlarmMsg&)> m_alarmSer;
    std::optional<dmq::RemoteChannel<void(AlarmMsg&)>> m_alarmChannel;
    std::optional<dmq::RemoteChannel<void(CommandMsg&)>> m_commandChannel;
};
```

## Key Components

* **`dmq::rpc::RemoteDispatcher.h`**: Manages the internal network thread and marshals calls onto it automatically. `RemoteInvokeWait()` blocks the caller until the remote ACKs or times out — the one capability `DataBus::Publish()` (inherently async/fire-and-forget) doesn't provide.
  * `Attach(sendTransport, recvTransport)` — call once (typically from the owning class's `Create()`, after its own transport member exists) to hand `RemoteDispatcher` the `ITransport&` it sends/receives through.
  * `AttachRetryMonitor(retryMonitor)` — call once, after `Attach()`, only if the owning class layered `RetryMonitor`/`ReliableTransport` on top of its transport. Skip it for a self-reliable transport (e.g. ZeroMQ) that has no `RetryMonitor`.
  * `SetCloseHandler(handler)` — supply a delegate that closes the concrete transport(s) the owning class owns; `ITransport` itself has no `Close()`. Called by `Stop()` before the receive thread is joined. Optional: if never set, this is a no-op.
  * `GetThread()` / `GetTransportMonitor()` / `GetSendTransport()` — public accessors an owning class needs to marshal calls onto the network thread, wire a transport's `TransportMonitor`, and construct `RemoteChannel`s against the send transport.
* **`extras/dispatcher/RemoteChannel.h`**: The recommended way to configure a remote endpoint — aggregates the dispatcher, stream, serializer, and delegate binding for one message signature into a single object. `Bind()` registers the receive-side handler; `GetEndpoint()` returns the `IRemoteInvoker*` to pass to `RegisterEndpoint()`. See [`extras/dispatcher/README.md`](../dispatcher/README.md).

## Transport Support

Any transport implementing `dmq::transport::ITransport` works — `RemoteDispatcher` never constructs or names a concrete transport type itself, so there's no per-transport list to extend. This is also what `extras/databus`'s `Participant` already does generically; `RemoteDispatcher` now follows the same shape rather than the old `NetworkEngine`'s fixed `#if`/`#elif` chain over five transports.
