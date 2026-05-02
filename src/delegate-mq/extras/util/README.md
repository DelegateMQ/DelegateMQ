# Utility & Helper Layer

This directory contains concrete utility classes and helper functions that complement the core DelegateMQ library. 

While the core library (`namespace dmq`) provides the fundamental mechanism for delegates and threading interfaces, this directory provides the **"batteries included"** implementations required to build real-world applications (Timers, Reliability, Networking logic, etc.).

> **Note:** Code in this directory resides in the **`dmq::util`** namespace. 

## Functional Modules

### 1. Asynchronous Helpers
* **`dmq::util::AsyncInvoke.h`**: A powerful template helper that allows you to fire any function (lambda, member, free) onto any thread in a single line of code.
    * *Example:* `dmq::util::AsyncInvoke(myFunc, workerThread, 1s, arg1);`

### 2. Timing & Scheduling
* **`dmq::util::Timer.h`**: A complete software timer system capable of one-shot and periodic callbacks via delegates. Supports millisecond and microsecond precision depending on the platform clock.
* **`dmq::util::TimerDelegate.h`**: A stateful delegate wrapper that prevents thread queue flooding. It ensures at most one timer message is in the target thread's queue at any time, providing safe backpressure for high-frequency timers.

### 3. Reliability Layer (QoS)
These classes provide transport-layer reliability (ACKs and Retries) over unreliable media (like UDP or Serial).
* **`dmq::util::TransportMonitor.h`**: Tracks outgoing messages and handles sequence numbers.
* **`dmq::util::RetryMonitor.h`**: Logic to detect lost packets and trigger re-transmissions.
* **`dmq::util::ReliableTransport.h`**: A composite transport that wraps a raw transport (e.g., UDP) and adds reliability logic transparently.

### 4. Networking Logic
* **`dmq::util::NetworkEngine.h`**: A high-level manager that coordinates the `dmq::util::Dispatcher` and `ITransport` to simplify sending messages to remote endpoints.
* **`dmq::util::NetworkConnect.h`**: Platform-specific socket initialization helpers (e.g., `dmq::util::NetworkContext`) to handle networking boilerplate like `WSAStartup`.
* **`dmq::util::RemoteEndpoint.h`**: Base class for `dmq::DelegateMemberRemote` used to register receive-side endpoints with `dmq::util::NetworkEngine`.

### 5. Monotonic Messaging and Time
These utilities help handle message ordering and timestamping in distributed systems.
* **`dmq::util::ClockHelper.h`**: Simplified functions to get monotonic timestamps (`TimestampMs()`, `TimestampUs()`) across all supported platforms.
* **`dmq::util::MonotonicGuard.h`**: A generic template class to filter out-of-order or stale messages. Handles 32-bit rollover logic automatically and provides simple 64-bit monotonic checks.

### 6. System Utilities
* **`dmq::util::Fault.h`**: Assertions and fault trapping macros (`ASSERT_TRUE`, `FAULT_Handler`) used throughout the library examples.
* **`dmq::util::crc16.h`**: Checksum utility for data integrity in serial/UDP headers.

## Usage

Include these headers when you need specific functionality beyond basic delegates. For example, if you need to schedule a periodic callback:

```cpp
#include "extras/util/Timer.h"

dmq::util::Timer myTimer;
// Use a delegate to receive timer callbacks
myTimer.OnExpired.Connect(dmq::MakeDelegate(&myClass, &MyClass::OnTimeout));
myTimer.Start(1000); // 1-second periodic timer
```

Or to invoke a function on a specific thread with a 5-second timeout:

```cpp
#include "extras/util/AsyncInvoke.h"

// Fires and waits up to 5s for completion on workerThread
dmq::util::AsyncInvoke(myWorkerFunc, workerThread, 5s, 123, "hello");
```
