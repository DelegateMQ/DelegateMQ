#ifndef SIGNAL_H
#define SIGNAL_H

/// @file Signal.h
/// @brief Thread-safe Signal/slot with RAII connection handles; no allocation requirement.
///
/// @details `Signal<Sig>` is a thread-safe multicast delegate container that returns RAII
/// `ScopedConnection` handles from `Connect()`. Key properties:
///
/// * **No allocation requirement** — Signal may live on the stack, as a class member, or on
///   the heap without restriction.
/// * **Thread-safe** — concurrent Connect/Disconnect/operator() calls are all safe.
/// * **Lifetime-safe disconnect** — calling `Disconnect()` (or letting a `ScopedConnection`
///   go out of scope) after the Signal is destroyed is always a safe no-op.
///
/// Internally, Signal stores its subscriber list in a heap-allocated `State` block. Each
/// `Connection` holds two `shared_ptr<void>` context fields (state and delegate copy) plus
/// a raw `DisconnectImpl` function pointer. The destructor marks the block dead under the
/// mutex, so any concurrent disconnect that races with destruction simply sees the dead flag
/// and returns without touching the list.

#include "DelegateOpt.h"
#include "Delegate.h"
#include <memory>

DMQ_OPTIMIZE_ON

namespace dmq {

template <class R>
class UnicastDelegate;

template <class R>
class Signal;

// ---------------------------------------------------------------------------
// detail::Connection  (implementation detail — use ScopedConnection)
// ---------------------------------------------------------------------------
namespace detail {

/// @brief Move-only subscription token. Internal implementation detail of Signal.
/// Users should store `ScopedConnection`, not `Connection` directly.
///
/// Stores two type-erased `shared_ptr<void>` contexts (signal state and delegate copy)
/// plus a raw `DisconnectFn` function pointer. No `std::function` heap allocation.
class Connection {
public:
    using DisconnectFn = void(*)(const std::shared_ptr<void>&, const std::shared_ptr<void>&);

    Connection() = default;

    Connection(std::shared_ptr<void> ctx1, std::shared_ptr<void> ctx2, DisconnectFn fn) noexcept
        : m_ctx1(std::move(ctx1))
        , m_ctx2(std::move(ctx2))
        , m_disconnect_fn(fn)
        , m_connected(true) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept
        : m_ctx1(std::move(other.m_ctx1))
        , m_ctx2(std::move(other.m_ctx2))
        , m_disconnect_fn(other.m_disconnect_fn)
        , m_connected(other.m_connected) {
        other.m_connected = false;
        other.m_disconnect_fn = nullptr;
    }

    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            Disconnect();
            m_ctx1 = std::move(other.m_ctx1);
            m_ctx2 = std::move(other.m_ctx2);
            m_disconnect_fn = other.m_disconnect_fn;
            m_connected = other.m_connected;
            other.m_connected = false;
            other.m_disconnect_fn = nullptr;
        }
        return *this;
    }

    ~Connection() {}

    bool IsConnected() const { return m_connected; }

    void Disconnect() {
        if (!m_connected) return;
        if (m_disconnect_fn)
            m_disconnect_fn(m_ctx1, m_ctx2);
        m_ctx1.reset();
        m_ctx2.reset();
        m_disconnect_fn = nullptr;
        m_connected = false;
    }

private:
    std::shared_ptr<void> m_ctx1;
    std::shared_ptr<void> m_ctx2;
    DisconnectFn m_disconnect_fn = nullptr;
    bool m_connected = false;
    XALLOCATOR
};

} // namespace detail

// ---------------------------------------------------------------------------
// ScopedConnection
// ---------------------------------------------------------------------------

/// @brief RAII handle to a single Signal subscription. Disconnects automatically
/// on destruction. The only connection type users need to store.
///
/// @code
///   dmq::ScopedConnection conn = mySignal.Connect(MakeDelegate(...));
///   // conn goes out of scope -> automatically disconnected
/// @endcode
class ScopedConnection {
public:
    ScopedConnection() = default;
    explicit ScopedConnection(detail::Connection&& conn) : m_connection(std::move(conn)) {}
    ~ScopedConnection() { m_connection.Disconnect(); }

    ScopedConnection(ScopedConnection&& other) noexcept
        : m_connection(std::move(other.m_connection)) {}

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            m_connection.Disconnect();
            m_connection = std::move(other.m_connection);
        }
        return *this;
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    void Disconnect() { m_connection.Disconnect(); }
    bool IsConnected() const { return m_connection.IsConnected(); }

