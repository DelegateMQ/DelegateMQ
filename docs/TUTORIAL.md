# DelegateMQ Tutorial: From Local to Distributed

This tutorial guides you through the evolution of a system using DelegateMQ. We will build a simple **"Smart Motor"** system, starting with basic function calls and scaling up to a multi-threaded, remote-node architecture.

## Tutorial Phases
1. [**Synchronous**](#phase-1-the-direct-call-synchronous) - Basic function wrapping.
2. [**Asynchronous**](#phase-2-the-worker-thread-asynchronous) - Moving work to a background thread.
3. [**Signals**](#phase-3-the-observer-signals) - Decoupling with one-to-many events.
4. [**Timers**](#phase-4-periodic-tasks-timers) - Handling periodic heartbeats and timeouts.
5. [**Remote**](#phase-5-the-network-remote-delegates) - Calling functions on a different CPU.
6. [**DataBus**](#phase-6-the-middleware-databus) - Topic-based publish/subscribe (DDS Lite).

---

## Phase 1: The Direct Call (Synchronous)

Imagine a `Motor` class. In a traditional system, you call its methods directly. With DelegateMQ, we wrap these calls in a **Delegate**.

```cpp
#include "DelegateMQ.h"

class Motor {
public:
    void SetSpeed(int rpm) { 
        std::cout << "Motor speed set to " << rpm << " RPM\n"; 
    }
};

int main() {
    Motor motor;

    // 1. Create a delegate bound to the motor instance
    auto setSpeed = dmq::MakeDelegate(&motor, &Motor::SetSpeed);

    // 2. Invoke it like a normal function
    setSpeed(1500); 

    return 0;
}
```
**Why do this?** You've just created a type-safe "command" object. You can pass this `setSpeed` object to other components without them needing to know anything about the `Motor` class.

---

## Phase 2: The Worker Thread (Asynchronous)

In real-time systems, you don't want your GUI or Main thread to block while the Motor controller processes a command. We need to move the work to a **Worker Thread**.

**The Magic of DelegateMQ**: It's a one-line change.

```cpp
int main() {
    Motor motor;

    // 1. Create and start a hardware worker thread
    dmq::os::Thread motorThread("MotorThread");
    motorThread.CreateThread();

    // 2. Add the 'motorThread' argument to the delegate
    // That's it! The call will now automatically marshal to the other thread.
    auto asyncSetSpeed = dmq::MakeDelegate(&motor, &Motor::SetSpeed, motorThread);

    // 3. This returns immediately. Motor::SetSpeed runs on 'motorThread'.
    asyncSetSpeed(2500);

    return 0;
}
```
**Key Concept**: DelegateMQ handles the internal message queue, argument copying (marshalling), and thread signaling for you.

---

## Phase 3: The Observer (Signals)

Now the Motor needs to report its *actual* speed back to multiple listeners (a GUI, a Logger, and a Safety Monitor). We use a **Signal**.

```cpp
class Motor {
public:
    // A signal that broadcasts the speed changes
    dmq::Signal<void(int)> OnSpeedChanged;

    void SetSpeed(int rpm) {
        std::cout << "Motor speed set to " << rpm << " RPM\n"; 
        // Emit the signal to all connected slots
        OnSpeedChanged(rpm);
    }
};

class Logger {
public:
    void Log(int rpm) { std::cout << "LOG: Speed is " << rpm << "\n"; }
};

int main() {
    Motor motor;
    Logger logger;
    dmq::os::Thread logThread("LogThread");
    logThread.CreateThread();

    // Connect the logger to the motor. 
    // We want the logging to happen on the 'logThread'.
    dmq::ScopedConnection conn = motor.OnSpeedChanged.Connect(
        dmq::MakeDelegate(&logger, &Logger::Log, logThread)
    );

    motor.SetSpeed(2499);
    return 0;
} // 'conn' goes out of scope -> logger is automatically disconnected.
```
**Key Concept**: `dmq::ScopedConnection` provides RAII safety. Destroying it disconnects the slot, so keep it as a member of the listener and the connection ends when the listener does. If other threads emit, a call already in progress can still run once; see [SIGNALS.md](SIGNALS.md#disconnect-and-calls-already-in-progress).

### Alternative: Manual Management
If you prefer manual control over subscriptions (common in C#), you can use `dmq::MulticastDelegateSafe`.

```cpp
dmq::MulticastDelegateSafe<void(int)> OnData;

// 1. Subscribe using +=
OnData += dmq::MakeDelegate(&logger, &Logger::Log, logThread);

// 2. You MUST manually unsubscribe using -= before 'logger' is destroyed
OnData -= dmq::MakeDelegate(&logger, &Logger::Log, logThread);
```

---

## Phase 4: Periodic Tasks (Timers)

Timers are essential for heartbeats, watchdogs, and periodic sensor polling. DelegateMQ provides a thread-safe `dmq::util::Timer`.

```cpp
#include "extras/util/Timer.h"

int main() {
    dmq::os::Thread logThread("LogThread");
    logThread.CreateThread();

    dmq::util::Timer heartbeatTimer;
    bool running = true;

    // 1. Connect a callback to the timer
    // We want the heartbeat to run on the LogThread
    auto conn = heartbeatTimer.OnExpired.Connect(
        dmq::MakeDelegate([]() { std::cout << "HEARTBEAT\n"; }, logThread)
    );

    // 2. Start the timer (1000ms, periodic)
    heartbeatTimer.Start(std::chrono::milliseconds(1000));

    // 3. IMPORTANT: You must call ProcessTimers() periodically
    // This is typically done in your main loop or a high-priority thread.
    while (running) {
        dmq::util::Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
```
**Key Concept**: `ProcessTimers()` is the engine that drives all timers. It must be called periodically from an application-owned thread.

---

## Phase 5: The Network (Remote Delegates)

Your system grows. You move the `Motor` logic to a dedicated Microcontroller, but your `Controller` code is on a PC. We use **Remote Delegates**.

```cpp
// shared.h - Shared ID known by both nodes
constexpr dmq::DelegateRemoteId MOTOR_SPEED_ID = 100;

// --- Controller Node (Sender) ---
// 1. Create a channel using a transport and a serializer, passing the remote ID
// (e.g., dmq::transport::UdpTransport transport;
//        dmq::serialization::msgpack::Serializer<void(int)> serializer;)
dmq::RemoteChannel<void(int)> motorChannel(transport, serializer, MOTOR_SPEED_ID);

// 2. Send the command across the wire
motorChannel(3000); 
```

The code on the **Motor Node** (Receiver) maps that same ID to a local function:
```cpp
// --- Motor Node (Receiver) ---
// (Note: motor, transport and serializer objects must be defined on this node)
dmq::RemoteChannel<void(int)> motorChannel(transport, serializer);

// Bind the ID to a specific handler function
motorChannel.Bind(&motor, &Motor::SetSpeed, MOTOR_SPEED_ID);

// The dispatcher owns the receive thread and routes each incoming ID to its channel
dmq::rpc::RemoteDispatcher dispatcher;
dispatcher.Attach(transport, transport);  // send and receive transport
dispatcher.RegisterEndpoint(MOTOR_SPEED_ID, motorChannel);
dispatcher.Start();  // incoming MOTOR_SPEED_ID messages now invoke Motor::SetSpeed
```

### Catching Errors
A remote call can fail where a local one can't: the serializer can fail, a frame can arrive corrupt, or the message ID can have no receiver. These errors don't throw. Each is reported as a `dmq::DelegateError` to an error handler, so register one on each side.

```cpp
// Sender: catch errors raised while sending on this channel
void OnMotorError(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux) {
    std::cout << "Remote ID " << id << " error " << static_cast<int>(error) << "\n";
}
motorChannel.SetErrorHandler(dmq::MakeDelegate(&OnMotorError));

// Receiver: RegisterEndpoint() routes each channel's errors to dispatcher.OnError.
// Store the connection, or the subscription ends immediately.
dmq::ScopedConnection errConn = dispatcher.OnError.Connect(dmq::MakeDelegate(&OnMotorError));
```

Common errors are `ERR_SERIALIZE`/`ERR_DESERIALIZE` (serializer failure), `ERR_TRANSPORT_RECEIVE` (bad frame or socket error) and `ERR_NO_DISPATCHER`. For reliable delivery, a transport with ACK and retry reports lost messages through `dispatcher.OnDeliveryFailed` once retries run out, and `dispatcher.OnStatus` reports each `TIMEOUT` while a message is still being retried.

---

## Phase 6: The Middleware (DataBus)

Managing individual IDs and IP addresses becomes tedious. The **DataBus** provides "Location Transparency." You just care about the **Topic Name**.

```cpp
// Component A (Publisher)
dmq::databus::DataBus::Publish<int>("motor/set_speed", 3500);

// Component B (Subscriber - could be local or remote!)
auto conn = dmq::databus::DataBus::Subscribe<int>("motor/set_speed", [](int speed) {
    std::cout << "Received speed request: " << speed << "\n";
}, &motorThread);
```

### High-Level Features:
- **Targeted Thread Dispatch**: Subscribers specify which thread they run on (e.g., `&motorThread`), ensuring thread-safe delivery without manual queuing.
- **LVC (Last Value Cache)**: New subscribers get the most recent data immediately.
- **Monitoring**: Use the `dmq-spy` tool to see every message on the bus in real-time.
- **No Hidden Threads**: The DataBus creates no threads. You poll for incoming data, or a `NetworkNode` runs one receive thread for you.

### Catching Errors
Publishing never throws. When something goes wrong (a topic published with two different types, a serializer failure, too many participants), the DataBus reports the topic and a `dmq::DelegateError` to `SubscribeError` subscribers:

```cpp
auto errConn = dmq::databus::DataBus::SubscribeError(
    [](const dmq::xstring& topic, dmq::DelegateError error) {
        std::cout << "DataBus error on " << topic << ": " << static_cast<int>(error) << "\n";
    });
```

- **Keep the connection**: `SubscribeError` is `[[nodiscard]]`. If the `ScopedConnection` is discarded, the subscription ends at once and no errors are reported.
- **Reported once**: each error is reported once per topic, so a fault inside a fast publish loop doesn't flood the handler. Call `DataBus::EnableContinuousErrors(true)` to report every occurrence.
- **Per node**: `Participant::SubscribeError` catches errors for a single remote node only.
- **Hard faults**: a programming error, such as `ERR_TYPE_MISMATCH` or `ERR_CAPACITY_EXCEEDED`, is reported to `SubscribeError` first and then faults (assert or exception), so the handler gets one last diagnostic.

---

## Summary: Which to Use?

| Scenario | Recommendation |
| :--- | :--- |
| Simple internal callback | `dmq::Delegate` |
| Move work to another thread | `dmq::DelegateAsync` |
| Multiple observers / Events | `dmq::Signal` |
| Periodic heartbeats / Timeouts | `dmq::util::Timer` |
| Point-to-point Network call | `dmq::RemoteChannel` |
| Large system data sharing | `dmq::databus::DataBus` |

## Next Steps
- Read **[DETAILS.md](DETAILS.md)** for advanced threading and memory management.
- Explore **[PORTING.md](PORTING.md)** to run DelegateMQ on your specific RTOS.
- Check the **[Sample Projects](../example/sample-projects/README.md)** for working UDP/ZeroMQ/MQTT examples.
