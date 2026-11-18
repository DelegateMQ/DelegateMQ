# DelegateMQ Sample Code

Short, single-file examples of common patterns built with DelegateMQ. They aren't standalone projects: the unit-test build in [test/](../../test/main.cpp) compiles them into `delegate_app` and runs them (see [Getting Started](../../README.md#getting-started)).

| File | Shows |
| :--- | :--- |
| [ActiveObject.cpp](ActiveObject.cpp) | An active object: a class whose methods run on its own thread |
| [AllTargets.cpp](AllTargets.cpp) | Every kind of target (free, member, static, lambda, `std::function`) in one delegate container |
| [AsyncAPI.cpp](AsyncAPI.cpp) | An asynchronous API built with a template helper function |
| [AsyncCallback.cpp](AsyncCallback.cpp) | Receiving asynchronous callbacks on a chosen thread |
| [AsyncFuture.cpp](AsyncFuture.cpp) | `std::async` and `std::future` with delegates, so the caller continues while the target thread works |
| [AsyncMethodInvocation.cpp](AsyncMethodInvocation.cpp) | The asynchronous method invocation (AMI) pattern |
| [AsyncWorker.cpp](AsyncWorker.cpp) | A dedicated worker thread using `AsyncInvoke` |
| [BindingProperty.cpp](BindingProperty.cpp) | The binding property pattern |
| [Command.cpp](Command.cpp) | The command pattern |
| [Coroutine.cpp](Coroutine.cpp) | C++20 coroutines with async delegates (`co_await`) |
| [CountdownLatch.cpp](CountdownLatch.cpp) | A countdown latch with C++20 `std::latch` and delegates |
| [DataBus.cpp](DataBus.cpp) | Publishing and subscribing to a DataBus topic |
| [MessagePriority.cpp](MessagePriority.cpp) | `dmq::Priority::URGENT` messages overtaking queued normal work |
| [Observer.cpp](Observer.cpp) | The observer pattern |
| [Proactor.cpp](Proactor.cpp) | The proactor pattern |
| [ProducerConsumer.cpp](ProducerConsumer.cpp) | Producer/consumer with asynchronous delegates |
| [Reactor.cpp](Reactor.cpp) | The reactor pattern, with a delegate storing and invoking the event handlers |
| [RemoteCommunication.cpp](RemoteCommunication.cpp) | A complete remote delegate over a mock transport |
| [ReplyToCaller.cpp](ReplyToCaller.cpp) | An async service that delivers each completion on the thread that made the request |
| [SafeTimer.cpp](SafeTimer.cpp) | Timers with RAII `ScopedConnection`s |
| [ScatterGather.cpp](ScatterGather.cpp) | Scatter-gather: work dispatched to several threads at once, results collected |
| [SignalSlot.cpp](SignalSlot.cpp) | Signals and slots |
| [SignalSlotNoHeap.cpp](SignalSlotNoHeap.cpp) | Signals with minimal heap use, for embedded targets |
| [SysData.cpp](SysData.cpp) | Thread-safe shared system data that notifies registered clients when it changes (with a lock) |
| [SysDataNoLock.cpp](SysDataNoLock.cpp) | The same without a lock: changes are made on one thread through an async delegate |