private:
    detail::Connection m_connection;
    XALLOCATOR
};

// ---------------------------------------------------------------------------
// detail::Signal* helpers  (implementation detail of Signal, defined here so
// they can construct/return ScopedConnection)
// ---------------------------------------------------------------------------
namespace detail {

/// @brief Non-templated: the mutex + alive-flag + delegate list shared by every
/// `Signal<Sig>` in the program. Hoisted out of `Signal<Sig>` (where it lived as
/// a nested `State` struct) because nesting inside the class template caused a
/// separate (byte-identical) type -- and critically a separate `xmake_shared`
/// allocation control block -- to be generated per signature, even though none
/// of its members depend on RetType/Args.
struct SignalState {
    mutable RecursiveMutex mtx;
    bool alive = true;
    xlist<std::shared_ptr<DelegateBase>> delegates;
    XALLOCATOR
};

/// @brief Non-templated: a snapshot of a signal's subscriber list at a point in
/// time, captured under lock so `operator()` can invoke without holding it.
struct SignalSnapshot {
    std::shared_ptr<DelegateBase> small_buf[SIGNAL_SBO_COUNT];
    xlist<std::shared_ptr<DelegateBase>> large_buf;
    size_t count = 0;
};

/// @brief Identity-based disconnect: removes `copyVoid` (the exact shared_ptr
/// instance stored at Connect() time) from `stateVoid`'s delegate list, if the
/// state is still alive. Non-templated: this never needed the concrete
/// `DelegateType` to begin with, but as a member of `Signal<Sig>` it used to be
/// emitted once per signature anyway.
inline void SignalDisconnectImpl(const std::shared_ptr<void>& stateVoid, const std::shared_ptr<void>& copyVoid) {
    auto* state = static_cast<SignalState*>(stateVoid.get());
    auto copy = std::static_pointer_cast<DelegateBase>(copyVoid);
    dmq::LockGuard<RecursiveMutex> lock(state->mtx);
    if (state->alive)
        state->delegates.remove(copy);
}

/// @brief Clone `delegate` and append it to `state`'s list under lock, returning
/// a connection handle wired to `SignalDisconnectImpl`. `delegate` is taken as
/// `const DelegateBase&` (rather than the more-derived `Delegate<Sig>&`) so
/// `delegate.Clone()`'s return type here is `DelegateBase*` -- the resulting
/// shared_ptr control block is therefore not templated on RetType/Args, and is
/// shared by every `Signal<Sig>` in the program instead of duplicated per
/// signature (mirrors the fix already applied to `MulticastDelegate::PushBack`).
inline ScopedConnection SignalConnect(std::shared_ptr<SignalState>& state, RecursiveMutex& mutex, const DelegateBase& delegate) {
    auto copy = std::shared_ptr<DelegateBase>(delegate.Clone(), std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
    if (!copy)
        BAD_ALLOC();

    dmq::LockGuard<RecursiveMutex> lock(mutex);
    dmq::LockGuard<RecursiveMutex> stateLock(state->mtx);
    state->delegates.push_back(copy);

    return ScopedConnection(Connection(
        std::static_pointer_cast<void>(state),
        std::static_pointer_cast<void>(copy),
        &SignalDisconnectImpl
    ));
}

/// @brief Capture a snapshot of `state`'s current delegate list under lock.
inline SignalSnapshot SignalGetSnapshot(const std::shared_ptr<SignalState>& state, RecursiveMutex& mutex) {
    dmq::LockGuard<RecursiveMutex> lock(mutex);
    dmq::LockGuard<RecursiveMutex> stateLock(state->mtx);

    SignalSnapshot s;
    s.count = state->delegates.size();
    if (s.count <= SIGNAL_SBO_COUNT) {
        size_t i = 0;
        for (auto& d : state->delegates) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            s.small_buf[i++] = d;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }
    } else {
        s.large_buf = state->delegates;
    }
    return s;
}

/// @brief Disconnect all subscribers: swap in a fresh `SignalState`, mark the
/// old one dead+cleared under its own lock. Used by `Signal::Clear()`.
inline void SignalClear(std::shared_ptr<SignalState>& state, RecursiveMutex& mutex) {
    std::shared_ptr<SignalState> oldState;
    {
        dmq::LockGuard<RecursiveMutex> lock(mutex);
        oldState = state;
        state = xmake_shared<SignalState>();
    }
    {
        dmq::LockGuard<RecursiveMutex> lock(oldState->mtx);
        oldState->alive = false;
        oldState->delegates.clear();
    }
}

/// @brief Mark `state` dead and clear its list under lock. Used by `~Signal()`
/// -- unlike `SignalClear`, does not swap in a replacement state, since the
/// owning Signal is being destroyed rather than reset for reuse.
inline void SignalMarkDead(std::shared_ptr<SignalState>& state, RecursiveMutex& mutex) {
    dmq::LockGuard<RecursiveMutex> lock(mutex);
    dmq::LockGuard<RecursiveMutex> stateLock(state->mtx);
    state->alive = false;
    state->delegates.clear();
}

/// @brief Lock-protected subscriber count.
inline size_t SignalSize(const std::shared_ptr<SignalState>& state, RecursiveMutex& mutex) {
    dmq::LockGuard<RecursiveMutex> lock(mutex);
    dmq::LockGuard<RecursiveMutex> stateLock(state->mtx);
    return state->delegates.size();
}

} // namespace detail

