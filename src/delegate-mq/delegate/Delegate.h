#ifndef _DELEGATE_H
#define _DELEGATE_H

// Delegate.h
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, Aug 2020.

/// @file
/// @brief Delegate series of classes are used to invoke a function synchronously. 
/// @details Delegates support binding to free functions, class instance functions, class 
/// static function, and std::function targets. Lambda functions can be bound to a delegate 
/// when assigned to a `std::function`. The classes are not thread-safe.
/// 
/// Limitations:
/// 
/// * Cannot insert `DelegateMember` into an ordered container. e.g. `std::list` ok, 
/// `std::set` not ok.
/// 
/// * `std::function` compares the function signature type, not the underlying object instance.
/// See `DelegateFunction<>` class for more info.

#include <functional>
#include <memory>
#include "DelegateOpt.h"

/// The delegate library namespace
namespace dmq {

namespace trait
{
    // Helper trait to check if a type is a reference to a std::shared_ptr
    template <typename T>
    struct is_shared_ptr_reference : std::false_type {};

    template <typename T>
    struct is_shared_ptr_reference<std::shared_ptr<T>&> : std::true_type {};

    template <typename T>
    struct is_shared_ptr_reference<const std::shared_ptr<T>&> : std::true_type {};

    template <typename T>
    struct is_shared_ptr_reference<std::shared_ptr<T>*> : std::true_type {};

    template <typename T>
    struct is_shared_ptr_reference<const std::shared_ptr<T>*> : std::true_type {};

    // Helper trait to check if a type is a non-const reference to a std::shared_ptr
    template <typename T>
    struct is_non_const_shared_ptr_reference : std::false_type {};

    template <typename T>
    struct is_non_const_shared_ptr_reference<std::shared_ptr<T>&> : std::true_type {};

    template <typename T>
    struct is_non_const_shared_ptr_reference<std::shared_ptr<T>*> : std::true_type {};



    // Helper trait to check if a type is a double pointer (e.g., int**)
    template <typename T>
    struct is_double_pointer {
        // Remove 'const', 'volatile', and references first
        using RawT = std::remove_cv_t<std::remove_reference_t<T>>;

        static constexpr bool value =
            std::is_pointer_v<RawT> &&
            std::is_pointer_v<std::remove_pointer_t<RawT>>;
    };

    // Helper trait to detect std::function specializations.
    template <typename T>
    struct is_std_function : std::false_type {};

    template <typename Sig>
    struct is_std_function<std::function<Sig>> : std::true_type {};

    // Helper trait to check if a type is a callable (lambda, functor)
    // but NOT a function pointer or std::function (which have their own MakeDelegate overloads).
    template <typename T, typename = void>
    struct is_callable : std::false_type {};

    template <typename T>
    struct is_callable<T, std::void_t<decltype(&std::decay_t<T>::operator())>> :
        std::integral_constant<bool,
            !std::is_pointer_v<std::decay_t<T>> &&
            !is_std_function<std::decay_t<T>>::value> {};

    // Helper to deduce function signature from a member function pointer (like operator())
    template <typename T>
    struct function_traits;

    template <typename ClassType, typename RetType, typename... Args>
    struct function_traits<RetType(ClassType::*)(Args...) const> {
        using function_type = RetType(Args...);
    };

    template <typename ClassType, typename RetType, typename... Args>
    struct function_traits<RetType(ClassType::*)(Args...)> {
        using function_type = RetType(Args...);
    };
}

/// @brief Non-template base class for all delegates.
class DelegateBase {
public:
    DelegateBase() = default;
    virtual ~DelegateBase() noexcept = default;

    /// @brief Compares two delegate objects for equality.
    /// @param rhs The delegate object to compare with the current object.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const DelegateBase& rhs) const noexcept { return Equal(rhs); }

    /// @brief Compares two delegate objects for inequality.
    /// @param rhs The delegate object to compare with the current object.
    /// @return `true` if the objects are not equal, `false` otherwise.
    bool operator!=(const DelegateBase& rhs) const { return !Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept = 0;

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept = 0;

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& other) const = 0;

    /// @brief Clone a delegate instance.
    /// @details Use Clone() to provide a deep copy using a base pointer. Covariant 
    /// overloading is used so that a Clone() method return type is a more 
    /// specific type in the derived class implementations.
    /// @return A dynamic copy of this instance created with `operator new()`. 
    /// @post The caller is responsible for deleting the clone instance. 
    virtual DelegateBase* Clone() const = 0;

