#ifndef _UNICAST_DELEGATE_SAFE_H
#define _UNICAST_DELEGATE_SAFE_H

/// @file
/// @brief Delegate container for storing an invoking a single delegate instance. 
/// Class is thread-safe.

#include "UnicastDelegate.h"

namespace dmq {

template <class R>
class UnicastDelegateSafe; // Not defined

/// @brief A thread-safe delegate container storing one delegate. Void and  
/// non-void return values supported. 
template<class RetType, class... Args>
class UnicastDelegateSafe<RetType(Args...)> : public UnicastDelegate<RetType(Args...)>
{
public:
    using DelegateType = Delegate<RetType(Args...)>;
    using BaseType = UnicastDelegate<RetType(Args...)>;

    UnicastDelegateSafe() = default;
    virtual ~UnicastDelegateSafe() = default;

    UnicastDelegateSafe(const UnicastDelegateSafe& rhs) : BaseType() {
        dmq::LockGuard<RecursiveMutex> lock(rhs.m_lock);
        BaseType::operator=(rhs);
    }

    UnicastDelegateSafe(UnicastDelegateSafe&& rhs) : BaseType() {
        dmq::LockGuard<RecursiveMutex> lock(rhs.m_lock);
        BaseType::operator=(std::move(rhs));
    }

    /// Constructor to initialize from a Delegate (Copy)
    UnicastDelegateSafe(const DelegateType& d) {
        dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator=(d);
    }

    /// Constructor to initialize from a Delegate (Move)
    UnicastDelegateSafe(DelegateType&& d) {
        dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator=(std::move(d));
    }

    /// Invoke the bound target.
    /// @param[in] args The arguments used when invoking the target function
    /// @return The target function return value. 
    RetType operator()(Args... args) {
        std::shared_ptr<DelegateType> delegate;
        {
            // Lock only to fetch the delegate pointer. Release lock before 
            // invoking the delegate to prevent circular lock dependencies 
            // and deadlocks.
            const dmq::LockGuard<RecursiveMutex> lock(m_lock);
            delegate = this->m_delegate;
        }

        if (delegate)
            return (*delegate)(args...);
        return RetType();
    }

    /// Invoke the bound target functions. 
    /// @param[in] args The arguments used when invoking the target function
    void Broadcast(Args... args) {
        operator()(args...);
    }

    /// Assign a delegate to the container.
    /// @param[in] rhs A delegate target to assign
    void operator=(const DelegateType& rhs) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator=(rhs);
    }

    /// Assign a delegate to the container.
    /// @param[in] rhs A delegate target to assign
    void operator=(DelegateType&& rhs) {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::operator=(rhs);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    UnicastDelegateSafe& operator=(const UnicastDelegateSafe& rhs) {
        if (this != &rhs) {
            std::scoped_lock lock(m_lock, rhs.m_lock);
            BaseType::operator=(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    UnicastDelegateSafe& operator=(UnicastDelegateSafe&& rhs) noexcept {
        if (this != &rhs) {
            std::scoped_lock lock(m_lock, rhs.m_lock);
            BaseType::operator=(std::move(rhs));
        }
        return *this;
    }

    /// @brief Clear the all target functions.
    virtual void operator=(std::nullptr_t) noexcept { 
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::Clear();
    }

    /// Any registered delegates?
    /// @return `true` if delegate container is empty.
    bool Empty() const { 
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        return BaseType::Empty();
    }

    /// Remove the registered delegate
    void Clear() {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        BaseType::Clear();
    }

    /// Get the number of delegates stored.
    /// @return The number of delegates stored.
    std::size_t Size() const { 
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        return BaseType::Size();
    }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the container is not empty, `false` if the container is empty.
    explicit operator bool() const {
        const dmq::LockGuard<RecursiveMutex> lock(m_lock);
        return BaseType::operator bool();
    }

private:
    /// Lock to make the class thread-safe. 
    /// Must be recursive to allow the callback to modify this delegate.
    /// Must be mutable to allow locking in const methods.
    mutable RecursiveMutex m_lock;
};

}

#endif
