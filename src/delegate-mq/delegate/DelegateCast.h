#ifndef _DELEGATE_CAST_H
#define _DELEGATE_CAST_H

/// @file DelegateCast.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief The casts the delegates need, with or without RTTI.
///
/// The library needs two casts: Equal() checks that the other delegate is of
/// its own class (or derived from it), and an async Invoke() turns the
/// DelegateMsg it receives back into its typed message. With RTTI these are
/// dynamic_cast and std::dynamic_pointer_cast.
///
/// DMQ_NO_RTTI replaces them, so the library builds with -fno-rtti (GCC,
/// Clang) or /GR- (MSVC). DelegateSelect.h defines it automatically when the
/// compiler has RTTI off; DelegateOpt.h includes this header. A type is then
/// identified by a TypeId: the address of a static variable that exists once
/// per type. Each delegate class overrides CastTo(),
/// which matches its own TypeId or asks its base class, the same answers
/// dynamic_cast gives along the delegate class hierarchy (single inheritance
/// from DelegateBase). Each message class overrides GetMsgType().

#include "DelegateSelect.h"     // DMQ_NO_RTTI, set there when the compiler has RTTI off
#include <memory>
#if !defined(DMQ_NO_RTTI)
    #include <typeindex>
#endif

#include <type_traits>

namespace dmq {

/// @brief A type's identity without RTTI: unique per type, comparable.
using TypeId = const void*;

namespace detail {
    template <class T>
    // One per type and program (an inline variable). Not const: a linker that
    // folds identical read-only data (MSVC /OPT:ICF) could otherwise give two
    // types the same address; writable data is never folded.
    struct TypeIdTag { static inline char id = 0; };
}

/// @brief The TypeId of T. Like typeid(T), ignores references and top-level
/// const/volatile: TypeIdOf<const T&>() == TypeIdOf<T>().
template <class T>
constexpr TypeId TypeIdOf() noexcept { return &detail::TypeIdTag<std::remove_cv_t<std::remove_reference_t<T>>>::id; }

/// @brief A type's identity as a map key or for comparing: std::type_index
/// with RTTI (stable across shared libraries), TypeId without.
#if defined(DMQ_NO_RTTI)
using TypeKey = TypeId;
template <class T>
TypeKey TypeKeyOf() noexcept { return TypeIdOf<T>(); }
#else
using TypeKey = std::type_index;
template <class T>
TypeKey TypeKeyOf() noexcept { return std::type_index(typeid(T)); }
#endif

/// @brief dynamic_cast<const T*>(&rhs) for a delegate: rhs as a T, or nullptr
/// if it isn't one. T derives from DelegateBase.
template <class T, class Base>
const T* DelegateCast(const Base& rhs) noexcept
{
#if defined(DMQ_NO_RTTI)
    return static_cast<const T*>(rhs.CastTo(TypeIdOf<T>()));
#else
    return dynamic_cast<const T*>(&rhs);
#endif
}

/// @brief std::dynamic_pointer_cast<T>(msg) for a delegate message: the
/// message as a T, or nullptr if it isn't one.
template <class T, class Msg>
std::shared_ptr<T> MsgCast(const std::shared_ptr<Msg>& msg) noexcept
{
#if defined(DMQ_NO_RTTI)
    if (msg && msg->GetMsgType() == TypeIdOf<T>())
        return std::static_pointer_cast<T>(msg);
    return nullptr;
#else
    return std::dynamic_pointer_cast<T>(msg);
#endif
}

} // namespace dmq

/// @brief In a delegate class: its CastTo() for DelegateCast() under
/// DMQ_NO_RTTI (nothing with RTTI). ThisType is the class, ParentType its
/// direct delegate base class.
#if defined(DMQ_NO_RTTI)
    #define DMQ_DELEGATE_CAST_TO(ThisType, ParentType) \
        const void* CastTo(dmq::TypeId id) const noexcept override { \
            return id == dmq::TypeIdOf<ThisType>() ? static_cast<const void*>(static_cast<const ThisType*>(this)) \
                                                   : ParentType::CastTo(id); \
        }
#else
    #define DMQ_DELEGATE_CAST_TO(ThisType, ParentType)
#endif

/// @brief In a delegate message class: its GetMsgType() for MsgCast() under
/// DMQ_NO_RTTI (nothing with RTTI).
#if defined(DMQ_NO_RTTI)
    #define DMQ_MSG_TYPE(ThisType) \
        dmq::TypeId GetMsgType() const noexcept override { return dmq::TypeIdOf<ThisType>(); }
#else
    #define DMQ_MSG_TYPE(ThisType)
#endif

#endif // _DELEGATE_CAST_H