// ---------------------------------------------------------------------------
// Signal
// ---------------------------------------------------------------------------

/// @brief Thread-safe multicast delegate returning RAII connection handles.
/// @details May be instantiated on the stack, as a class member, or on the heap.
/// `Signal<Sig>` is the single signal type in the library.
template<class RetType, class... Args>
class Signal<RetType(Args...)>
{
public:
    using DelegateType = Delegate<RetType(Args...)>;

    /// @brief Snapshot of the subscriber list at a point in time. Alias for the
    /// non-templated `detail::SignalSnapshot` -- kept as a nested name here since
    /// external code (e.g. `extras/util/Timer.cpp`) names it as `Signal<Sig>::Snapshot`.
    using Snapshot = detail::SignalSnapshot;

    Signal() = default;

    ~Signal() {
        // Mark the shared state dead under the lock. Any concurrent Disconnect()
        // that races with this will either complete its removal first (holding the
        // lock) or see alive=false and skip removal. Either way, no UAF.
        detail::SignalMarkDead(m_state, m_mutex);
    }

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    /// @brief Subscribe a delegate and return a RAII connection handle.
    /// @details The returned `ScopedConnection` automatically disconnects on
    /// scope exit. Safe to call regardless of how the Signal was allocated.
    /// @return A `ScopedConnection`. Let it go out of scope to auto-disconnect,
    ///         or call `Disconnect()` manually.
    [[nodiscard]] ScopedConnection Connect(const DelegateType& delegate) {
        return detail::SignalConnect(m_state, m_mutex, delegate);
    }

    /// @brief Subscribe a UnicastDelegate and return a RAII connection handle.
    [[nodiscard]] ScopedConnection Connect(const UnicastDelegate<RetType(Args...)>& delegate) {
        if (delegate.GetDelegate())
            return Connect(*delegate.GetDelegate());
        return {};
    }

    /// @brief Invoke all connected delegates.
    void operator()(Args... args) {
        auto snapshot = GetSnapshot();
        InvokeSnapshot(snapshot, std::forward<Args>(args)...);
    }

    /// @brief Capture a snapshot of all current subscribers.
    /// @details The snapshot holds shared_ptrs to the delegates, ensuring they
    /// stay alive even if the Signal is destroyed.
    Snapshot GetSnapshot() const {
        return detail::SignalGetSnapshot(m_state, m_mutex);
    }

    /// @brief Invoke a previously captured snapshot of delegates.
    static void InvokeSnapshot(const Snapshot& s, Args... args) {
        if (s.count <= SIGNAL_SBO_COUNT) {
            for (size_t i = 0; i < s.count; ++i) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                if (s.small_buf[i])
                    (*static_cast<DelegateType*>(s.small_buf[i].get()))(args...);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            }
        } else {
            for (auto& d : s.large_buf) {
                if (d)
                    (*static_cast<DelegateType*>(d.get()))(args...);
            }
        }
    }

    /// @brief Number of currently connected subscribers.
    std::size_t Size() const {
        return detail::SignalSize(m_state, m_mutex);
    }

    bool Empty() const { return Size() == 0; }

    /// @brief Disconnect all subscribers.
    void Clear() {
        detail::SignalClear(m_state, m_mutex);
    }

    XALLOCATOR

private:
    mutable RecursiveMutex m_mutex;
    std::shared_ptr<detail::SignalState> m_state = xmake_shared<detail::SignalState>();
};

} // namespace dmq

DMQ_OPTIMIZE_OFF

#endif // SIGNAL_H
