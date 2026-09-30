# Signal — Publish / Subscribe

`dmq::Signal<Sig>` is the recommended way to implement publish/subscribe within a process. It returns a `dmq::ScopedConnection` handle from `Connect()` that automatically disconnects when it goes out of scope — no manual unsubscribe needed. `Connect()` is `[[nodiscard]]`: discarding the return value causes an immediate disconnect, leaving no active subscription.

---

## Table of Contents

- [Basic Usage](#basic-usage)
- [Lambda Slots](#lambda-slots)
- [Mixed Sync and Async Slots](#mixed-sync-and-async-slots)
- [Disconnect and Calls Already in Progress](#disconnect-and-calls-already-in-progress)
- [When to use `dmq::MulticastDelegateSafe` instead](#when-to-use-dmqmulticastdelegatesafe-instead)

---

## Basic Usage

**Publisher** — declare `dmq::Signal<>` as a plain class member and call it to emit:

```cpp
class Button
{
public:
    dmq::Signal<void(int buttonId)> OnPressed;  // plain member, no shared_ptr needed

    void Press(int id) { OnPressed(id); }       // emit to all connected slots
};
```

**Subscriber** — connect with `dmq::MakeDelegate`, store the `dmq::ScopedConnection`:

```cpp
class UI
{
public:
    UI(Button& btn) : m_thread("UIThread")
    {
        m_thread.CreateThread();

        // Connect: callback will run on m_thread context
        m_conn = btn.OnPressed.Connect(
            dmq::MakeDelegate(this, &UI::HandlePress, m_thread)
        );
    }
    // No destructor needed — m_conn disconnects automatically when UI is destroyed

private:
    void HandlePress(int buttonId)
    {
        std::cout << "Button " << buttonId << " pressed\n";
    }

    dmq::os::Thread m_thread;
    dmq::ScopedConnection m_conn;  // RAII: disconnects on destruction
};

// Usage
Button btn;
{
    UI ui(btn);
    btn.Press(1);   // UI::HandlePress called on UIThread
}                   // ui destroyed -> m_conn disconnects -> no more callbacks
btn.Press(2);       // safe: no subscribers, nothing happens
```

This example emits on one thread. If other threads emit while a subscriber is being destroyed, see [Disconnect and Calls Already in Progress](#disconnect-and-calls-already-in-progress).

---

## Lambda Slots

```cpp
dmq::Signal<void(int)> OnData;

// Stateless lambda
dmq::ScopedConnection c1 = OnData.Connect(dmq::MakeDelegate([](int v) {
    std::cout << "Got: " << v << "\n";
}));

// Capturing lambda — no std::function wrapper needed
int factor = 3;
dmq::ScopedConnection c2 = OnData.Connect(dmq::MakeDelegate(
    [factor](int v) { std::cout << v * factor; }
));

OnData(10);  // both slots called
```

---

## Mixed Sync and Async Slots

Unlike most signal libraries, DelegateMQ lets each subscriber independently choose its execution context. The publisher doesn't need to know:

```cpp
Button btn;

// Subscriber A: synchronous (called on the emitting thread)
dmq::ScopedConnection connA = btn.OnPressed.Connect(
    dmq::MakeDelegate([](int id) { std::cout << "Sync: " << id; })
);

// Subscriber B: asynchronous (called on workerThread)
dmq::ScopedConnection connB = btn.OnPressed.Connect(
    dmq::MakeDelegate([](int id) { std::cout << "Async: " << id; }, workerThread)
);

btn.Press(1);  // connA called synchronously, connB queued on workerThread
```

---

## Disconnect and Calls Already in Progress

`Disconnect()` (or destroying the `dmq::ScopedConnection`) removes the slot, but it does not wait for a call that has already started. A slot can still run once **after** `Disconnect()` returns in two cases:

1. **Another thread is emitting.** `operator()` takes a snapshot of the slots under the lock, then calls them after releasing it, so a slot may itself connect, disconnect or emit without deadlocking. A snapshot taken just before your `Disconnect()` still calls your slot.
2. **An async slot's message is already queued.** `MakeDelegate(..., thread)` queues a message on `thread`; disconnecting doesn't remove messages already queued.

`dmq::MulticastDelegateSafe` and `dmq::UnicastDelegateSafe` behave the same way. With a single thread (connect, emit and disconnect all on one thread) none of this applies.

When other threads emit, disconnecting alone doesn't make the slot's target safe to destroy. Guard the target's lifetime yourself. A small shared gate works: the slot runs only while the gate is open, and closing it waits for a call in progress to finish.

```cpp
struct Gate {
    std::mutex m;
    bool open = true;
};

class Subscriber
{
public:
    explicit Subscriber(Button& btn) : m_gate(std::make_shared<Gate>())
    {
        auto gate = m_gate;   // the slot keeps the gate alive, not the Subscriber
        m_conn = btn.OnPressed.Connect(dmq::MakeDelegate(
            std::function<void(int)>([this, gate](int id) {
                std::lock_guard<std::mutex> lock(gate->m);
                if (gate->open)
                    HandlePress(id);   // only while the Subscriber is alive
            })));
    }

    ~Subscriber()
    {
        {
            std::lock_guard<std::mutex> lock(m_gate->m);   // waits for a call in progress
            m_gate->open = false;
        }
        m_conn.Disconnect();
    }

private:
    void HandlePress(int id);
    std::shared_ptr<Gate> m_gate;
    dmq::ScopedConnection m_conn;
};
```

For an async slot, the same gate check inside the slot covers queued messages too. Alternatively, drain or exit the target thread (`ExitThread()`) before destroying what its messages target.

---

## When to use `dmq::MulticastDelegateSafe` instead

Use `dmq::Signal` by default. Reach for `dmq::MulticastDelegateSafe` only when you need explicit control over subscription timing.

| | `dmq::Signal<Sig>` | `dmq::MulticastDelegateSafe<Sig>` |
| --- | --- | --- |
| **Subscription** | `Connect()` → returns `dmq::ScopedConnection` | `operator+=` → no return value |
| **Unsubscription** | Automatic when `dmq::ScopedConnection` is destroyed | Manual `operator-=` |
| **Lifetime safety** | Disconnects on scope exit, even if Signal is already destroyed. A call already in progress on another thread can still run once ([details](#disconnect-and-calls-already-in-progress)) | Caller responsible; missed `-=` leaves a dangling subscriber. Same in-progress caveat after `-=` |
| **Mixed sync/async slots** | Yes — each subscriber independently chooses its thread | Yes |
| **Prefer when** | Observer pattern, component events, any long-lived subscription | Subscription lifetime is fully explicit and controlled by the caller |
| **Avoid when** | You need to control the exact moment of disconnect | Subscriber lifetime is hard to predict or tied to complex ownership |

```cpp
// dmq::MulticastDelegateSafe — manual subscription management
dmq::MulticastDelegateSafe<void(int)> OnData;
OnData += dmq::MakeDelegate(&obj, &MyClass::Handle, workerThread);
OnData(42);
OnData -= dmq::MakeDelegate(&obj, &MyClass::Handle, workerThread);  // must not forget this
```
