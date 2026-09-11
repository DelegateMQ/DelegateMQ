#ifndef _UNICAST_DELEGATE_H
#define _UNICAST_DELEGATE_H

/// @file
/// @brief Delegate container for storing an invoking a single delegate instance.
/// Class is not thread-safe.

#include "Delegate.h"
#include <memory>

DMQ_OPTIMIZE_ON

namespace dmq {

template <class R>
class UnicastDelegate; // Not defined

namespace detail {

/// @brief Clone `delegate` and wrap it in a shared_ptr, fully erased to the
/// non-templated `DelegateBase` at construction. `delegate.Clone()` is called
/// through a `const DelegateBase&`, so its return type here is `DelegateBase*`
/// -- the resulting shared_ptr control block is therefore not templated on
/// RetType/Args, and is shared by every `UnicastDelegate<Sig>` in the program
/// instead of duplicated per signature. Non-templated: none of this logic
/// depends on the owning container's signature.
inline std::shared_ptr<DelegateBase> UnicastClone(const DelegateBase& delegate) {
    auto delegateClone = delegate.Clone();
    if (!delegateClone)
        BAD_ALLOC();
    return std::shared_ptr<DelegateBase>(delegateClone, std::default_delete<DelegateBase>(), ::dmq::stl_allocator<DelegateBase>());
}

} // namespace detail

/// @brief A non-thread-safe delegate container storing one delegate. Void and
/// non-void return values supported.
template<class RetType, class... Args>
class UnicastDelegate<RetType(Args...)>
{
public:
    using DelegateType = Delegate<RetType(Args...)>;

    UnicastDelegate() = default;
    virtual ~UnicastDelegate() { Clear(); }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    UnicastDelegate(const UnicastDelegate& rhs) {
        if (rhs.m_delegate)
            m_delegate = detail::UnicastClone(*rhs.m_delegate);
    }

    /// Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    UnicastDelegate(UnicastDelegate&& rhs) noexcept : m_delegate(std::move(rhs.m_delegate)) { }

    /// Constructor to initialize from a Delegate (Copy)
    UnicastDelegate(const DelegateType& d) {
        *this = d;
    }

    /// Constructor to initialize from a Delegate (Move)
    UnicastDelegate(DelegateType&& d) {
        *this = std::move(d);
    }


    /// Invoke the bound target.
    /// @param[in] args The arguments used when invoking the target function
    /// @return The target function return value.
    RetType operator()(Args... args) const {
        if (m_delegate)
            return (*static_cast<DelegateType*>(m_delegate.get()))(args...);	// Invoke delegate callback
        else
            return RetType();
    }

    /// Invoke the bound target functions.
    /// @param[in] args The arguments used when invoking the target function
    void Broadcast(Args... args) const {
        (*this)(args...);
    }

    /// Assign a delegate to the container.
    /// @param[in] rhs A delegate target to assign
    void operator=(const DelegateType& rhs) {
        m_delegate = detail::UnicastClone(rhs);
    }

    /// Assign a delegate to the container.
    /// @param[in] rhs A delegate target to assign
    void operator=(DelegateType&& rhs) {
        // Clone() even on rvalue: no move API exists across the polymorphic delegate hierarchy.
        m_delegate = detail::UnicastClone(rhs);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    UnicastDelegate& operator=(const UnicastDelegate& rhs) {
        if (this != &rhs) {
            m_delegate = rhs.m_delegate ? detail::UnicastClone(*rhs.m_delegate) : nullptr;
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    UnicastDelegate& operator=(UnicastDelegate&& rhs) noexcept {
        if (this != &rhs) {
            m_delegate = std::move(rhs.m_delegate);
        }
        return *this;
    }

    /// @brief Clear the all target functions.
    virtual void operator=(std::nullptr_t) noexcept { Clear(); }

    /// Any registered delegates?
    /// @return `true` if delegate container is empty.
    bool Empty() const { return !m_delegate; }

    /// Remove the registered delegate
    void Clear() { m_delegate = nullptr; }

    /// Get the number of delegates stored.
    /// @return The number of delegates stored.
    std::size_t Size() const { return m_delegate == nullptr ? 0 : 1; }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the container is not empty, `false` if the container is empty.
    explicit operator bool() const { return !Empty(); }

    /// @brief Get the underlying delegate.
    /// @return The underlying delegate pointer, or nullptr if empty.
    const DelegateType* GetDelegate() const { return static_cast<const DelegateType*>(m_delegate.get()); }

protected:
    /// Registered delegate. Stored as `DelegateBase` (not `DelegateType`) so the
    /// shared_ptr control block is shared across every `UnicastDelegate<Sig>`
    /// signature instead of duplicated per signature. Only `operator()`/
    /// `GetDelegate()`, which must return the concrete `DelegateType`, cast back.
    std::shared_ptr<DelegateBase> m_delegate = nullptr;
};

}

DMQ_OPTIMIZE_OFF

#endif
