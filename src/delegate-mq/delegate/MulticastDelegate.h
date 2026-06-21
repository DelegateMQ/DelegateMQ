#ifndef _MULTICAST_DELEGATE_H
#define _MULTICAST_DELEGATE_H

/// @file
/// @brief Delegate container for storing and iterating over a collection of 
/// delegate instances. Supports reentrant removal during invocation. 
/// Class is not thread-safe.

#include "Delegate.h"
#include <list>
#include <algorithm>
#include <memory>

namespace dmq {

template <class R>
class MulticastDelegate; // Not defined

/// @brief Not thread-safe multicast delegate container class. The class has a list of 
/// `Delegate<>` instances. When invoked, each `Delegate` instance within the invocation 
/// list is called. A snapshot of the delegates is taken during the broadcast. If a 
/// delegate is removed during a broadcast, it will still be invoked in the current 
/// broadcast pass if it was in the snapshot.
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
    MulticastDelegate(const MulticastDelegate& rhs) { CopyFrom(rhs); }

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
        size_t count = m_delegates.size();
        if (count <= SIGNAL_SBO_COUNT) {
            std::shared_ptr<DelegateType> small_buf[SIGNAL_SBO_COUNT];
            size_t i = 0;
            for (auto& d : m_delegates) {
                small_buf[i++] = d;
            }
            for (size_t i = 0; i < count; ++i) {
                if (small_buf[i])
                    (*small_buf[i])(args...);
                small_buf[i].reset(); // Clear to release shared_ptr immediately
            }
        } else {
            xlist<std::shared_ptr<DelegateType>> large_buf = m_delegates;
            for (auto& d : large_buf) {
                if (d)
                    (*d)(args...);
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
            CopyFrom(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    MulticastDelegate& operator=(MulticastDelegate&& rhs) noexcept {
        if (&rhs != this) {
            m_delegates = std::move(rhs.m_delegates);
        }
        return *this;
    }

    /// @brief Clear the all target functions.
    virtual void operator=(std::nullptr_t) noexcept { Clear(); }

    /// Insert a delegate into the container.
    /// @param[in] delegate A delegate target to insert
    void PushBack(const DelegateType& delegate) {
        auto delegateClone = delegate.Clone();
        if (!delegateClone)
            BAD_ALLOC();

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        // No exceptions: Direct execution. 
        // If shared_ptr or vector allocation fails here on embedded, 
        // standard behavior is usually an abort() or system reset.
        std::shared_ptr<DelegateType> sharedDelegate(delegateClone, std::default_delete<DelegateType>(), ::dmq::stl_allocator<std::remove_const_t<DelegateType>>());
        m_delegates.push_back(std::forward<std::shared_ptr<DelegateType>>(sharedDelegate));
#else
        // Exceptions enabled: Safe to try-catch.
        try {
            std::shared_ptr<DelegateType> sharedDelegate(delegateClone, std::default_delete<DelegateType>(), ::dmq::stl_allocator<std::remove_const_t<DelegateType>>());
            m_delegates.push_back(std::forward<std::shared_ptr<DelegateType>>(sharedDelegate));
        }
        catch (const std::bad_alloc&) {
            BAD_ALLOC();
        }
#endif
    }

    /// Remove a delegate into the container.
    /// @param[in] delegate The delegate target to remove.
    void Remove(const DelegateType& delegate) {
        auto it = m_delegates.begin();
        while (it != m_delegates.end()) {
            if (*it && (**it == delegate)) {
                it = m_delegates.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Any registered delegates?
    /// @return `true` if delegate container is empty.
    bool Empty() const { return m_delegates.empty(); }

    /// Removal all registered delegates.
    void Clear() { m_delegates.clear(); }

    /// Get the number of delegates stored.
    /// @return The number of delegates stored.
    std::size_t Size() const { return m_delegates.size(); }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the container is not empty, `false` if the container is empty.
    explicit operator bool() const { return !Empty(); }

private:
    /// Copy all delegate container objects.
    /// @param[in] other The container to copy from
    void CopyFrom(const MulticastDelegate& other) {
        for (auto& delegate : other.m_delegates) {
            if (!delegate) continue;  // Skip soft-deleted entries (mid-broadcast nulls)
            auto delegateClone = delegate->Clone();
            if (!delegateClone)
                BAD_ALLOC();

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
            // No exceptions: Direct execution.
            std::shared_ptr<DelegateType> sharedDelegate(delegateClone, std::default_delete<DelegateType>(), ::dmq::stl_allocator<std::remove_const_t<DelegateType>>());
            m_delegates.push_back(sharedDelegate);
#else
            // Exceptions enabled: Safe to try-catch.
            try {
                std::shared_ptr<DelegateType> sharedDelegate(delegateClone, std::default_delete<DelegateType>(), ::dmq::stl_allocator<std::remove_const_t<DelegateType>>());
                m_delegates.push_back(sharedDelegate);
            }
            catch (const std::bad_alloc&) {
                BAD_ALLOC();
            }
#endif
        }
    }

protected:
    /// List of registered delegates
    xlist<std::shared_ptr<DelegateType>> m_delegates;
};

}

#endif
