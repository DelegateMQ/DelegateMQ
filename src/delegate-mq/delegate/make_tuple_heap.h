#ifndef _MAKE_TUPLE_HEAP_H
#define _MAKE_TUPLE_HEAP_H

// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, Aug 2020.

/// @file
/// @brief Helpers for storing the arguments of a non-blocking async call.
///
/// @details `DelegateAsyncMsg` (`DelegateAsync.h`) builds its `std::tuple<Args...>` of
/// arguments with `store_async_arg()`, one argument at a time, for transport through a
/// thread message queue. By-value arguments are stored in the tuple directly; for
/// pointer, pointer-to-pointer and reference arguments, `tuple_append()` makes a heap
/// copy of the data, freed by a `heap_arg_deleter` once the call is done.
///
/// The destination thread uses `std::apply()` to invoke the target function using
/// the tuple of arguments.

#include <tuple>
#include <memory>
#include <type_traits>
#include "DelegateOpt.h"
DMQ_OPTIMIZE_ON

namespace dmq 
{
template<class T>
struct is_shared_ptr : std::false_type {};

template<class T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template<class T>
struct is_shared_ptr<std::shared_ptr<T>&> : std::true_type {};

template<class T>
struct is_shared_ptr<const std::shared_ptr<T>&> : std::true_type {};

template<class T>
struct is_shared_ptr<std::shared_ptr<T>*> : std::true_type {};

template<class T>
struct is_shared_ptr<const std::shared_ptr<T>*> : std::true_type {};

template<class T>
struct is_unique_ptr : std::false_type {};

template<class T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};
   
/// @brief Base class for all deleter's
class heap_arg_deleter_base
{
public:
    virtual ~heap_arg_deleter_base() = default;

    XALLOCATOR
};

/// @brief Forward declaration for heap argument deleter
template<typename T>
class heap_arg_deleter;

/// @brief Frees heap memory for pointer heap argument
template<typename T>
class heap_arg_deleter<T*> : public heap_arg_deleter_base
{
public:
    heap_arg_deleter(T* arg) : m_arg(arg) { }
    virtual ~heap_arg_deleter() {
        xdelete(m_arg);
    }
private:
    T* m_arg;
};

/// @brief Frees heap memory for pointer to pointer heap argument
template<typename T>
class heap_arg_deleter<T**> : public heap_arg_deleter_base
{
public:
    // Capture the original inner pointer at construction so the destructor always
    // frees the xnew'd copy, regardless of whether the target function overwrites
    // *arg (e.g. an outgoing-argument pattern like (*s) = new T).
    heap_arg_deleter(T** arg) : m_arg(arg), m_inner(arg ? *arg : nullptr) {}
    virtual ~heap_arg_deleter() {
        if (m_arg && *m_arg != m_inner) {
            xdelete(*m_arg);    // free the newly assigned pointer if overwritten by target function
        }
        xdelete(m_inner);       // free the original xnew'd inner copy (may be nullptr)
        xdelete(m_arg);         // free the outer pointer storage
    }
private:
    T** m_arg;
    T*  m_inner;  // original value of *arg at dispatch time
};

