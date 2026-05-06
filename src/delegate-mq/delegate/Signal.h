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
/// Internally, Signal stores its subscriber list in a heap-allocated `State` block shared
/// with the disconnect lambdas via `shared_ptr`. The destructor marks the block dead under
/// the mutex, so any concurrent disconnect that races with destruction simply sees the
/// dead flag and returns without touching the list.

#include "DelegateOpt.h"
#include "Delegate.h"
#include <functional>
#include <memory>

namespace dmq {

template <class R>
class Signal;

// ---------------------------------------------------------------------------
// detail::Connection  (implementation detail — use ScopedConnection)
// ---------------------------------------------------------------------------
namespace detail {

/// @brief Move-only subscription token. Internal implementation detail of Signal.
/// Users should store `ScopedConnection`, not `Connection` directly.
class Connection {
public:
    Connection() = default;

    template<typename DisconnectFunc>
    Connection(std::weak_ptr<void> watcher, DisconnectFunc&& func)
        : m_watcher(watcher)
        , m_disconnect(std::forward<DisconnectFunc>(func))
        , m_connected(true) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept
        : m_watcher(std::move(other.m_watcher))
        , m_disconnect(std::move(other.m_disconnect))
        , m_connected(other.m_connected) {
        other.m_connected = false;
        other.m_disconnect = nullptr;
    }

    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            Disconnect();
            m_watcher    = std::move(other.m_watcher);
            m_disconnect = std::move(other.m_disconnect);
            m_connected  = other.m_connected;
            other.m_connected  = false;
            other.m_disconnect = nullptr;
        }
        return *this;
    }

    ~Connection() {}

    bool IsConnected() const { return m_connected && !m_watcher.expired(); }

    void Disconnect() {
        if (!m_connected) return;
        if (!m_watcher.expired() && m_disconnect)
            m_disconnect();
        m_disconnect = nullptr;
        m_watcher.reset();
        m_connected = false;
    }

private:
    std::weak_ptr<void>  m_watcher;
    std::function<void()> m_disconnect;
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

    Signal() = default;

    ~Signal() {
        // Mark the shared state dead under the lock. Any concurrent Disconnect()
        // that races with this will either complete its removal first (holding the
        // lock) or see alive=false and skip removal. Either way, no UAF.
        dmq::LockGuard<RecursiveMutex> lock(m_state->mtx);
        m_state->alive = false;
        m_state->delegates.clear();
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
        auto copy  = std::shared_ptr<DelegateType>(delegate.Clone());
        if (!copy)
            BAD_ALLOC();
        auto state = m_state;
        {
            dmq::LockGuard<RecursiveMutex> lock(state->mtx);
            state->delegates.push_back(copy);
        }
        return ScopedConnection(detail::Connection(
            std::weak_ptr<void>(state),
            [state, copy]() {
                dmq::LockGuard<RecursiveMutex> lock(state->mtx);
                if (state->alive)
                    state->delegates.remove(copy);  // shared_ptr identity comparison
            }
        ));
    }

    /// @brief Invoke all connected delegates.
    void operator()(Args... args) {
        auto snapshot = GetSnapshot();
        InvokeSnapshot(snapshot, std::forward<Args>(args)...);
    }

    /// @brief Capture a snapshot of all current subscribers.
    /// @details The snapshot holds shared_ptrs to the delegates, ensuring they
    /// stay alive even if the Signal is destroyed.
    struct Snapshot {
        std::shared_ptr<DelegateType> small_buf[SIGNAL_SBO_COUNT];
        xlist<std::shared_ptr<DelegateType>> large_buf;
        size_t count = 0;
    };

    Snapshot GetSnapshot() const {
        Snapshot s;
        {
            dmq::LockGuard<RecursiveMutex> lock(m_state->mtx);
            s.count = m_state->delegates.size();
            if (s.count <= SIGNAL_SBO_COUNT) {
                size_t i = 0;
                for (auto& d : m_state->delegates) {
                    s.small_buf[i++] = d;
                }
            } else {
                s.large_buf = m_state->delegates;
            }
        }
        return s;
    }

    /// @brief Invoke a previously captured snapshot of delegates.
    static void InvokeSnapshot(const Snapshot& s, Args... args) {
        if (s.count <= SIGNAL_SBO_COUNT) {
            for (size_t i = 0; i < s.count; ++i) {
                if (s.small_buf[i])
                    (*s.small_buf[i])(args...);
            }
        } else {
            for (auto& d : s.large_buf) {
                if (d)
                    (*d)(args...);
            }
        }
    }

    /// @brief Number of currently connected subscribers.
    std::size_t Size() const {
        dmq::LockGuard<RecursiveMutex> lock(m_state->mtx);
        return m_state->delegates.size();
    }

    bool Empty() const { return Size() == 0; }

    /// @brief Disconnect all subscribers.
    void Clear() {
        dmq::LockGuard<RecursiveMutex> lock(m_state->mtx);
        m_state->delegates.clear();
    }

    XALLOCATOR

private:
    struct State {
        mutable RecursiveMutex mtx;
        bool alive = true;
        xlist<std::shared_ptr<DelegateType>> delegates;
        XALLOCATOR
    };
    std::shared_ptr<State> m_state = xmake_shared<State>();
};

} // namespace dmq

#endif // SIGNAL_H
