#ifndef _DELEGATE_ASYNC_H
#define _DELEGATE_ASYNC_H

// DelegateAsync.h
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, Aug 2020.

/// @file
/// @brief Delegate "`Async`" series of classes used to invoke a function asynchronously. 
/// 
/// @details The classes are not thread safe. Invoking a function asynchronously requires 
/// sending a clone of the object to the destination thread message queue. The destination 
/// thread calls `Invoke()` to invoke the target function.
/// 
/// A `IThread` implementation is required to serialize and dispatch an async delegate onto
/// a destination thread of control. 
/// 
/// Argument data is created on the heap using `operator new` for transport thought a thread 
/// message queue. An optional fixed-block allocator is available. See `DMQ_ALLOCATOR`. 
/// 
/// `RetType operator()(Args... args)` - called by the source thread to initiate the async
/// function call. May throw `std::bad_alloc` if dynamic storage allocation fails and `DMQ_ASSERTS` 
/// is not defined. Clone() may also throw `std::bad_alloc` unless `DMQ_ASSERTS`. All other delegate 
/// class functions do not throw exceptions.
///
/// `void Invoke(std::shared_ptr<DelegateMsg> msg)` - called by the destination
/// thread to invoke the target function. The destination thread must not call any other
/// delegate instance functions.
/// 
/// Limitations:
/// 
/// * The target function return value is not valid after invoke since the delegate does 
/// not wait for the target function to be called.
/// 
/// * Cannot use a `void*` as a target function argument.
/// 
/// * Cannot use rvalue reference (T&&) as a target function argument.
/// 
/// * Cannot insert `DelegateMemberAsync` into an ordered container. e.g. `std::list` ok, 
/// `std::set` not ok.
/// 
/// * `std::function` compares the function signature type, not the underlying object instance.
/// See `DelegateFunction<>` class for more info.
/// 
/// Code within `<common_code>` and `</common_code>` is updated using src_dup.py. Manually update 
/// the code within the `DelegateFreeAsync` `common_code` tags, then run the script to 
/// propagate to the remaining delegate classes to simplify code maintenance.
/// 
/// `python src_dup.py DelegateAsync.h`

#include "Delegate.h"
#include "IThread.h"
#include "IInvoker.h"
#include <tuple>

namespace dmq {

/// @brief Stores all function arguments suitable for non-blocking asynchronous calls.
/// Argument data is stored in the heap.
/// @tparam Args The argument types of the bound delegate function.
template <class...Args>
class DelegateAsyncMsg : public DelegateMsg
{
public:
    /// Constructor
    /// @param[in] invoker - the invoker instance
    /// @param[in] priority - the delegate message priority
    /// @param[in] args - a parameter pack of all target function arguments
    /// @throws std::bad_alloc If make_tuble_heap() fails to obtain memory and DMQ_ASSERTS not defined.
    DelegateAsyncMsg(std::shared_ptr<IThreadInvoker> invoker, Priority priority, Args... args) : DelegateMsg(invoker, priority),
        m_args(make_tuple_heap(m_heapMem, m_start, std::forward<Args>(args)...)) {
    }

    /// Delete the default constructor
    DelegateAsyncMsg() = delete;

    /// Delete the copy constructor
    DelegateAsyncMsg(const DelegateAsyncMsg&) = delete;

    /// Delete the copy assignment operator
    DelegateAsyncMsg& operator=(const DelegateAsyncMsg&) = delete;

    /// Delete the move constructor and move assignment
    DelegateAsyncMsg(DelegateAsyncMsg&&) = delete;
    DelegateAsyncMsg& operator=(DelegateAsyncMsg&&) = delete;

    virtual ~DelegateAsyncMsg() = default;

    /// Get all function arguments that were created on the heap
    /// @return A tuple of all function arguments
    std::tuple<Args...>& GetArgs() { return m_args; }

private:
    /// A list of heap allocated argument memory blocks
    xlist<std::shared_ptr<heap_arg_deleter_base>> m_heapMem;