    // Optional fixed block allocator for delegates created on the heap 
    // using operator new(). See DMQ_ALLOCATOR in DelegateOpt.h and 
    // ENABLE_ALLOCATOR in CMakeLists.txt.
    XALLOCATOR
};

template <class R>
class Delegate; // Not defined

/// @brief Template base class for all delegates.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class Delegate<RetType(Args...)> : public DelegateBase {
public:
    /// @brief Invoke the bound callable function stored within the 
    /// delegate instance.
    /// @param[in] args The bound function argument(s), if any.
    /// @return The bound function return value, if any.
    virtual RetType operator()(Args... args) = 0;

    /// @brief Clone an instance of a Delegate instance.
    /// @return A new Delegate instance created on the heap. 
    /// @post The caller is responsible for deleting the instance.
    virtual Delegate* Clone() const override = 0;
};

template <class R>
class DelegateFree; // Not defined

/// @brief `DelegateFree<>` class synchronously invokes a free target function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFree<RetType(Args...)> : public Delegate<RetType(Args...)> {
public:
    typedef RetType(*FreeFunc)(Args...);
    using ClassType = DelegateFree<RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] func The target free function to store.
    DelegateFree(FreeFunc func) { Bind(func); }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFree(const ClassType& rhs) { Assign(rhs); }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFree(ClassType&& rhs) noexcept : m_func(rhs.m_func) { rhs.Clear(); }

    /// @brief Default constructor creates an empty delegate.
    DelegateFree() = default;

    /// @brief Destructor ensures empty when destroyed.
    ~DelegateFree() override { Clear(); }

    /// @brief Bind a free function to the delegate.
    /// @details This method associates a free function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The free function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    void Bind(FreeFunc func) { m_func = func; }

    /// Compares two ClassType objects using the '<' operator.
    /// @param rhs The object to compare with.
    /// @return `true` if the current object's value is less than the other object's value,
    /// `false` otherwise.
    bool operator<(const ClassType& rhs) const {
        return m_func < rhs.m_func;
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

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) { 
        m_func = rhs.m_func; 
    }

    /// @brief Invoke the bound delegate function synchronously. Always safe to call.
    /// @param[in] args - the function arguments, if any.
    /// @return The bound function return value, if any. If empty delegate
    /// default return type returned. 
    virtual RetType operator()(Args... args) override {
        if (Empty())
            return RetType();
        return std::invoke(m_func, std::forward<Args>(args)...);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            m_func = rhs.m_func;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept {
        return Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_func == derivedRhs->m_func;
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !Empty();
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

    /// @brief Check if the delegate is bound to a target function.
    /// @return `true` if the delegate has a target function, `false` otherwise.
    bool Empty() const noexcept { return !m_func; }

    /// @brief Clear the target function.
    /// @post The delegate is empty.
    void Clear() noexcept { m_func = nullptr; }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the object is not empty, `false` if the object is empty.
    explicit operator bool() const noexcept { return !Empty(); }

private:
    /// @brief Pointer to a free function, representing the bound target function.
    FreeFunc m_func = nullptr;		
};

template <class C, class R>
class DelegateMember; // Not defined

/// @brief `DelegateMember<>` class synchronously invokes a class member target 
/// function using a class object pointer or shared pointer.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class TClass, class RetType, class... Args>
class DelegateMember<TClass, RetType(Args...)> : public Delegate<RetType(Args...)> {
public:
    typedef TClass* ObjectPtr;
    typedef std::shared_ptr<TClass> SharedPtr;
    typedef RetType(TClass::* MemberFunc)(Args...);
    typedef RetType(TClass::* ConstMemberFunc)(Args...) const;
    using ClassType = DelegateMember<TClass, RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    DelegateMember(SharedPtr object, MemberFunc func) { Bind(object, func); }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    DelegateMember(SharedPtr object, ConstMemberFunc func) { Bind(object, func); }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    DelegateMember(ObjectPtr object, MemberFunc func) { Bind(object, func); }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    DelegateMember(ObjectPtr object, ConstMemberFunc func) { Bind(object, func); }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateMember(const ClassType& rhs) { Assign(rhs); }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateMember(ClassType&& rhs) noexcept : m_object(rhs.m_object), m_func(rhs.m_func) { rhs.Clear(); }

    /// @brief Default constructor creates an empty delegate.
    DelegateMember() = default;

    /// @brief Destructor ensures empty when destroyed.
    ~DelegateMember() override { Clear(); }

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The member function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    void Bind(SharedPtr object, MemberFunc func) {
        static_assert(!std::is_const<TClass>::value, "Cannot bind non-const function to const object.");
        m_object = object;
        m_func = func;
    }

    /// @brief Bind a const member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The function to bind to the delegate. The member function to 
    /// bind to the delegate. This function must match the signature of the delegate.
    void Bind(SharedPtr object, ConstMemberFunc func) {
        m_object = object;
        // Note: Casting ConstMemberFunc to MemberFunc is formally UB when invoked 
        // through the non-const pointer, but works safely on all mainstream ABIs.
        m_func = reinterpret_cast<MemberFunc>(func);
    }

    /// @brief Bind a member function to a raw pointer.
    /// @details Wraps the raw object pointer in a `std::shared_ptr` with a no-op 
    /// deleter, ensuring the delegate references the object without taking ownership 
    /// or attempting to delete it. The caller must ensure the object outlives the delegate.
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance (raw pointer).
    /// @param[in] func The const member function to bind.
    void Bind(ObjectPtr object, MemberFunc func) {
        static_assert(!std::is_const<TClass>::value, "Cannot bind non-const function to const object.");
        auto deleter = [](TClass*) {};                        // No-op deleter
        m_object = std::shared_ptr<TClass>(object, deleter, ::dmq::stl_allocator<std::remove_const_t<TClass>>());  // Not deleted when out of scope
        m_func = func;
    }

    /// @brief Bind a const member function to a raw pointer.
    /// @details Wraps the raw object pointer in a `std::shared_ptr` with a no-op
    /// deleter, ensuring the delegate references the object without taking ownership
    /// or attempting to delete it. The caller must ensure the object outlives the delegate.
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance (raw pointer).
    /// @param[in] func The const member function to bind.
    void Bind(ObjectPtr object, ConstMemberFunc func) {
        auto deleter = [](TClass*) {};                        // No-op deleter
        m_object = std::shared_ptr<TClass>(object, deleter, ::dmq::stl_allocator<std::remove_const_t<TClass>>());  // Not deleted when out of scope
        // Note: Casting ConstMemberFunc to MemberFunc is formally UB when invoked 
        // through the non-const pointer, but works safely on all mainstream ABIs.
        m_func = reinterpret_cast<MemberFunc>(func);
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

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_object = rhs.m_object;
        m_func = rhs.m_func;
    }

    /// @brief Invoke the bound delegate function synchronously. Always safe to call.
    /// @param[in] args - the function arguments, if any.
    /// @return The bound function return value, if any. If empty delegate
    /// default return type returned. 
    virtual RetType operator()(Args... args) override {
        if (Empty())
            return RetType();

        if constexpr (std::is_const<TClass>::value) 
            return std::invoke(reinterpret_cast<ConstMemberFunc>(m_func), m_object, std::forward<Args>(args)...);
        else
            return std::invoke(m_func, m_object, std::forward<Args>(args)...);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            m_object = rhs.m_object;
            m_func = rhs.m_func;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept {
        return Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        return derivedRhs &&
            m_func == derivedRhs->m_func &&
            m_object == derivedRhs->m_object;
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !Empty();
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

    /// @brief Check if the delegate is bound to a target function.
    /// @return `true` if the delegate has a target function, `false` otherwise.
    bool Empty() const noexcept { return !(m_object && m_func); }

    /// @brief Clear the target function.
    /// @post The delegate is empty.
    void Clear() noexcept { m_object = nullptr; m_func = nullptr; }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the object is not empty, `false` if the object is empty.
    explicit operator bool() const noexcept { return !Empty(); }

private:
    /// Compares two ClassType objects using the '<' operator.
    /// @param rhs The object to compare with.
    /// @return `true` if the current object's value is less than the other object's value,
    /// `false` otherwise.
    /// @note Do not call! Not allowed since comparing member function 
    /// pointers for operator< not allowed in C++.
    bool operator<(const ClassType& rhs) const = delete; 

    /// Pointer to a class object, representing the bound target instance.
    SharedPtr m_object = nullptr;

    /// Pointer to a member function, representing the bound target function.
    MemberFunc m_func = nullptr;
};

template <class C, class R>
class DelegateMemberSp; // Not defined

/// @brief `DelegateMemberSp<>` class synchronously invokes a class member target 
/// function using a weak pointer.
/// @details This class is a "safe" version of DelegateMember. It holds a std::weak_ptr 
/// to the target. If the target is destroyed, the callback is silently dropped.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class TClass, class RetType, class... Args>
class DelegateMemberSp<TClass, RetType(Args...)> : public Delegate<RetType(Args...)> {
public:
    typedef TClass* ObjectPtr;
    typedef std::shared_ptr<TClass> SharedPtr;
    typedef std::weak_ptr<TClass> WeakPtr; 
    typedef RetType(TClass::* MemberFunc)(Args...);
    typedef RetType(TClass::* ConstMemberFunc)(Args...) const;
    using ClassType = DelegateMemberSp<TClass, RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    DelegateMemberSp(SharedPtr object, MemberFunc func) { Bind(object, func); }

    /// @brief Constructor to create a class instance.
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    DelegateMemberSp(SharedPtr object, ConstMemberFunc func) { Bind(object, func); }

    /// @brief Copy constructor.
    DelegateMemberSp(const ClassType& rhs) { Assign(rhs); }

    /// @brief Move constructor.
    DelegateMemberSp(ClassType&& rhs) noexcept : m_object(std::move(rhs.m_object)), m_func(rhs.m_func) { rhs.Clear(); }

    /// @brief Default constructor.
    DelegateMemberSp() = default;

    /// @brief Destructor.
    ~DelegateMemberSp() override { Clear(); }

    /// @brief Bind a member function to the delegate.
    void Bind(SharedPtr object, MemberFunc func) {
        static_assert(!std::is_const<TClass>::value, "Cannot bind non-const function to const object.");
        m_object = object; // Implicit conversion from shared_ptr to weak_ptr
        m_func = func;
    }

    /// @brief Bind a const member function to the delegate.
    void Bind(SharedPtr object, ConstMemberFunc func) {
        m_object = object; // Implicit conversion from shared_ptr to weak_ptr
        // Note: Casting ConstMemberFunc to MemberFunc is formally UB when invoked 
        // through the non-const pointer, but works safely on all mainstream ABIs.
        m_func = reinterpret_cast<MemberFunc>(func);
    }

    /// @brief Creates a copy of the current object.
    virtual ClassType* Clone() const override {
        return new(std::nothrow) ClassType(*this);
    }

    /// @brief Assigns the state of one object to another.
    void Assign(const ClassType& rhs) {
        m_object = rhs.m_object;
        m_func = rhs.m_func;
    }

    /// @brief Invoke the bound delegate function synchronously.
    /// @details SAFELY locks the weak pointer. If object is dead, returns default.
    virtual RetType operator()(Args... args) override {
        // 1. Try to promote weak_ptr to strong shared_ptr
        if (auto strongPtr = m_object.lock())
        {
            // 2. SUCCESS: Object is alive. Invoke.
            if constexpr (std::is_const<TClass>::value)
                return std::invoke(reinterpret_cast<ConstMemberFunc>(m_func), strongPtr, std::forward<Args>(args)...);
            else
                return std::invoke(m_func, strongPtr, std::forward<Args>(args)...);
        }

        // 3. FAILURE: Object is dead. Silently ignore.
        return RetType();
    }

    /// @brief Assignment operator.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            m_object = std::move(rhs.m_object);
            m_func = rhs.m_func;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept {
        return Clear();
    }

    /// @brief Compares two delegate objects for equality.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        if (!derivedRhs) return false;

        // Compare Weak Pointers by locking them first
        auto ptr1 = m_object.lock();
        auto ptr2 = derivedRhs->m_object.lock();

        return (ptr1 == ptr2) && (m_func == derivedRhs->m_func);
    }

    // Standard operator== overloads
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }
    virtual bool operator==(std::nullptr_t) const noexcept override { return Empty(); }
    virtual bool operator!=(std::nullptr_t) const noexcept override { return !Empty(); }
    friend bool operator==(std::nullptr_t, const ClassType& rhs) noexcept { return rhs.Empty(); }
    friend bool operator!=(std::nullptr_t, const ClassType& rhs) noexcept { return !rhs.Empty(); }

    /// @brief Check if the delegate is bound and the object is alive.
    bool Empty() const noexcept { return m_object.expired() || !m_func; }

    /// @brief Clear the target function.
    void Clear() noexcept { m_object.reset(); m_func = nullptr; }

    explicit operator bool() const noexcept { return !Empty(); }

private:
    bool operator<(const ClassType& rhs) const = delete;

    /// Weak pointer to the target object.
    WeakPtr m_object; 

    /// Pointer to a member function.
    MemberFunc m_func = nullptr;
};

template <class R>
class DelegateFunction; // Not defined

/// @brief `DelegateFunction<>` class synchronously invokes a `std::function` target function.
/// @details Caution when binding to a `std::function` using this class. `std::function` cannot be 
/// compared for equality directly in a meaningful way using `operator==`. Therefore, the delegate
/// library uses `target_type()` for comparison. `target_type()` only compares the types of the 
/// stored callable objects, but the not actual instances. The code below shows the issue.
/// 
/// `Test t1, t2;`  
/// `std::function<void(int)> f1 = std::bind(&Test::Func, &t1, std::placeholders::_1);`  
/// `std::function<void(int)> f2 = std::bind(&Test::Func, &t2, std::placeholders::_1);`  
/// `MulticastDelegateSafe<void(int)> safe;`  
/// `safe += MakeDelegate(f1);`  
/// `safe += MakeDelegate(f2);`  
/// `safe -= MakeDelegate(f2);   // Should remove f2, not f1!`  
/// 
/// Depending on how usage, this may never be a issue but its worth noting. 
/// 
/// The other delegate classes has no such limitations and works under all conditions,
/// including comparing two instance functions of the same class. 
/// 
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFunction<RetType(Args...)> : public Delegate<RetType(Args...)> {
public:
    using FunctionType = std::function<RetType(Args...)>;
    using ClassType = DelegateFunction<RetType(Args...)>;

    /// @brief Constructor to create a class instance.
    /// @param[in] func The target `std::function` to store.
    DelegateFunction(FunctionType func) { Bind(func); }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFunction(const ClassType& rhs) { Assign(rhs); }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFunction(ClassType&& rhs) noexcept : m_func(rhs.m_func) { rhs.Clear(); }

    /// @brief Default constructor creates an empty delegate.
    DelegateFunction() = default;

    /// @brief Destructor ensures empty when destroyed.
    ~DelegateFunction() override { Clear(); }

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The `std::function` to bind to the delegate. This function must 
    /// match the signature of the delegate.
    void Bind(FunctionType func) {
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        // No exceptions: Direct assignment.
        // If the STL needs to allocate memory here and fails, 
        // it will likely abort() internally on embedded systems.
        m_func = func;
#else
        // Exceptions enabled: Safe to try-catch.
        try {
            m_func = func;
        }
        catch (const std::bad_alloc&) {
            BAD_ALLOC();
        }
#endif
    }

    /// Compares two ClassType objects using the '<' operator.
    /// @param rhs The object to compare with.
    /// @return `true` if the current object's value is less than the other object's value,
    /// `false` otherwise.
    bool operator<(const ClassType& rhs) const {
        return m_func.template target<FunctionType>() < rhs.m_func.template target<FunctionType>();
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

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_func = rhs.m_func;
    }

    /// @brief Invoke the bound delegate function synchronously. Always safe to call.
    /// @param[in] args - the function arguments, if any.
    /// @return The bound function return value, if any. If empty delegate
    /// default return type returned. 
    virtual RetType operator()(Args... args) override {
        if (Empty())
            return RetType();
        return m_func(std::forward<Args>(args)...);
    }

    /// @brief Assignment operator that assigns the state of one object to another.
    /// @param[in] rhs The object whose state is to be assigned to the current object.
    /// @return A reference to the current object.
    ClassType& operator=(const ClassType& rhs) {
        if (&rhs != this) {
            Assign(rhs);
        }
        return *this;
    }

    /// @brief Move assignment operator that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    /// @return A reference to the current object.
    ClassType& operator=(ClassType&& rhs) noexcept {
        if (&rhs != this) {
            m_func = rhs.m_func;
            rhs.Clear();
        }
        return *this;
    }

    /// @brief Clear the target function.
    virtual void operator=(std::nullptr_t) noexcept {
        return Clear();
    }

    /// @brief Compares two delegate objects for equality.
    /// @param[in] rhs The `DelegateBase` object to compare with the current object.
    /// @return `true` if the two delegate objects are equal, `false` otherwise.
    virtual bool Equal(const DelegateBase& rhs) const override {
        auto derivedRhs = dynamic_cast<const ClassType*>(&rhs);
        if (derivedRhs) {
            // If both delegates are empty, they are equal
            if (Empty() && derivedRhs->Empty())
                return true;

            if (m_func && derivedRhs->m_func)
                return m_func.target_type() == derivedRhs->m_func.target_type();

            return false;
        }

        return false;  // Return false if dynamic cast failed
    }

    /// Compares two delegate objects for equality.
    /// @return `true` if the objects are equal, `false` otherwise.
    bool operator==(const ClassType& rhs) const noexcept { return Equal(rhs); }

    /// Overload operator== to compare the delegate to nullptr
    /// @return `true` if delegate is null.
    virtual bool operator==(std::nullptr_t) const noexcept override {
        return Empty();
    }

    /// Overload operator!= to compare the delegate to nullptr
    /// @return `true` if delegate is not null.
    virtual bool operator!=(std::nullptr_t) const noexcept override {
        return !Empty();
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

    /// @brief Check if the delegate is bound to a target function.
    /// @return `true` if the delegate has a target function, `false` otherwise.
    bool Empty() const noexcept { return !m_func; }

    /// @brief Clear the target function.
    /// @post The delegate is empty.
    void Clear() noexcept { m_func = nullptr; }

    /// @brief Implicit conversion operator to `bool`.
    /// @return `true` if the object is not empty, `false` if the object is empty.
    explicit operator bool() const noexcept { return !Empty(); }

private:
    /// A std::function instance, representing the bound target function.
    FunctionType m_func;
};

/// @brief Creates a delegate that binds to a free function.
/// @tparam RetType The return type of the free function.
/// @tparam Args The types of the function arguments.
/// @param[in] func A pointer to the free function to bind to the delegate.
/// @return A `DelegateFree` object bound to the specified free function.
template <class RetType, class... Args>
auto MakeDelegate(RetType(*func)(Args... args)) {
    return DelegateFree<RetType(Args...)>(func);
}

/// @brief Creates a delegate that binds to a non-const member function.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @return A `DelegateMember` object bound to the specified non-const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::*func)(Args... args)) {
    return DelegateMember<TClass, RetType(Args...)>(object, func);
}

/// @brief Creates a delegate that binds to a const member function.
/// @tparam TClass The class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @return A `DelegateMember` object bound to the specified const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::*func)(Args... args) const) {
    return DelegateMember<TClass, RetType(Args...)>(object, func);
}

