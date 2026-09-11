#ifndef _MULTICAST_DELEGATE_SAFE_H
#define _MULTICAST_DELEGATE_SAFE_H

/// @file
/// @brief Delegate container for storing and iterating over a collection of
/// delegate instances. Class is thread-safe.

#include "MulticastDelegate.h"

DMQ_OPTIMIZE_ON

namespace dmq {

template <class R>
class MulticastDelegateSafe; // Not defined

namespace detail {

/// @brief Lock-protected wrapper around `MulticastPushBack` (MulticastDelegate.h).
/// Non-templated: none of this depends on the owning container's signature.
inline void MulticastSafePushBack(xlist<std::shared_ptr<DelegateBase>>& delegates, RecursiveMutex& lock, const DelegateBase& delegate) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    MulticastPushBack(delegates, delegate);
}

/// @brief Lock-protected wrapper around `MulticastRemove`.
inline void MulticastSafeRemove(xlist<std::shared_ptr<DelegateBase>>& delegates, int broadcastCount, bool& cleanup, RecursiveMutex& lock, const DelegateBase& delegate) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    MulticastRemove(delegates, broadcastCount, cleanup, delegate);
}

/// @brief Lock-protected wrapper around `MulticastClear`.
inline void MulticastSafeClear(xlist<std::shared_ptr<DelegateBase>>& delegates, int broadcastCount, bool& cleanup, RecursiveMutex& lock) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    MulticastClear(delegates, broadcastCount, cleanup);
}

/// @brief Lock-protected emptiness check.
inline bool MulticastSafeEmpty(const xlist<std::shared_ptr<DelegateBase>>& delegates, RecursiveMutex& lock) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    return delegates.empty();
}

/// @brief Lock-protected size check.
inline size_t MulticastSafeSize(const xlist<std::shared_ptr<DelegateBase>>& delegates, RecursiveMutex& lock) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    return delegates.size();
}

/// @brief Snapshot the current delegate list under lock, into `smallBuf` (up to
/// `smallCap` entries) or `largeBuf` if there are more than `smallCap`. Returns
/// the number of delegates snapshotted. The lock is released before returning,
/// so the caller can invoke each delegate without holding it (see the comment
/// on `MulticastDelegateSafe::operator()` for why). Non-templated: entirely
/// independent of the owning container's signature.
inline size_t MulticastSafeSnapshot(xlist<std::shared_ptr<DelegateBase>>& delegates, RecursiveMutex& lock,
    std::shared_ptr<DelegateBase>* smallBuf, size_t smallCap, xlist<std::shared_ptr<DelegateBase>>& largeBuf) {
    const dmq::LockGuard<RecursiveMutex> g(lock);
    size_t count = delegates.size();
    if (count <= smallCap) {
        size_t i = 0;
        for (auto& d : delegates) {
            smallBuf[i++] = d;
        }
    } else {
        largeBuf = delegates;
    }
    return count;
}

} // namespace detail

/// @brief Thread-safe multicast delegate container class.
template<class RetType, class... Args>
class MulticastDelegateSafe<RetType(Args...)> : public MulticastDelegate<RetType(Args...)>
{
public:
    using DelegateType = Delegate<RetType(Args...)>;
    using BaseType = MulticastDelegate<RetType(Args...)>;

    MulticastDelegateSafe() = default;
    virtual ~MulticastDelegateSafe() = default;

    MulticastDelegateSafe(const MulticastDelegateSafe& rhs) : BaseType() {
        dmq::ScopedLock<RecursiveMutex, RecursiveMutex> lock(m_lock, rhs.m_lock);
        BaseType::operator=(rhs);
    }

    MulticastDelegateSafe(MulticastDelegateSafe&& rhs) : BaseType() {
        dmq::ScopedLock<RecursiveMutex, RecursiveMutex> lock(m_lock, rhs.m_lock);
        BaseType::operator=(std::move(rhs));
    }

    /// Constructor to initialize from a single Delegate (Copy)
    MulticastDelegateSafe(const DelegateType& d) {
        this->PushBack(d);
    }

    /// Constructor to initialize from a single Delegate (Move)
    MulticastDelegateSafe(DelegateType&& d) {
        this->PushBack(std::move(d));
    }

