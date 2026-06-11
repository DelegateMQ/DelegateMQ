# DataBus

The `dmq::databus::DataBus` is a central registry for topic-based communication within DelegateMQ. It provides a flexible publish-subscribe (Pub/Sub) architecture that decouples data producers from consumers.

## Quickstart

Three steps to send data between components:

**1. Define a message type**
```cpp
// Any serializable struct — inherit MessageBase for built-in sequence numbers
struct TemperatureMsg : public serialize::I {
    float celsius = 0.0f;
    std::ostream& write(serialize& ms, std::ostream& os) override { return ms.write(os, celsius); }
    std::istream& read (serialize& ms, std::istream& is) override { return ms.read (is, celsius); }
};
```

**2. Subscribe**
```cpp
// Connection owns the subscription — must stay in scope while callbacks are wanted
auto conn = dmq::databus::DataBus::Subscribe<TemperatureMsg>(
    "sensor/temperature",
    [](const TemperatureMsg& msg) {
        printf("Temp: %.1f C\n", msg.celsius);
    });
```

**3. Publish**
```cpp
TemperatureMsg msg;
msg.celsius = 36.6f;
dmq::databus::DataBus::Publish("sensor/temperature", msg);
// → subscriber lambda fires immediately on the calling thread
```

> **Connection lifetime**: `Subscribe` returns a `dmq::ScopedConnection`. Letting it go out of scope silently unsubscribes — store it in a member variable.

To dispatch to a specific thread instead of the caller's thread:
```cpp
auto conn = dmq::databus::DataBus::Subscribe<TemperatureMsg>(
    "sensor/temperature",
    dmq::MakeDelegate(&MySensor::OnTemp, this),
    &m_workerThread);   // callback executes on m_workerThread, not the publisher's thread
```

To span multiple processes over a network, see [Multi-Process Quickstart — `NetworkNode`](#multi-process-quickstart--networknode) below.

---

## Features

- **Topic-Based Communication**: Components interact via named string topics rather than direct object references.
- **Thread Dispatching**: Subscribers can specify an `dmq::IThread` to have their callbacks executed on a specific thread.
- **Quality of Service (QoS)**: Supports Last Value Cache (LVC) to provide the most recent data to new subscribers immediately upon connection.
- **Filtering**: `SubscribeFilter` allows subscribers to receive only the data that matches a specific predicate.
- **Remote Distribution**: `dmq::databus::Participant` integration allows the `dmq::databus::DataBus` to span multiple physical nodes over any supported transport (UDP, TCP, ZeroMQ, etc.).
- **Monitoring & Spying**: The `Monitor` API allows for global observation of all bus traffic, useful for logging, debugging, or UI dashboards.
- **Type Safety**: Built on C++ templates to ensure type-safe data transmission.

## Basic Usage

### Subscribing to a Topic

```cpp
// Simple subscription on the current thread
auto conn = dmq::databus::DataBus::Subscribe<int>("Temperature", [](const int& value) {
    std::cout << "Temp changed: " << value << std::endl;
});

// Subscription dispatched to a specific worker thread
auto conn2 = dmq::databus::DataBus::Subscribe<int>("Temperature",
    dmq::MakeDelegate(&MyClass::OnTempChange, &myObj), &workerThread);
```

### Publishing to a Topic

```cpp
dmq::databus::DataBus::Publish<int>("Temperature", 25);
```

---

## Multi-Process Quickstart — `NetworkNode`

`dmq::databus::NetworkNode<Transport>` is a ready-made multi-peer network layer.
It handles transport setup, Participant creation, reliability stacking, and the
receive-loop thread. A new user writes only a `SetupNetwork()` function.

### Minimal two-node example

**`shared/Topics.h`** — shared between all nodes:
```cpp
#include "DelegateMQ.h"
#include "messages/TemperatureMsg.h"
#include "messages/AlarmMsg.h"

namespace myapp {
    // Remote IDs — unique per message type, consistent across nodes
    static constexpr dmq::DelegateRemoteId RID_TEMPERATURE = 1;
    static constexpr dmq::DelegateRemoteId RID_ALARM       = 2;

    namespace topics {
        static constexpr const char* TEMPERATURE = "sensor/temperature";
        static constexpr const char* ALARM       = "sys/alarm";
    }

    // Serializer instances (defined in one .cpp, extern everywhere else)
    extern dmq::serialization::serializer::Serializer<void(TemperatureMsg)> serTemp;
    extern dmq::serialization::serializer::Serializer<void(AlarmMsg)>       serAlarm;
}
```

**`node_a/System.cpp`** — the sensor node (publishes temperature, receives alarms):
```cpp
#include "extras/databus/NetworkNode.h"
#include "port/transport/win32-udp/Win32UdpTransport.h"  // or LinuxUdpTransport
#include "Topics.h"

using Network = dmq::databus::NetworkNode<dmq::transport::Win32UdpTransport>;
static Network g_net;

void SetupNetwork() {
    g_net.Start("SensorNode", /*listenPort=*/6000);

    g_net.Receive<AlarmMsg>(myapp::topics::ALARM, myapp::RID_ALARM, myapp::serAlarm);

    g_net.AddPeer("DisplayNode", "127.0.0.1", /*udpPort=*/6001);

    g_net.Send<TemperatureMsg>(myapp::topics::TEMPERATURE, myapp::RID_TEMPERATURE,
                               myapp::serTemp);
}

// From here: DataBus::Publish<TemperatureMsg>(topics::TEMPERATURE, reading) routes
// automatically over the network to DisplayNode.
```

**`node_b/System.cpp`** — the display node (receives temperature, publishes alarms):
```cpp
void SetupNetwork() {
    g_net.Start("DisplayNode", /*listenPort=*/6001);

    g_net.Receive<TemperatureMsg>(myapp::topics::TEMPERATURE, myapp::RID_TEMPERATURE,
                                  myapp::serTemp);

    g_net.AddPeer("SensorNode", "127.0.0.1", /*udpPort=*/6000);

    g_net.Send<AlarmMsg>(myapp::topics::ALARM, myapp::RID_ALARM, myapp::serAlarm,
                         dmq::databus::Reliability::RELIABLE);  // ACK + retry
}
```

### Key points

- **Order-independent**: `Receive()` and `Send()` may be called before or after `Start()` / `AddPeer()`. Registrations are stored and applied retroactively.
- **Reliability tiers**: `Reliability::UNRELIABLE` (default) sends raw UDP; `Reliability::RELIABLE` adds ACK + automatic retry via `RetryMonitor`.
- **Transport-agnostic**: Pass any `ITransport`-derived type as the template argument — `Win32UdpTransport`, `LinuxUdpTransport`, `ZephyrUdpTransport`, etc.
- **Fixed allocation**: `MaxPeers` and `MaxTopics` template parameters control pre-allocated capacity. No heap for transport objects (`RemoteNode` members are by-value in `std::array`).
- **`Participant` allocation**: Uses `xmake_shared` — fixed-block allocator on embedded targets.

### Template parameters

```cpp
template <typename Transport,
          size_t MaxPeers  = 4,   // maximum remote peers
          size_t MaxTopics = 16>  // maximum Send() or Receive() calls each
class NetworkNode;
```

---

## Internal Mechanics

The `dmq::databus::DataBus` utilizes DelegateMQ's `dmq::MulticastDelegate` system internally. When you `Publish`, the bus identifies all local and remote subscribers for that topic and invokes them. Remote subscribers are handled via `dmq::IDispatcher` and `dmq::transport::ITransport` layers, making the network boundary transparent to the application logic.