    /// An empty starting tuple
    std::tuple<> m_start;

    /// A tuple with each element stored within the heap
    std::tuple<Args...> m_args;
};

template <class R>
class DelegateFreeAsync; // Not defined

/// @brief `DelegateFreeAsync<>` class asynchronously invokes a free target function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFreeAsync<RetType(Args...)> : public DelegateFree<RetType(Args...)>, public IThreadInvoker {
public:
    typedef RetType(*FreeFunc)(Args...);
    using ClassType = DelegateFreeAsync<RetType(Args...)>;
    using BaseType = DelegateFree<RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] func The target free function to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateFreeAsync(FreeFunc func, IThread& thread) :
        BaseType(func), m_thread(&thread) {
        Bind(func, thread);
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFreeAsync(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFreeAsync(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_thread(rhs.m_thread), m_priority(rhs.m_priority) {
        rhs.Clear();
    }

    DelegateFreeAsync() = default;

    /// @brief Bind a free function to the delegate.
    /// @details This method associates a free function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The free function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(FreeFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_thread = rhs.m_thread;
        m_priority = rhs.m_priority;
        BaseType::Assign(rhs);
    }
    /// @brief Creates a copy of the current object.
    /// @details Clones the current instance of the class by creating a new object
    /// and copying the state of the current object to it. 
    /// @return A pointer to a new `ClassType` instance or nullptr if allocation fails.
    /// @post The caller is responsible for deleting the clone object and checking for 
    /// nullptr.
    virtual ClassType* Clone() const override {
        return new(std::nothrow) ClassType(*this);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            BaseType::operator=(rhs);
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            BaseType::operator=(std::move(rhs));
            m_thread = rhs.m_thread;    // Use the resource
            m_priority = rhs.m_priority;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept override {
        return this->Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_thread == derivedRhs->m_thread &&
            m_priority == derivedRhs->m_priority &&
            BaseType::Equal(rhs);
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return this->Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !this->Empty();
    }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    friend bool operator==(std::nullptr_t, const ClassType& rhs) noexcept {
        return rhs.Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    friend bool operator!=(std::nullptr_t, const ClassType& rhs) noexcept {
        return !rhs.Empty();
    }

    /// @brief Invoke the bound delegate function asynchronously. Called by the source thread.
    /// @details Invoke delegate function asynchronously and do not wait for return value.
    /// This function is called by the source thread. Dispatches the delegate data into the 
    /// destination thread message queue. `Invoke()` must be called by the destination 
    /// thread to invoke the target function. Always safe to call.
    /// 
    /// The `DelegateAsyncMsg` duplicates and copies the function arguments into heap memory. 
    /// The source thread is not required to place function arguments into the heap. The delegate
    /// library performs all necessary heap and argument coping for the caller. Ensure complex
    /// argument data types can be safely copied by creating a copy constructor if necessary. 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    /// @throws std::bad_alloc If dynamic memory allocation fails and DMQ_ASSERTS not defined.
    virtual RetType operator()(Args... args) override {
        if (this->Empty())
            return RetType();

        // Synchronously invoke the target function?
        if (m_sync) {
            // Invoke the target function directly
            return BaseType::operator()(std::forward<Args>(args)...);
        }
        else {
            // Create a clone instance of this delegate 
            auto delegate = xmake_shared<ClassType>(*this);
            if (!delegate)
                BAD_ALLOC();

            // Create a new message instance for sending to the destination thread
            auto msg = xmake_shared<DelegateAsyncMsg<Args...>>(delegate, m_priority, std::forward<Args>(args)...);
            if (!msg)
                BAD_ALLOC();

            auto thread = this->GetThread();
            if (thread) {
                // Dispatch message onto the callback destination thread. Invoke()
                // will be called by the destintation thread. 
                bool success = thread->DispatchDelegate(msg);
                (void)success;
            }

            // Do not wait for destination thread return value from async function call
            return RetType();

            // Check if any argument is a shared_ptr with wrong usage
            // std::shared_ptr reference arguments are not allowed with asynchronous delegates as the behavior is 
            // undefined. In other words:
            // void MyFunc(std::shared_ptr<T> data)		// Ok!
            // void MyFunc(std::shared_ptr<T>& data)	// Error if DelegateAsync or DelegateSpAsync target!
            static_assert(!(
                std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
                "std::shared_ptr reference argument not allowed");
        }
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the source thread. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination thread. Called by the 
    /// destintation thread.
    /// @details Each source thread call to `operator()` generate a call to `Invoke()` 
    /// on the destination thread. Unlike `DelegateAsyncWait`, a lock is not required between 
    /// source and destination `delegateMsg` access because the source thread is not waiting 
    /// for the function call to complete.
    /// @param[in] msg The delegate message created and sent within `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) override {
        // Typecast the base pointer to back correct derived to instance
        auto delegateMsg = std::dynamic_pointer_cast<DelegateAsyncMsg<Args...>>(msg);
        if (delegateMsg == nullptr)
            return false;

        // Invoke the delegate function synchronously
        m_sync = true;

        // Invoke the target function using the source thread supplied function arguments
        std::apply(&BaseType::operator(),
            std::tuple_cat(std::make_tuple(this), delegateMsg->GetArgs()));
        return true;
    }

    /// @brief Get the destination thread that the target function is invoked on.
    /// @return The target thread.
    IThread* GetThread() const noexcept { return m_thread; }

    /// @brief Get the delegate message priority
    /// @return Delegate message priority
    Priority GetPriority() const noexcept { return m_priority; }
    void SetPriority(Priority priority) noexcept { m_priority = priority; }

private:
    /// The target thread to invoke the delegate function.
    IThread* m_thread = nullptr;

    /// Flag to control synchronous vs asynchronous target invoke behavior.
    bool m_sync = false;

    /// The delegate message priority
    Priority m_priority = Priority::NORMAL;

    // </common_code>
};

template <class C, class R>
class DelegateMemberAsync; // Not defined

/// @brief `DelegateMemberAsync<>` class asynchronously invokes a class member target function.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class TClass, class RetType, class... Args>
class DelegateMemberAsync<TClass, RetType(Args...)> : public DelegateMember<TClass, RetType(Args...)>, public IThreadInvoker {
public:
    typedef TClass* ObjectPtr;
    typedef std::shared_ptr<TClass> SharedPtr;
    typedef RetType(TClass::* MemberFunc)(Args...);
    typedef RetType(TClass::* ConstMemberFunc)(Args...) const;
    using ClassType = DelegateMemberAsync<TClass, RetType(Args...)>;
    using BaseType = DelegateMember<TClass, RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateMemberAsync(SharedPtr object, MemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateMemberAsync(SharedPtr object, ConstMemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateMemberAsync(ObjectPtr object, MemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateMemberAsync(ObjectPtr object, ConstMemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateMemberAsync(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateMemberAsync(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_thread(rhs.m_thread), m_priority(rhs.m_priority) {
        rhs.Clear();
    }

    DelegateMemberAsync() = default;

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The function to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(SharedPtr object, MemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    /// @brief Bind a const member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The member function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(SharedPtr object, ConstMemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The function to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(ObjectPtr object, MemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    /// @brief Bind a const member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The member function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(ObjectPtr object, ConstMemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_thread = rhs.m_thread;
        m_priority = rhs.m_priority;
        BaseType::Assign(rhs);
    }
    /// @brief Creates a copy of the current object.
    /// @details Clones the current instance of the class by creating a new object
    /// and copying the state of the current object to it. 
    /// @return A pointer to a new `ClassType` instance or nullptr if allocation fails.
    /// @post The caller is responsible for deleting the clone object and checking for 
    /// nullptr.
    virtual ClassType* Clone() const override {
        return new(std::nothrow) ClassType(*this);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            BaseType::operator=(rhs);
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            BaseType::operator=(std::move(rhs));
            m_thread = rhs.m_thread;    // Use the resource
            m_priority = rhs.m_priority;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept override {
        return this->Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_thread == derivedRhs->m_thread &&
            m_priority == derivedRhs->m_priority &&
            BaseType::Equal(rhs);
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return this->Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !this->Empty();
    }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    friend bool operator==(std::nullptr_t, const ClassType& rhs) noexcept {
        return rhs.Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    friend bool operator!=(std::nullptr_t, const ClassType& rhs) noexcept {
        return !rhs.Empty();
    }

    /// @brief Invoke the bound delegate function asynchronously. Called by the source thread.
    /// @details Invoke delegate function asynchronously and do not wait for return value.
    /// This function is called by the source thread. Dispatches the delegate data into the 
    /// destination thread message queue. `Invoke()` must be called by the destination 
    /// thread to invoke the target function. Always safe to call.
    /// 
    /// The `DelegateAsyncMsg` duplicates and copies the function arguments into heap memory. 
    /// The source thread is not required to place function arguments into the heap. The delegate
    /// library performs all necessary heap and argument coping for the caller. Ensure complex
    /// argument data types can be safely copied by creating a copy constructor if necessary. 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    /// @throws std::bad_alloc If dynamic memory allocation fails and DMQ_ASSERTS not defined.
    virtual RetType operator()(Args... args) override {
        if (this->Empty())
            return RetType();

        // Synchronously invoke the target function?
        if (m_sync) {
            // Invoke the target function directly
            return BaseType::operator()(std::forward<Args>(args)...);
        }
        else {
            // Create a clone instance of this delegate 
            auto delegate = xmake_shared<ClassType>(*this);
            if (!delegate)
                BAD_ALLOC();

            // Create a new message instance for sending to the destination thread
            auto msg = xmake_shared<DelegateAsyncMsg<Args...>>(delegate, m_priority, std::forward<Args>(args)...);
            if (!msg)
                BAD_ALLOC();

            auto thread = this->GetThread();
            if (thread) {
                // Dispatch message onto the callback destination thread. Invoke()
                // will be called by the destintation thread. 
                bool success = thread->DispatchDelegate(msg);
                (void)success;
            }

            // Do not wait for destination thread return value from async function call
            return RetType();

            // Check if any argument is a shared_ptr with wrong usage
            // std::shared_ptr reference arguments are not allowed with asynchronous delegates as the behavior is 
            // undefined. In other words:
            // void MyFunc(std::shared_ptr<T> data)		// Ok!
            // void MyFunc(std::shared_ptr<T>& data)	// Error if DelegateAsync or DelegateSpAsync target!
            static_assert(!(
                std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
                "std::shared_ptr reference argument not allowed");
        }
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the source thread. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination thread. Called by the 
    /// destintation thread.
    /// @details Each source thread call to `operator()` generate a call to `Invoke()` 
    /// on the destination thread. Unlike `DelegateAsyncWait`, a lock is not required between 
    /// source and destination `delegateMsg` access because the source thread is not waiting 
    /// for the function call to complete.
    /// @param[in] msg The delegate message created and sent within `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) override {
        // Typecast the base pointer to back correct derived to instance
        auto delegateMsg = std::dynamic_pointer_cast<DelegateAsyncMsg<Args...>>(msg);
        if (delegateMsg == nullptr)
            return false;

        // Invoke the delegate function synchronously
        m_sync = true;

        // Invoke the target function using the source thread supplied function arguments
        std::apply(&BaseType::operator(),
            std::tuple_cat(std::make_tuple(this), delegateMsg->GetArgs()));
        return true;
    }

    /// @brief Get the destination thread that the target function is invoked on.
    /// @return The target thread.
    IThread* GetThread() const noexcept { return m_thread; }

    /// @brief Get the delegate message priority
    /// @return Delegate message priority
    Priority GetPriority() const noexcept { return m_priority; }
    void SetPriority(Priority priority) noexcept { m_priority = priority; }

private:
    /// The target thread to invoke the delegate function.
    IThread* m_thread = nullptr;

    /// Flag to control synchronous vs asynchronous target invoke behavior.
    bool m_sync = false;

    /// The delegate message priority
    Priority m_priority = Priority::NORMAL;

    // </common_code>
};

template <class C, class R>
class DelegateMemberAsyncSp; // Not defined

/// @brief `DelegateMemberAsyncSp<>` class asynchronously invokes a class member target function
/// using a weak pointer (safe from use-after-free).
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class TClass, class RetType, class... Args>
class DelegateMemberAsyncSp<TClass, RetType(Args...)> : public DelegateMemberSp<TClass, RetType(Args...)>, public IThreadInvoker {
public:
    typedef TClass* ObjectPtr;
    typedef std::shared_ptr<TClass> SharedPtr;
    typedef RetType(TClass::* MemberFunc)(Args...);
    typedef RetType(TClass::* ConstMemberFunc)(Args...) const;
    using ClassType = DelegateMemberAsyncSp<TClass, RetType(Args...)>;
    using BaseType = DelegateMemberSp<TClass, RetType(Args...)>;

    DelegateMemberAsyncSp(SharedPtr object, MemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    DelegateMemberAsyncSp(SharedPtr object, ConstMemberFunc func, IThread& thread) : BaseType(object, func), m_thread(&thread) {
        Bind(object, func, thread);
    }

    DelegateMemberAsyncSp(const ClassType& rhs) : BaseType(rhs) { Assign(rhs); }

    DelegateMemberAsyncSp(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_thread(rhs.m_thread), m_priority(rhs.m_priority) {
        rhs.Clear();
    }

    DelegateMemberAsyncSp() = default;

    void Bind(SharedPtr object, MemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    void Bind(SharedPtr object, ConstMemberFunc func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(object, func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_thread = rhs.m_thread;
        m_priority = rhs.m_priority;
        BaseType::Assign(rhs);
    }
    /// @brief Creates a copy of the current object.
    /// @details Clones the current instance of the class by creating a new object
    /// and copying the state of the current object to it. 
    /// @return A pointer to a new `ClassType` instance or nullptr if allocation fails.
    /// @post The caller is responsible for deleting the clone object and checking for 
    /// nullptr.
    virtual ClassType* Clone() const override {
        return new(std::nothrow) ClassType(*this);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            BaseType::operator=(rhs);
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            BaseType::operator=(std::move(rhs));
            m_thread = rhs.m_thread;    // Use the resource
            m_priority = rhs.m_priority;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept override {
        return this->Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_thread == derivedRhs->m_thread &&
            m_priority == derivedRhs->m_priority &&
            BaseType::Equal(rhs);
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return this->Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !this->Empty();
    }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    friend bool operator==(std::nullptr_t, const ClassType& rhs) noexcept {
        return rhs.Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    friend bool operator!=(std::nullptr_t, const ClassType& rhs) noexcept {
        return !rhs.Empty();
    }

    /// @brief Invoke the bound delegate function asynchronously. Called by the source thread.
    /// @details Invoke delegate function asynchronously and do not wait for return value.
    /// This function is called by the source thread. Dispatches the delegate data into the 
    /// destination thread message queue. `Invoke()` must be called by the destination 
    /// thread to invoke the target function. Always safe to call.
    /// 
    /// The `DelegateAsyncMsg` duplicates and copies the function arguments into heap memory. 
    /// The source thread is not required to place function arguments into the heap. The delegate
    /// library performs all necessary heap and argument coping for the caller. Ensure complex
    /// argument data types can be safely copied by creating a copy constructor if necessary. 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    /// @throws std::bad_alloc If dynamic memory allocation fails and DMQ_ASSERTS not defined.
    virtual RetType operator()(Args... args) override {
        if (this->Empty())
            return RetType();

        // Synchronously invoke the target function?
        if (m_sync) {
            // Invoke the target function directly
            return BaseType::operator()(std::forward<Args>(args)...);
        }
        else {
            // Create a clone instance of this delegate 
            auto delegate = xmake_shared<ClassType>(*this);
            if (!delegate)
                BAD_ALLOC();

            // Create a new message instance for sending to the destination thread
            auto msg = xmake_shared<DelegateAsyncMsg<Args...>>(delegate, m_priority, std::forward<Args>(args)...);
            if (!msg)
                BAD_ALLOC();

            auto thread = this->GetThread();
            if (thread) {
                // Dispatch message onto the callback destination thread. Invoke()
                // will be called by the destintation thread. 
                bool success = thread->DispatchDelegate(msg);
                (void)success;
            }

            // Do not wait for destination thread return value from async function call
            return RetType();

            // Check if any argument is a shared_ptr with wrong usage
            // std::shared_ptr reference arguments are not allowed with asynchronous delegates as the behavior is 
            // undefined. In other words:
            // void MyFunc(std::shared_ptr<T> data)		// Ok!
            // void MyFunc(std::shared_ptr<T>& data)	// Error if DelegateAsync or DelegateSpAsync target!
            static_assert(!(
                std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
                "std::shared_ptr reference argument not allowed");
        }
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the source thread. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination thread. Called by the 
    /// destintation thread.
    /// @details Each source thread call to `operator()` generate a call to `Invoke()` 
    /// on the destination thread. Unlike `DelegateAsyncWait`, a lock is not required between 
    /// source and destination `delegateMsg` access because the source thread is not waiting 
    /// for the function call to complete.
    /// @param[in] msg The delegate message created and sent within `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) override {
        // Typecast the base pointer to back correct derived to instance
        auto delegateMsg = std::dynamic_pointer_cast<DelegateAsyncMsg<Args...>>(msg);
        if (delegateMsg == nullptr)
            return false;

        // Invoke the delegate function synchronously
        m_sync = true;

        // Invoke the target function using the source thread supplied function arguments
        std::apply(&BaseType::operator(),
            std::tuple_cat(std::make_tuple(this), delegateMsg->GetArgs()));
        return true;
    }

    /// @brief Get the destination thread that the target function is invoked on.
    /// @return The target thread.
    IThread* GetThread() const noexcept { return m_thread; }

    /// @brief Get the delegate message priority
    /// @return Delegate message priority
    Priority GetPriority() const noexcept { return m_priority; }
    void SetPriority(Priority priority) noexcept { m_priority = priority; }

private:
    /// The target thread to invoke the delegate function.
    IThread* m_thread = nullptr;

    /// Flag to control synchronous vs asynchronous target invoke behavior.
    bool m_sync = false;

    /// The delegate message priority
    Priority m_priority = Priority::NORMAL;

    // </common_code>
};

template <class R>
class DelegateFunctionAsync; // Not defined

/// @brief `DelegateFunctionAsync<>` class asynchronously invokes a `std::function` target function.
/// @details Caution when binding to a `std::function` using this class. `std::function` cannot be 
/// compared for equality directly in a meaningful way using `operator==`. Therefore, the delegate
/// library used 
/// 
/// See `DelegateFunction<>` base class for important usage limitations.
/// 
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFunctionAsync<RetType(Args...)> : public DelegateFunction<RetType(Args...)>, public IThreadInvoker {
public:
    using FunctionType = std::function<RetType(Args...)>;
    using ClassType = DelegateFunctionAsync<RetType(Args...)>;
    using BaseType = DelegateFunction<RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] func The target `std::function` to store.
    /// @param[in] thread The execution thread to invoke `func`.
    DelegateFunctionAsync(FunctionType func, IThread& thread) :
        BaseType(func), m_thread(&thread) {
        Bind(func, thread);
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFunctionAsync(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFunctionAsync(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_thread(rhs.m_thread), m_priority(rhs.m_priority) {
        rhs.Clear();
    }

    DelegateFunctionAsync() = default;

    /// @brief Bind a `std::function` to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The `std::function` to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] thread The execution thread to invoke `func`.
    void Bind(FunctionType func, IThread& thread) {
        m_thread = &thread;
        BaseType::Bind(func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_thread = rhs.m_thread;
        m_priority = rhs.m_priority;
        BaseType::Assign(rhs);
    }
    /// @brief Creates a copy of the current object.
    /// @details Clones the current instance of the class by creating a new object
    /// and copying the state of the current object to it. 
    /// @return A pointer to a new `ClassType` instance or nullptr if allocation fails.
    /// @post The caller is responsible for deleting the clone object and checking for 
    /// nullptr.
    virtual ClassType* Clone() const override {
        return new(std::nothrow) ClassType(*this);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            BaseType::operator=(rhs);
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            BaseType::operator=(std::move(rhs));
            m_thread = rhs.m_thread;    // Use the resource
            m_priority = rhs.m_priority;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept override {
        return this->Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_thread == derivedRhs->m_thread &&
            m_priority == derivedRhs->m_priority &&
            BaseType::Equal(rhs);
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return this->Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !this->Empty();
    }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    friend bool operator==(std::nullptr_t, const ClassType& rhs) noexcept {
        return rhs.Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    friend bool operator!=(std::nullptr_t, const ClassType& rhs) noexcept {
        return !rhs.Empty();
    }

    /// @brief Invoke the bound delegate function asynchronously. Called by the source thread.
    /// @details Invoke delegate function asynchronously and do not wait for return value.
    /// This function is called by the source thread. Dispatches the delegate data into the 
    /// destination thread message queue. `Invoke()` must be called by the destination 
    /// thread to invoke the target function. Always safe to call.
    /// 
    /// The `DelegateAsyncMsg` duplicates and copies the function arguments into heap memory. 
    /// The source thread is not required to place function arguments into the heap. The delegate
    /// library performs all necessary heap and argument coping for the caller. Ensure complex
    /// argument data types can be safely copied by creating a copy constructor if necessary. 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    /// @throws std::bad_alloc If dynamic memory allocation fails and DMQ_ASSERTS not defined.
    virtual RetType operator()(Args... args) override {
        if (this->Empty())
            return RetType();

        // Synchronously invoke the target function?
        if (m_sync) {
            // Invoke the target function directly
            return BaseType::operator()(std::forward<Args>(args)...);
        }
        else {
            // Create a clone instance of this delegate 
            auto delegate = xmake_shared<ClassType>(*this);
            if (!delegate)
                BAD_ALLOC();

            // Create a new message instance for sending to the destination thread
            auto msg = xmake_shared<DelegateAsyncMsg<Args...>>(delegate, m_priority, std::forward<Args>(args)...);
            if (!msg)
                BAD_ALLOC();

            auto thread = this->GetThread();
            if (thread) {
                // Dispatch message onto the callback destination thread. Invoke()
                // will be called by the destintation thread. 
                bool success = thread->DispatchDelegate(msg);
                (void)success;
            }

            // Do not wait for destination thread return value from async function call
            return RetType();

            // Check if any argument is a shared_ptr with wrong usage
            // std::shared_ptr reference arguments are not allowed with asynchronous delegates as the behavior is 
            // undefined. In other words:
            // void MyFunc(std::shared_ptr<T> data)		// Ok!
            // void MyFunc(std::shared_ptr<T>& data)	// Error if DelegateAsync or DelegateSpAsync target!
            static_assert(!(
                std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
                "std::shared_ptr reference argument not allowed");
        }
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the source thread. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination thread. Called by the 
    /// destintation thread.
    /// @details Each source thread call to `operator()` generate a call to `Invoke()` 
    /// on the destination thread. Unlike `DelegateAsyncWait`, a lock is not required between 
    /// source and destination `delegateMsg` access because the source thread is not waiting 
    /// for the function call to complete.
    /// @param[in] msg The delegate message created and sent within `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) override {
        // Typecast the base pointer to back correct derived to instance
        auto delegateMsg = std::dynamic_pointer_cast<DelegateAsyncMsg<Args...>>(msg);
        if (delegateMsg == nullptr)
            return false;

        // Invoke the delegate function synchronously
        m_sync = true;

        // Invoke the target function using the source thread supplied function arguments
        std::apply(&BaseType::operator(),
            std::tuple_cat(std::make_tuple(this), delegateMsg->GetArgs()));
        return true;
    }

    /// @brief Get the destination thread that the target function is invoked on.
    /// @return The target thread.
    IThread* GetThread() const noexcept { return m_thread; }

    /// @brief Get the delegate message priority
    /// @return Delegate message priority
    Priority GetPriority() const noexcept { return m_priority; }
    void SetPriority(Priority priority) noexcept { m_priority = priority; }

private:
    /// The target thread to invoke the delegate function.
    IThread* m_thread = nullptr;

    /// Flag to control synchronous vs asynchronous target invoke behavior.
    bool m_sync = false;

    /// The delegate message priority
    Priority m_priority = Priority::NORMAL;

    // </common_code>
};

/// @brief Creates an asynchronous delegate that binds to a free function.
/// @tparam RetType The return type of the free function.
/// @tparam Args The types of the function arguments.
/// @param[in] func A pointer to the free function to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateFreeAsync` object bound to the specified free function and thread.
template <class RetType, class... Args>
auto MakeDelegate(RetType(*func)(Args... args), IThread& thread) {
    return DelegateFreeAsync<RetType(Args...)>(func, thread);
}

/// @brief Creates an asynchronous delegate that binds to a non-const member function.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateMemberAsync` object bound to the specified non-const member function and thread.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args... args), IThread& thread) {
    return DelegateMemberAsync<TClass, RetType(Args...)>(object, func, thread);
}

/// @brief Creates an asynchronous delegate that binds to a const member function.
/// @tparam TClass The class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateMemberAsync` object bound to the specified const member function and thread.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args... args) const, IThread& thread) {
    return DelegateMemberAsync<TClass, RetType(Args...)>(object, func, thread);
}

/// @brief Creates a delegate that binds to a const member function.
/// @tparam TClass The const class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateMemberAsync` object bound to the specified non-const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(const TClass* object, RetType(TClass::* func)(Args... args) const, IThread& thread) {
    return DelegateMemberAsync<const TClass, RetType(Args...)>(object, func, thread);
}

/// @brief Creates an asynchronous delegate that binds to a non-const member function using a shared pointer.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateMemberAsyncSp` (SAFE) bound to the specified non-const member function and thread.
template <class TClass, class RetVal, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetVal(TClass::* func)(Args... args), IThread& thread) {
    return DelegateMemberAsyncSp<TClass, RetVal(Args...)>(object, func, thread);
}


/// @brief Creates an asynchronous delegate that binds to a const member function using a shared pointer.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetVal The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateMemberAsyncSp` (SAFE) bound to the specified const member function and thread.
template <class TClass, class RetVal, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetVal(TClass::* func)(Args... args) const, IThread& thread) {
    return DelegateMemberAsyncSp<TClass, RetVal(Args...)>(object, func, thread);
}

/// @brief Creates an asynchronous delegate that binds to a `std::function`.
/// @tparam RetType The return type of the `std::function`.
/// @tparam Args The types of the function arguments.
/// @param[in] func The `std::function` to bind to the delegate.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateFunctionAsync` object bound to the specified `std::function` and thread.
template <class RetType, class... Args>
auto MakeDelegate(std::function<RetType(Args...)> func, IThread& thread) {
    return DelegateFunctionAsync<RetType(Args...)>(func, thread);
}

/// @brief Creates an asynchronous delegate that binds to a raw lambda or functor.
/// @tparam F The lambda or functor type.
/// @param[in] func The lambda or functor to bind.
/// @param[in] thread The `IThread` on which the function will be invoked asynchronously.
/// @return A `DelegateFunctionAsync` object bound to the specified lambda or functor and thread.
template <typename F, typename = std::enable_if_t<trait::is_callable<F>::value>>
auto MakeDelegate(F&& func, IThread& thread) {
    using Sig = typename trait::function_traits<decltype(&std::remove_reference_t<F>::operator())>::function_type;
    return DelegateFunctionAsync<Sig>(std::forward<F>(func), thread);
}

}

#endif