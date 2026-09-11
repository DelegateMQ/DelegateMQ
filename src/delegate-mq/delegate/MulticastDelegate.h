#ifndef _MULTICAST_DELEGATE_H
#define _MULTICAST_DELEGATE_H

/// @file
/// @brief Delegate container for storing and iterating over a collection of
/// delegate instances. Supports reentrant removal during invocation.
/// Class is not thread-safe.

#include "Delegate.h"
#include <algorithm>
#include <memory>

DMQ_OPTIMIZE_ON

namespace dmq {

template <class R>
class MulticastDelegate; // Not defined

namespace detail {

/// @brief Clone `delegate` and append it to `delegates`, fully erased to the
/// non-templated `DelegateBase` at construction. `delegate.Clone()` is called
/// through a `const DelegateBase&`, so its return type here is `DelegateBase*`
/// (not the more-derived leaf type) -- the resulting shared_ptr control block
/// is therefore not templated on RetType/Args, and is shared by every
/// `MulticastDelegate<Sig>` in the program instead of duplicated per signature.
/// Templated only on nothing at all (a plain function): none of this logic
/// depends on the owning container's signature.
/// @param[in,out] delegates The list to append to.
/// @param[in] delegate The delegate to clone and store.
inline void MulticastPushBack(xlist<std::shared_ptr<DelegateBase>>& delegates, const DelegateBase& delegate) {
    auto delegateClone = delegate.Clone();
    if (!delegateClone)
        BAD_ALLOC();

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    // No exceptions: Direct execution.
    // If shared_ptr or vector allocation fails here on embedded,
    // standard behavior is usually an abort() or system reset.
    std::shared_ptr<DelegateBase> sharedDelegate(delegateClone, std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
    delegates.push_back(std::forward<std::shared_ptr<DelegateBase>>(sharedDelegate));
#else
    // Exceptions enabled: Safe to try-catch.
    try {
        std::shared_ptr<DelegateBase> sharedDelegate(delegateClone, std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
        delegates.push_back(std::forward<std::shared_ptr<DelegateBase>>(sharedDelegate));
    }
    catch (const std::bad_alloc&) {
        BAD_ALLOC();
    }
#endif
}

/// @brief Remove `delegate` from `delegates`, respecting reentrant removal
/// during an active broadcast.
/// @param[in,out] delegates The list to remove from.
/// @param[in] broadcastCount The owning container's current broadcast nesting depth.
/// @param[out] cleanup Set to `true` if a lazy null-removal is needed later.
/// @param[in] delegate The delegate to remove.
inline void MulticastRemove(xlist<std::shared_ptr<DelegateBase>>& delegates, int broadcastCount, bool& cleanup, const DelegateBase& delegate) {
    auto it = std::find_if(delegates.begin(), delegates.end(),
        [&delegate](const std::shared_ptr<DelegateBase>& item) {
            // Must check if item is valid before comparing!
            return item && (*item == delegate);
        });

    if (it != delegates.end()) {
        if (broadcastCount > 0) {
            // REENTRANCY DETECTED:
            // Do not erase(). Just null out the pointer.
            // The iterator in operator() stays valid, but next access sees null.
            it->reset();
            cleanup = true;
        }
        else {
            // Safe to erase immediately
            delegates.erase(it);
        }
    }
}

/// @brief Remove all delegates from `delegates`, respecting reentrant removal.
inline void MulticastClear(xlist<std::shared_ptr<DelegateBase>>& delegates, int broadcastCount, bool& cleanup) {
    if (broadcastCount > 0) {
        for (auto& delegate : delegates) {
            delegate.reset();
        }
        cleanup = true;
    }
    else {
        delegates.clear();
    }
}

/// @brief Deep-copy every delegate in `src` (cloned, fully erased to
/// `DelegateBase`) into `dest`.
inline void MulticastCopyFrom(xlist<std::shared_ptr<DelegateBase>>& dest, const xlist<std::shared_ptr<DelegateBase>>& src) {
    for (auto& delegate : src) {
        if (!delegate) continue;
        auto delegateClone = delegate->Clone();
        if (!delegateClone)
            BAD_ALLOC();

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        // No exceptions: Direct execution.
        std::shared_ptr<DelegateBase> sharedDelegate(delegateClone, std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
        dest.push_back(sharedDelegate);
#else
        // Exceptions enabled: Safe to try-catch.
        try {
            std::shared_ptr<DelegateBase> sharedDelegate(delegateClone, std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
            dest.push_back(sharedDelegate);
        }
        catch (const std::bad_alloc&) {
            BAD_ALLOC();
        }
#endif
    }
}

/// @brief Move every element of `src` onto the end of `dest`, then clear `src`.
/// Used by the reentrant branch of move-assignment, which must keep `dest`'s
/// own list object identity stable (an active broadcast's iterator into it may
/// be live) rather than replacing it outright.
inline void MulticastAppendAndClear(xlist<std::shared_ptr<DelegateBase>>& dest, xlist<std::shared_ptr<DelegateBase>>& src) {
    for (auto& delegate : src) {
        dest.push_back(std::move(delegate));
    }
    src.clear();
}

/// @brief RAII guard marking an active broadcast. Increments `cnt` on
/// construction, decrements on destruction, and purges lazily-nulled entries
/// from `delegates` once the outermost nested broadcast finishes. Takes plain
/// references to the exact fields it needs rather than a container pointer,
/// so it carries no dependency on RetType/Args at all.
class BroadcastGuard {
public:
    BroadcastGuard(int& cnt, xlist<std::shared_ptr<DelegateBase>>& delegates, bool& cleanup)
        : m_cnt(cnt), m_delegates(delegates), m_cleanup(cleanup) {
        m_cnt++; // Lock
    }
    ~BroadcastGuard() {
        m_cnt--; // Unlock
        if (m_cnt == 0 && m_cleanup) {
            // Efficiently remove all null pointers from the list
            m_delegates.remove_if([](const std::shared_ptr<DelegateBase>& item) {
                return item == nullptr;
                });
            m_cleanup = false;
        }
    }
private:
    int& m_cnt;
    xlist<std::shared_ptr<DelegateBase>>& m_delegates;
    bool& m_cleanup;
};

} // namespace detail

/// @brief Not thread-safe multicast delegate container class. The class has a list of
/// `Delegate<>` instances. When invoked, each `Delegate` instance within the invocation
/// list is called. The broadcast iterates over the live list; if a delegate is removed
/// during a broadcast, its current execution finishes safely but it is removed from the list.
/// If a new delegate is added during a broadcast, it may be invoked in the current pass.
/// @note The subscriber list is stored as `shared_ptr<DelegateBase>` rather than
/// `shared_ptr<Delegate<RetType(Args...)>>` so that `xlist`'s instantiation (and all its
/// member functions) is shared across every `MulticastDelegate<Sig>` in the program instead
/// of being duplicated once per signature. The bookkeeping operations on that list
/// (`PushBack`/`Remove`/`Clear`/copy/the broadcast-nesting guard) are likewise implemented as
/// plain, non-templated `detail::Multicast*` functions/`detail::BroadcastGuard` above, for the
/// same reason -- their bodies never touch `RetType`/`Args` either. Only `operator()`, which
/// must actually invoke the bound function, needs the concrete `DelegateType` back.
template<class RetType, class... Args>
class MulticastDelegate<RetType(Args...)>
{
public:
    using DelegateType = Delegate<RetType(Args...)>;

    MulticastDelegate() = default;
    virtual ~MulticastDelegate() { Clear(); }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    MulticastDelegate(const MulticastDelegate& rhs) { detail::MulticastCopyFrom(m_delegates, rhs.m_delegates); }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    MulticastDelegate(MulticastDelegate&& rhs) noexcept : m_delegates(std::move(rhs.m_delegates)) { }

    /// Constructor to initialize from a single Delegate (Copy)
    MulticastDelegate(const DelegateType& d) {
        PushBack(d);
    }

    /// Constructor to initialize from a single Delegate (Move)
    MulticastDelegate(DelegateType&& d) {
        PushBack(d);
    }

    /// Invoke all bound target functions. Safe to remove delegates during invocation.
    /// A void return value is used since multiple targets invoked.
    /// @param[in] args The arguments used when invoking the target functions
    void operator()(Args... args) {
        // RAII Guard: Increments now, Decrements + Cleans up on return/throw
        detail::BroadcastGuard guard(m_broadcastCount, m_delegates, m_cleanup);

        // Iterate safely
        for (auto it = m_delegates.begin(); it != m_delegates.end(); ++it) {
            std::shared_ptr<DelegateBase> delegate = *it; // Copy to prevent UAF if removed mid-invocation
            if (delegate) {
                (*static_cast<DelegateType*>(delegate.get()))(args...);
            }
        }
    }

    /// Invoke all bound target functions. A void return value is used
    /// since multiple targets invoked.
    /// @param[in] args The arguments used when invoking the target functions
    void Broadcast(Args... args) {
        (*this)(args...);
    }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void operator+=(const DelegateType& delegate) { PushBack(delegate); }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void operator+=(DelegateType&& delegate) { PushBack(delegate); }

    /// Remove a delegate from the container.
    /// @param[in] delegate A delegate target to remove
    void operator-=(const DelegateType& delegate) { Remove(delegate); }

    /// Remove a delegate from the container.
    /// @param[in] delegate A delegate target to remove
    void operator-=(DelegateType&& delegate) { Remove(delegate); }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    MulticastDelegate& operator=(const MulticastDelegate& rhs) {
        if (&rhs != this) {
            Clear();
            detail::MulticastCopyFrom(m_delegates, rhs.m_delegates);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    MulticastDelegate& operator=(MulticastDelegate&& rhs) noexcept {
        if (&rhs != this) {
            if (m_broadcastCount > 0) {
                // Reentrant: a callback is reassigning the container it's currently
                // broadcasting from. Clear() only nulls existing entries in this case,
                // leaving m_delegates itself intact so the active iterator in operator()
                // stays valid. Append rhs's delegates rather than replacing the
                // container outright; stale nulls are purged after the broadcast ends.
                Clear();
                detail::MulticastAppendAndClear(m_delegates, rhs.m_delegates);
            }
            else {
                Clear();
                m_delegates = std::move(rhs.m_delegates);
            }
        }
        return *this;
    }

    /// @brief Clear the all target functions.
    virtual void operator=(std::nullptr_t) noexcept { Clear(); }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void PushBack(const DelegateType& delegate) {
        detail::MulticastPushBack(m_delegates, delegate);
    }

    /// Remove a delegate into the container.
    /// @param[in] delegate The delegate target to remove.
    void Remove(const DelegateType& delegate) {
        detail::MulticastRemove(m_delegates, m_broadcastCount, m_cleanup, delegate);
    }

    /// Any registered delegates?
    /// @return `true` if delegate container is empty.
    bool Empty() const { return m_delegates.empty(); }

    /// Removal all registered delegates.
    void Clear() {
        detail::MulticastClear(m_delegates, m_broadcastCount, m_cleanup);
    }

    /// Get the number of delegates stored.
    /// @return The number of delegates stored.
    std::size_t Size() const { return m_delegates.size(); }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the container is not empty, `false` if the container is empty.
    explicit operator bool() const { return !Empty(); }

protected:
    /// List of registered delegates. Stored as `DelegateBase` (not `DelegateType`) so this
    /// `xlist` instantiation, and every member function on it, is shared across all
    /// `MulticastDelegate<Sig>` signatures instead of being duplicated per signature.
    xlist<std::shared_ptr<DelegateBase>> m_delegates;

    /// Count of active nested broadcasts
    int m_broadcastCount = 0;

    /// Flag for handling lazy delete
    bool m_cleanup = false;
};

}

DMQ_OPTIMIZE_OFF

#endif
