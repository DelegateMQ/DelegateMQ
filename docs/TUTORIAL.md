# DelegateMQ Tutorial: From Local to Distributed

This tutorial guides you through the evolution of a system using DelegateMQ. We will build a simple **"Smart Motor"** system, starting with basic function calls and scaling up to a multi-threaded, remote-node architecture.

## Tutorial Phases
1. [**Synchronous**](#phase-1-the-direct-call-synchronous) - Basic function wrapping.
2. [**Asynchronous**](#phase-2-the-worker-thread-asynchronous) - Moving work to a background thread.
3. [**Signals**](#phase-3-the-observer-signals) - Decoupling with one-to-many events.
4. [**Timers**](#phase-4-periodic-tasks-timers) - Handling periodic heartbeats and timeouts.
5. [**Remote**](#phase-5-the-network-remote-delegates) - Calling functions on a different CPU.
6. [**DataBus**](#phase-6-the-middleware-databus) - Topic-based discovery (DDS Lite).

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
**Key Concept**: `dmq::ScopedConnection` provides RAII safety. If the listener is destroyed, the connection is severed automatically, preventing "dead object" crashes.

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
// (e.g., UdpTransport transport; MsgPackSerializer serializer;)
dmq::RemoteChannel<void(int)> motorChannel(transport, serializer, MOTOR_SPEED_ID);

// 2. Send the command across the wire
motorChannel(3000); 
```

The code on the **Motor Node** (Receiver) maps that same ID to a local function:
```cpp
// --- Motor Node (Receiver) ---
// Bind the ID to a specific handler function
// (Note: motor and transport objects must be defined on this node)
motorChannel.Bind(&motor, &Motor::SetSpeed, MOTOR_SPEED_ID);

// Loop to process incoming network packets
while(running) {
    transport.ProcessIncoming(); 
}
```

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
- **Zero Library Threads**: You control the polling loop.

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
