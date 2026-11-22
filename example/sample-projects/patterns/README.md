# Pattern samples

Application designs built on DelegateMQ, often together with another library.

| Sample | Shows |
| :--- | :--- |
| [state-machine](./state-machine/README.md) | An active-object state machine: [active-fsm](https://github.com/DelegateMQ/active-fsm)'s `StateMachine` on its own thread, with timer-driven transitions and a transition signal observed from another thread. |
| [async-sqlite](./async-sqlite/README.md) | A blocking library on its own thread: [Async-SQLite](https://github.com/DelegateMQ/Async-SQLite) runs every SQLite call on one thread. Concurrent writers with no lock, a transaction sent as one function, futures, timeouts, and a signal to a display thread. |

`build_all.py` builds and runs these samples; the test scripts name them `patterns/<sample>`.