/// @brief Creates a delegate that binds to a const member function.
/// @tparam TClass The const class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @return A `DelegateMember` object bound to the specified non-const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(const TClass* object, RetType(TClass::* func)(Args... args) const) {
    return DelegateMember<const TClass, RetType(Args...)>(object, func);
}

/// @brief Creates a delegate that binds to a non-const member function with a shared pointer to the object.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @return A `DelegateMember` shared pointer bound to the specified non-const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetType(TClass::* func)(Args... args)) {
    return DelegateMemberSp<TClass, RetType(Args...)>(object, func);
}

/// @brief Creates a delegate that binds to a const member function with a shared pointer to the object.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @return A `DelegateMember` shared pointer bound to the specified const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetType(TClass::* func)(Args... args) const) {
    return DelegateMemberSp<TClass, RetType(Args...)>(object, func);
}

/// @brief Creates a delegate that binds to a `std::function`.
/// @tparam RetType The return type of the `std::function`.
/// @tparam Args The types of the function arguments.
/// @param[in] func The `std::function` to bind to the delegate.
/// @return A `DelegateFunction` object bound to the specified `std::function`.
template <class RetType, class... Args>
auto MakeDelegate(std::function<RetType(Args...)> func) {
    return DelegateFunction<RetType(Args...)>(func);
}

/// @brief Creates a delegate that binds to a raw lambda or functor.
/// @tparam F The lambda or functor type.
/// @param[in] func The lambda or functor to bind.
/// @return A `DelegateFunction` object bound to the specified lambda or functor.
template <typename F, typename = std::enable_if_t<trait::is_callable<F>::value>>
auto MakeDelegate(F&& func) {
    // Deduce the signature from the operator()
    using Sig = typename trait::function_traits<decltype(&std::remove_reference_t<F>::operator())>::function_type;
    return DelegateFunction<Sig>(std::forward<F>(func));
}

}

#endif
