#ifndef _MAKE_TUPLE_HEAP_H
#define _MAKE_TUPLE_HEAP_H

// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, Aug 2020.

/// @file
/// @brief Helper functions for creating dynamic copies of function arguments stored
/// within a tuple.
/// 
/// @details The template function `make_tuple_heap()` creates dynamic copies of 
/// function arguments, storing them within a tuple for transport through a thread
/// message queue. It supports all types of function arguments, including by value,
/// pointer, pointer-to-pointer, and reference.
/// 
/// The destination thread uses `std::apply()` to invoke the target function using
/// the tuple of arguments. See `Invoke()` and `DelegateAsyncMsg()` in the
/// file `DelegateAsync.h` for example usage.

#include <tuple>
#include <memory>
#include <type_traits>
#include "DelegateOpt.h"

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

/// @brief Frees heap memory for reference heap argument
template<typename T>
class heap_arg_deleter : public heap_arg_deleter_base
{
public:
    heap_arg_deleter(T& arg) : m_arg(arg) { }
    virtual ~heap_arg_deleter() {
        xdelete(&m_arg);
    }
private:
    T& m_arg;
};

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
    heap_arg_deleter(T** arg) : m_arg(arg), m_inner(*arg) {}
    virtual ~heap_arg_deleter() {
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

    // Check if arg is nullptr or *arg is nullptr
    if (arg != nullptr && *arg != nullptr) {
        // Allocate memory for heap_arg and copy the value
        heap_arg = xnew<Arg*>();
        if (!heap_arg) {
            BAD_ALLOC();
        }

        *heap_arg = xnew<Arg>(**arg);
        if (!*heap_arg) {
            xdelete(heap_arg);
            BAD_ALLOC();
        }
    }
    else {
        // If arg is nullptr or *arg is nullptr, create heap_arg as nullptr
        heap_arg = xnew<Arg*>(nullptr);
        if (!heap_arg) {
            BAD_ALLOC();
        }
    }
    auto deleter = xmake_shared<heap_arg_deleter<Arg**>>(heap_arg);
    if (!deleter) {
        xdelete(*heap_arg);
        xdelete(heap_arg);
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    heapArgs.push_back(deleter);
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
    if (arg != nullptr) {
        heap_arg = xnew<Arg>(*arg);
        if (!heap_arg) {
            BAD_ALLOC();
        }
    }
    auto deleter = xmake_shared<heap_arg_deleter<Arg*>>(heap_arg);
    if (!deleter) {
        xdelete(heap_arg);
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    heapArgs.push_back(deleter);
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
    if (!heap_arg) {
        BAD_ALLOC();
    }
    auto deleter = xmake_shared<heap_arg_deleter<Arg*>>(heap_arg);
    if (!deleter) {
        xdelete(heap_arg);
        BAD_ALLOC();
    }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
    heapArgs.push_back(deleter);

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

/// @brief Terminate the template metaprogramming argument loop. This function is 
/// called when there are no more arguments to process.
/// @tparam Ts The types of the remaining arguments.
/// @param heapArgs The list of deleters for heap - allocated arguments.
/// @param tup The current tuple of arguments.
/// @return The final tuple.
template<typename... Ts>
auto make_tuple_heap(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, std::tuple<Ts...> tup)
{
    (void)heapArgs;
    return tup;
}

/// @brief Creates a tuple with all tuple elements created on the heap using
/// `operator new()`. 
/// @details Call with an empty list and empty tuple. The empty tuple is concatenated
/// with each heap element. The list contains `heap_arg_deleter_base` objects for each 
/// argument heap memory block that will be automatically deleted after the bound
/// function is invoked on the target thread.
/// @tparam Arg1 The type of the first argument.
/// @tparam Args The types of the remaining arguments.
/// @tparam Ts The types of the existing tuple elements.
/// @param heapArgs The list of deleters for heap - allocated arguments.
/// @param tup The existing tuple of arguments. Typically call with an empty tuple. 
/// @param arg1 The first argument to append to the tuple.
/// @param args The remaining arguments to append to the tuple.
/// @return A new tuple with all arguments appended.
/// @throws std::bad_alloc If dynamic allocation of arguments created on the heap
/// for appending to the tuple fails and DMQ_ASSERTS not defined.
template<typename Arg1, typename... Args, typename... Ts>
auto make_tuple_heap(xlist<std::shared_ptr<heap_arg_deleter_base>>& heapArgs, std::tuple<Ts...> tup, Arg1 arg1, Args... args)
{
    static_assert(!std::is_same<std::decay_t<Arg1>, void*>::value, "void* argument not allowed");
    auto new_tup = tuple_append(heapArgs, tup, arg1);
    return make_tuple_heap(heapArgs, new_tup, args...);
}

}

#endif
