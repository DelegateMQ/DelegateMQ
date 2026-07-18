#ifndef ASYNC_INVOKE_H
#define ASYNC_INVOKE_H

/// @file AsyncInvoke.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Helper functions for simplified, one-line asynchronous function invocation.
///
/// @details
/// The `AsyncInvoke` helper functions provide a convenient wrapper around the 
/// DelegateMQ library to execute any callable (free function, lambda, or member function) 
/// on a specific target thread. 
///
/// **Key Features:**
/// * **Automatic Context Switching:** Automatically detects if the caller is already 
///   execute on the target thread. 
///   - If **Yes**: It executes the function synchronously (immediately) to avoid overhead.
///   - If **No**: It creates a temporary asynchronous delegate, marshals the arguments, 
///     and executes the function on the target thread.
/// * **Blocking Wait:** Supports a timeout parameter to wait for the asynchronous 
///   execution to complete and return a value.
/// * **Return Value Handling:** Safely retrieves the return value from the target 
///   function across thread boundaries.

#include "delegate/DelegateRemote.h"
#include "delegate/DelegateAsync.h"
#include "delegate/IThread.h"
#include "delegate/DelegateOpt.h"
#include "extras/dispatcher/RemoteChannel.h"
#include <functional>
#include <type_traits>
#include <utility>
#include <any>
#include <memory> 

namespace dmq::util {

/// Helper function to simplify asynchronous invoke of a free function/lambda.
/// @param[in] func - the function/lambda to invoke
/// @param[in] thread - the thread to invoke func on
/// @param[in] timeout - the time to wait for invoke to complete
/// @param[in] args - the function argument(s) passed to func
template <class Func, class... Args>
auto AsyncInvoke(Func func, dmq::IThread& thread, const dmq::Duration& timeout, Args&&... args)
{
    // Deduce return type
    using RetType = decltype(func(std::forward<Args>(args)...));

    // Is the calling function executing on the requested thread?
    if (!thread.IsCurrentThread())
    {
        // Internal type used to bridge raw lambda to DelegateFunctionAsyncWait.
        // decay_t ensures that references and const qualifiers are removed for storage.
        std::function<RetType(std::decay_t<Args>...)> typedFunc = func;

        // Create a delegate that points to func to invoke on thread
        auto delegate = dmq::MakeDelegate(typedFunc, thread, timeout);

        // Invoke the delegate target function asynchronously and wait for completion
        auto retVal = delegate.AsyncInvoke(std::forward<Args>(args)...);

        // Target function has a return value?
        if constexpr (!std::is_void_v<RetType>)
        {
            if (retVal.has_value()) {
                return retVal.value();
            }
            else {
                return RetType{};
            }
        }
    }
    else
    {
        // Invoke target function synchronously
        return func(std::forward<Args>(args)...);
    }
}

/// Helper function to simplify asynchronous invoke of a member function using a raw pointer.
/// @param[in] tclass - the class instance (pointer)
/// @param[in] func - the member function pointer
/// @param[in] thread - the thread to invoke func on
/// @param[in] timeout - the time to wait for invoke to complete
/// @param[in] args - the function argument(s) passed to func
template <class TClass, class Func, class... Args>
auto AsyncInvoke(TClass* tclass, Func func, dmq::IThread& thread, const dmq::Duration& timeout, Args&&... args)
{
    // Deduce return type using std::invoke (robust for member pointers)
    using RetType = decltype(std::invoke(func, tclass, std::forward<Args>(args)...));

    if (!thread.IsCurrentThread())
    {
        // Create delegate. MakeDelegate handles member pointers correctly.
        auto delegate = dmq::MakeDelegate(tclass, func, thread, timeout);

        auto retVal = delegate.AsyncInvoke(std::forward<Args>(args)...);

        if constexpr (!std::is_void_v<RetType>)
        {
            if (retVal.has_value()) {
                return retVal.value();
            }
            else {
                return RetType{};
            }
        }
    }
    else
    {
        return std::invoke(func, tclass, std::forward<Args>(args)...);
    }
}

/// Helper function to simplify asynchronous invoke of a member function using a shared pointer.
/// @param[in] tclass - the class instance (shared_ptr)
/// @param[in] func - the member function pointer
/// @param[in] thread - the thread to invoke func on
/// @param[in] timeout - the time to wait for invoke to complete
/// @param[in] args - the function argument(s) passed to func
template <class TClass, class Func, class... Args>
auto AsyncInvoke(std::shared_ptr<TClass> tclass, Func func, dmq::IThread& thread, const dmq::Duration& timeout, Args&&... args)
{
    // Deduce return type using std::invoke (robust for member pointers)
    using RetType = decltype(std::invoke(func, tclass, std::forward<Args>(args)...));

    if (!thread.IsCurrentThread())
    {
        // Create delegate. MakeDelegate handles shared_ptr correctly (creates DelegateMemberSp).
        auto delegate = dmq::MakeDelegate(tclass, func, thread, timeout);

        auto retVal = delegate.AsyncInvoke(std::forward<Args>(args)...);

        if constexpr (!std::is_void_v<RetType>)
        {
            if (retVal.has_value()) {
                return retVal.value();
            }
            else {
                return RetType{};
            }
        }
    }
    else
    {
        return std::invoke(func, tclass, std::forward<Args>(args)...);
    }
}

} // namespace dmq::util

#endif