    /// Invoke the bound target function for all stored delegate instances.
    /// A void return value is used since multiple targets invoked.
    /// @param[in] args The arguments used when invoking the target functions
    void operator()(Args... args) {
        // To prevent deadlocks, the mutex must be released before invoking the
        // delegate. Circular lock dependencies can occur if the delegate target
        // function itself attempts to acquire a lock that is held by the
        // thread invoking the delegate.
        // Use a small-buffer optimization to avoid heap allocation in the common case.
        // Buffers hold `DelegateBase` (not `DelegateType`) so this `xlist` instantiation
        // is shared across every `MulticastDelegateSafe<Sig>` signature; only the final
        // invoke below needs the concrete `DelegateType` back. The snapshot-under-lock
        // logic itself lives in `detail::MulticastSafeSnapshot` for the same reason.
        std::shared_ptr<DelegateBase> small_buf[SIGNAL_SBO_COUNT];
        xlist<std::shared_ptr<DelegateBase>> large_buf;
        size_t count = detail::MulticastSafeSnapshot(this->m_delegates, m_lock, small_buf, SIGNAL_SBO_COUNT, large_buf);

        if (count <= SIGNAL_SBO_COUNT) {
            for (size_t i = 0; i < count; ++i) {
                if (small_buf[i])
                    (*static_cast<DelegateType*>(small_buf[i].get()))(args...);
                small_buf[i].reset(); // Clear to release shared_ptr immediately
            }
        } else {
            for (auto& d : large_buf) {
                if (d)
                    (*static_cast<DelegateType*>(d.get()))(args...);
            }
        }
    }

    /// Invoke all bound target functions. A void return value is used
    /// since multiple targets invoked.
    /// @param[in] args The arguments used when invoking the target functions
    void Broadcast(Args... args) {
        operator()(args...);
    }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void operator+=(const Delegate<RetType(Args...)>& delegate) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator +=(delegate);
    }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void operator+=(Delegate<RetType(Args...)>&& delegate) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator +=(delegate);
    }

    /// Remove a delegate from the container.
    /// @param[in] delegate A delegate target to remove
    void operator-=(const Delegate<RetType(Args...)>& delegate) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator -=(delegate);
    }

    /// Remove a delegate from the container.
    /// @param[in] delegate A delegate target to remove
    void operator-=(Delegate<RetType(Args...)>&& delegate) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator -=(delegate);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    MulticastDelegateSafe& operator=(const MulticastDelegateSafe& rhs) {
        if (this != &rhs) {
            // Lock both instances safely to prevent modification of source during copy
            dmq::ScopedLock<RecursiveMutex, RecursiveMutex> lock(m_lock, rhs.m_lock);
            BaseType::operator=(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    MulticastDelegateSafe& operator=(MulticastDelegateSafe&& rhs) noexcept {
        if (this != &rhs) {
            dmq::ScopedLock<RecursiveMutex, RecursiveMutex> lock(m_lock, rhs.m_lock);
            BaseType::operator=(std::move(rhs));
        }
        return *this;
    }

    /// @brief Clear the all target functions.
    virtual void operator=(std::nullptr_t) noexcept {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::Clear();
    }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void PushBack(const DelegateType& delegate) {
        detail::MulticastSafePushBack(this->m_delegates, m_lock, delegate);
    }

    /// Remove a delegate into the container.
    /// @param[in] delegate The delegate target to remove.
    void Remove(const DelegateType& delegate) {
        detail::MulticastSafeRemove(this->m_delegates, this->m_broadcastCount, this->m_cleanup, m_lock, delegate);
    }

    /// Any registered delegates?
    /// @return `true` if delegate container is empty.
    bool Empty() const {
        return detail::MulticastSafeEmpty(this->m_delegates, m_lock);
    }

    /// Removal all registered delegates.
    void Clear() {
        detail::MulticastSafeClear(this->m_delegates, this->m_broadcastCount, this->m_cleanup, m_lock);
    }

    /// Get the number of delegates stored.
    /// @return The number of delegates stored.
    std::size_t Size() const {
        return detail::MulticastSafeSize(this->m_delegates, m_lock);
    }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the container is not empty, `false` if the container is empty.
    explicit operator bool() const {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        return BaseType::operator bool();
    }

private:
    /// Lock to make the class thread-safe
    mutable RecursiveMutex m_lock;
};

}

DMQ_OPTIMIZE_OFF

#endif