/// @brief Append a pointer to pointer argument to the tuple
template <typename Arg, typename... TupleElem>
auto tuple_append(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, const std::tuple<TupleElem...> &tup, Arg** arg)
{
    Arg** heap_arg = nullptr;
    Arg** del_arg = nullptr;

    if (arg != nullptr && *arg != nullptr) {
        // Allocate memory for heap_arg and copy the value
        heap_arg = xnew<Arg*>();
        if (!heap_arg) {
            // BAD_ALLOC() never returns: DMQ_ASSERTS calls the noreturn FaultHandler,
            // otherwise it throws std::bad_alloc. Nothing below this call executes.
            BAD_ALLOC();
        } else {
            *heap_arg = xnew<Arg>(**arg);
            if (!*heap_arg) {
                xdelete(heap_arg);
                BAD_ALLOC();
            } else {
                del_arg = heap_arg;
            }
        }
    }
    else {
        // If arg is nullptr or *arg is nullptr, create heap_arg as nullptr
        heap_arg = xnew<Arg*>(nullptr);
        if (!heap_arg) {
            BAD_ALLOC();
        } else {
            del_arg = heap_arg;
        }
    }

    auto deleter = xmake_shared<heap_arg_deleter<Arg**>>(del_arg);
    if (!deleter) {
        if (del_arg) {
            if (*del_arg) xdelete(*del_arg);
            xdelete(del_arg);
        }
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    if (deleter) {
        heapArgs.push_back(deleter);
    }
    return std::tuple_cat(tup, std::make_tuple(heap_arg));
#else
    try {
        heapArgs.push_back(deleter);
        return std::tuple_cat(tup, std::make_tuple(heap_arg));
    }
    catch (const std::bad_alloc&) {
        BAD_ALLOC();
        throw;
    }
#endif
}

/// @brief Append a pointer argument to the tuple
template <typename Arg, typename... TupleElem>
auto tuple_append(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, const std::tuple<TupleElem...> &tup, Arg* arg)
{
    Arg* heap_arg = nullptr;
    Arg* del_arg = nullptr;
    if (arg != nullptr) {
        heap_arg = xnew<Arg>(*arg);
        if (!heap_arg) {
            // BAD_ALLOC() never returns: DMQ_ASSERTS calls the noreturn FaultHandler,
            // otherwise it throws std::bad_alloc. Nothing below this call executes.
            BAD_ALLOC();
        } else {
            del_arg = heap_arg;
        }
    }
    auto deleter = xmake_shared<heap_arg_deleter<Arg*>>(del_arg);
    if (!deleter) {
        if (del_arg) {
            xdelete(del_arg);
        }
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    if (deleter) {
        heapArgs.push_back(deleter);
    }
    return std::tuple_cat(tup, std::make_tuple(heap_arg));
#else
    try {
        heapArgs.push_back(deleter);
        return std::tuple_cat(tup, std::make_tuple(heap_arg));
    }
    catch (const std::bad_alloc&) {
        BAD_ALLOC();
        throw;
    }
#endif
}

/// @brief Append a reference argument to the tuple
template <typename Arg, typename... TupleElem>
auto tuple_append(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, const std::tuple<TupleElem...> &tup, Arg& arg)
{
    Arg* heap_arg = xnew<Arg>(arg);
    Arg* del_arg = nullptr;
    if (!heap_arg) {
        // BAD_ALLOC() never returns: DMQ_ASSERTS calls the noreturn FaultHandler,
        // otherwise it throws std::bad_alloc. Nothing below this call executes.
        BAD_ALLOC();
    } else {
        del_arg = heap_arg;
    }
    auto deleter = xmake_shared<heap_arg_deleter<Arg*>>(del_arg);
    if (!deleter) {
        if (del_arg) {
            xdelete(del_arg);
        }
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    if (deleter) {
        heapArgs.push_back(deleter);
    }

    auto temp = std::make_tuple(std::forward_as_tuple(*heap_arg));  // Dereference heap_arg when creating tuple element
    auto new_type = std::get<0>(temp);
    return std::tuple_cat(tup, new_type);
#else
    try {
        heapArgs.push_back(deleter);

        auto temp = std::make_tuple(std::forward_as_tuple(*heap_arg));  // Dereference heap_arg when creating tuple element
        auto new_type = std::get<0>(temp);
        return std::tuple_cat(tup, new_type);
    }
    catch (const std::bad_alloc&) {
        BAD_ALLOC();
        throw;
    }
#endif
}

/// @brief Store one argument of a non-blocking async call. Chooses by `Param`, the
/// bound function's declared parameter type, not by how the argument arrives.
/// @details
/// - By-value parameter: returned as-is (moved). The message's `std::tuple<Args...>`
///   element holds its own copy, so a heap copy would only be copied again.
/// - Reference or pointer parameter: the caller's data is copied to the heap (freed
///   through `heapArgs` once the call is done) and a reference/pointer to that copy is
///   returned, using the `tuple_append()` overloads above.
///
/// This replaced `make_tuple_heap()`, which heap-copied every argument, by-value ones
/// too. An earlier attempt to skip the copy for values added an `Arg&&` overload of
/// `tuple_append()`, i.e. chose by the argument's value category. It silently stopped
/// applying when `make_tuple_heap()` began passing its arguments on as lvalues, and was
/// later removed. Deciding from `Param` with `if constexpr` doesn't depend on overload
/// resolution.
/// @tparam Param The declared parameter type of the bound function.
/// @param heapArgs The list of deleters for heap-allocated arguments.
/// @param arg The argument, forwarded as `Param`.
/// @return The value (by-value `Param`), or a reference/pointer to the heap copy.
template <typename Param, typename A>
decltype(auto) store_async_arg(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, A&& arg)
{
    static_assert(!std::is_same<std::decay_t<Param>, void*>::value, "void* argument not allowed");
    if constexpr (std::is_reference_v<Param>) {
        // Pass as an lvalue so tuple_append()'s Arg& overload makes the heap copy;
        // the returned element is a reference to that copy.
        auto& lv = arg;
        return std::get<0>(tuple_append(heapArgs, std::tuple<>(), lv));
    } else if constexpr (std::is_pointer_v<Param>) {
        // Pointer and pointer-to-pointer: heap copy of the pointee, as before.
        auto& lv = arg;
        Param heapPtr = std::get<0>(tuple_append(heapArgs, std::tuple<>(), lv));
        return heapPtr;
    } else {
        return std::forward<A>(arg);
    }
}

}

DMQ_OPTIMIZE_OFF

#endif
