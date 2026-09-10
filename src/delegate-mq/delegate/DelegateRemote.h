#ifndef _DELEGATE_REMOTE_H
#define _DELEGATE_REMOTE_H

// DelegateRemote.h
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, Aug 2025.

/// @file
/// @brief Delegate "`Remote`" series of classes used to invoke a function remotely 
/// (i.e. different CPU, different process, etc...). 
/// 
/// @details The classes are not thread safe. Invoking a function remotely requires serializing
/// all target function arguments and sending the argument data to the remote destination. All
/// argument data is serialized into a stream. The receiver calls `Invoke()` with the received
/// serialized stream arguments.
/// 
/// An `ISerializer` and `IDispatcher` implementations are required to serialize and dispatch a 
/// remote delegate. These interface implementations, relied upon by the remote delegate, might not 
/// be thread safe. Therefore, ensure each remote delegate instance is called from a single thread 
/// of control.
/// 
/// `RetType operator()(Args... args)` - called by the sender to initiate the remote function call. 
/// Use `SetErrorHandler()` to catch invoke errors. Clone() may throw `std::bad_alloc` unless 
/// `DMQ_ASSERTS`. All other delegate class functions do not throw exceptions.
/// 
/// `void Invoke(std::istream& is)` - called by the receiver to invoke the target function. 
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
/// * Cannot insert `DelegateRemoteAsync` into an ordered container. e.g. `std::list` ok, 
/// `std::set` not ok.
/// 
/// * `std::function` compares the function signature type, not the underlying object instance.
/// See `DelegateFunction<>` class for more info.
/// 
/// Code within `<common_code>` and `</common_code>` is updated using src_dup.py. Manually update 
/// the code within the `DelegateFreeRemote` `common_code` tags, then run the script to 
/// propagate to the remaining delegate classes to simplify code maintenance.
/// 
/// `python src_dup.py DelegateRemote.h`  

#include "Delegate.h"
#include "UnicastDelegate.h"
#include "ISerializer.h"
#include "IDispatcher.h"
#include "IInvoker.h"
#include <tuple>
#include "DelegateOpt.h"
#include <iostream>
#include <stdexcept>

DMQ_OPTIMIZE_ON

namespace dmq {

enum class DelegateError {
    SUCCESS = 0,
    ERR_STREAM_NOT_GOOD = 1,
    ERR_NO_SERIALIZER = 2,
    ERR_SERIALIZE = 3,
    ERR_DESERIALIZE = 4,
    ERR_DESERIALIZE_EXCEPTION = 5,
    ERR_NO_DISPATCHER = 6,
    ERR_DISPATCH = 7,
    ERR_TYPE_MISMATCH = 8
};

typedef int DelegateErrorAux;

// Get the type of the Nth position within a template parameter pack
template<size_t N, class... Args> using ArgTypeOf =
typename std::tuple_element<N, std::tuple<Args...>>::type;

// Get the value of the Nth position within a template parameter pack
template <size_t N, class... Args>
decltype(auto) ArgValueOf(Args&&... ts) {
    return std::get<N>(std::forward_as_tuple(ts...));
}

template <class Arg>
class RemoteArg
{
public:
    using NonConstArg = std::remove_const_t<Arg>;
    NonConstArg& Get() { return m_arg; }
private:
    NonConstArg m_arg{};
};

template <class Arg>
class RemoteArg<Arg*>
{
public:
    using NonConstArg = std::remove_const_t<Arg>;

    // Initialize the member pointer to point to the internal storage
    RemoteArg() : m_ptr(&m_arg) {}

    // Custom copy/move semantics to ensure m_ptr always points to the *local* m_arg
    RemoteArg(const RemoteArg& other) : m_arg(other.m_arg), m_ptr(&m_arg) {}
    RemoteArg& operator=(const RemoteArg& other) {
        if (this != &other) {
            m_arg = other.m_arg;
            m_ptr = &m_arg;
        }
        return *this;
    }

    RemoteArg(RemoteArg&& other) noexcept : m_arg(std::move(other.m_arg)), m_ptr(&m_arg) {}
    RemoteArg& operator=(RemoteArg&& other) noexcept {
        if (this != &other) {
            m_arg = std::move(other.m_arg);
            m_ptr = &m_arg;
        }
        return *this;
    }

    // Return a REFERENCE to the pointer (L-value), satisfying ISerializer::Read
    Arg*& Get() { return m_ptr; }

private:
    NonConstArg m_arg{};   // The actual object storage
    Arg* m_ptr;  // The persistent pointer variable
};

#if 0  // Arg** not supported on remote delegates
template <class Arg>
class RemoteArg<Arg**>
{
public:
    RemoteArg() : m_pArg(&m_arg), m_ppArg(&m_pArg) {}

    // Return reference to the pointer-to-pointer
    Arg**& Get() { return m_ppArg; }

private:
    Arg m_arg;
    Arg* m_pArg;
    Arg** m_ppArg;
};
#endif

template <class Arg>
class RemoteArg<Arg&>
{
public:
    using NonConstArg = std::remove_const_t<Arg>;
    NonConstArg& Get() { return m_arg; }
private:
    NonConstArg m_arg{};
};

namespace detail {

/// @brief Fully non-template holder for a `DelegateXRemote<...>` instance's dispatch
/// state — remote id, dispatcher, output stream, last error, last dispatched sequence
/// number, and error-handler delegate. None of this depends on the bound function's
/// signature or target class (the error handler's own signature is fixed, independent
/// of the owning delegate's), so every `DelegateFreeRemote`/`DelegateMemberRemote`/
/// `DelegateFunctionRemote` instantiation — for every signature and target class in the
/// program — composes this one definition instead of each generating its own copy.
/// @note `m_serializer` deliberately stays outside this class, on each owning delegate:
/// `ISerializer<RetType(Args...)>*` is the one piece of remote-dispatch state that
/// genuinely varies by signature.
/// @note `m_error` and `m_lastSeqNum` are per-instance, transient run-time observations
/// (the last error *this* object raised; the last sequence number *this* object's most
/// recent send was assigned) — never copied, assigned, or moved between instances,
/// only ever reset to their defaults on a fresh object. The special members below
/// deliberately leave them alone, mirroring the pre-refactor Assign()/copy-ctor/
/// move-ctor/move-assign, none of which touched them either.
class RemoteDispatchState {
public:
    RemoteDispatchState() = default;

    RemoteDispatchState(const RemoteDispatchState& rhs)
        : m_id(rhs.m_id), m_errorHandler(rhs.m_errorHandler),
          m_dispatcher(rhs.m_dispatcher), m_stream(rhs.m_stream) {
    }

    RemoteDispatchState(RemoteDispatchState&& rhs) noexcept
        : m_id(rhs.m_id), m_errorHandler(std::move(rhs.m_errorHandler)),
          m_dispatcher(rhs.m_dispatcher), m_stream(rhs.m_stream) {
        rhs.m_dispatcher = nullptr;
        rhs.m_stream = nullptr;
    }

    RemoteDispatchState& operator=(const RemoteDispatchState& rhs) {
        if (this != &rhs) {
            m_id = rhs.m_id;
            m_errorHandler = rhs.m_errorHandler;
            m_dispatcher = rhs.m_dispatcher;
            m_stream = rhs.m_stream;
        }
        return *this;
    }

    RemoteDispatchState& operator=(RemoteDispatchState&& rhs) noexcept {
        if (this != &rhs) {
            m_id = rhs.m_id;
            m_errorHandler = std::move(rhs.m_errorHandler);
            m_dispatcher = rhs.m_dispatcher;
            m_stream = rhs.m_stream;
            rhs.m_dispatcher = nullptr;
            rhs.m_stream = nullptr;
        }
        return *this;
    }

    DelegateRemoteId GetRemoteId() const noexcept { return m_id; }
    void SetRemoteId(DelegateRemoteId id) noexcept { m_id = id; }

    IDispatcher* GetDispatcher() const noexcept { return m_dispatcher; }
    void SetDispatcher(IDispatcher* dispatcher) noexcept { m_dispatcher = dispatcher; }

    dmq::xostringstream* GetStream() const noexcept { return m_stream; }
    void SetStream(dmq::xostringstream* stream) noexcept { m_stream = stream; }

    uint16_t GetLastSeqNum() const noexcept { return m_lastSeqNum; }

    void SetErrorHandler(const Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>& errorHandler) {
        m_errorHandler = errorHandler;  // Copy
    }
    void SetErrorHandler(Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>&& errorHandler) {
        m_errorHandler = std::move(errorHandler);  // Moving the temporary
    }
    void ClearErrorHandler() {
        m_errorHandler.Clear();
    }

    /// @brief Get the last error code.
    /// @return The last error detected.
    /// @post Error is reset to SUCCESS after call.
    DelegateError GetError() noexcept {
        DelegateError retVal = m_error;
        m_error = DelegateError::SUCCESS;
        return retVal;
    }

    /// Raise an error and callback the registered error handler.
    /// @param[in] error Error code.
    /// @param[in] auxCode Optional auxiliary code.
    /// @throws std::runtime_error If no error handler is registered.
    void RaiseError(DelegateError error, DelegateErrorAux auxCode = 0) {
        m_error = error;
        if (m_errorHandler) {
            m_errorHandler(m_id, error, auxCode);
        }
        else {
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
            // No throw
#else
            throw std::runtime_error("Delegate remote error " + std::to_string(static_cast<int>(error)) + " id " + std::to_string(m_id));
#endif
        }
    }

    /// Raise success and callback the registered error handler.
    void RaiseSuccess() {
        if (m_errorHandler)
            m_errorHandler(m_id, dmq::DelegateError::SUCCESS, 0);
    }

    /// @brief Check the stream, dispatch it to the remote, and report success/error
    /// through the error handler.
    /// @details Everything past the argument serialization step is identical
    /// regardless of the delegate's signature or target class, so it lives here once
    /// instead of being duplicated in every `DelegateXRemote<...>::operator()`.
    /// @param[in] writeFailed `true` if the caller's `ISerializer::Write()` call threw
    /// (already reported through `RaiseError(ERR_SERIALIZE)` by the caller).
    void SendSerialized(bool writeFailed) {
        if (writeFailed)
            return;

        if (!m_stream || !m_stream->good()) {
            RaiseError(DelegateError::ERR_STREAM_NOT_GOOD);
            return;
        }

        if (!m_dispatcher) {
            RaiseError(DelegateError::ERR_NO_DISPATCHER);
            return;
        }

        // Dispatch delegate invocation to the remote destination
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        int error = m_dispatcher->Dispatch(*m_stream, m_id, &m_lastSeqNum);
        if (error)
            RaiseError(DelegateError::ERR_DISPATCH, error);
        else
            RaiseSuccess();
#else
        try {
            int error = m_dispatcher->Dispatch(*m_stream, m_id, &m_lastSeqNum);
            if (error)
                RaiseError(DelegateError::ERR_DISPATCH, error);
            else
                RaiseSuccess();
        }
        catch (std::exception&) {
            RaiseError(DelegateError::ERR_DISPATCH);
        }
#endif
    }

    bool Equal(const RemoteDispatchState& rhs) const noexcept {
        return m_id == rhs.m_id;
    }

private:
    DelegateRemoteId m_id = INVALID_REMOTE_ID;
    mutable uint16_t m_lastSeqNum = 0;
    UnicastDelegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)> m_errorHandler;
    IDispatcher* m_dispatcher = nullptr;
    DelegateError m_error = DelegateError::SUCCESS;
    dmq::xostringstream* m_stream = nullptr;
};

} // namespace detail

template <class R>
class DelegateFreeRemote; // Not defined

/// @brief `DelegateFreeRemote<>` class asynchronously invokes a free target function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFreeRemote<RetType(Args...)> : public DelegateFree<RetType(Args...)>, public IRemoteInvoker {
public:
    typedef std::integral_constant<std::size_t, sizeof...(Args)> ArgCnt;
    typedef RetType(*FreeFunc)(Args...);
    using ClassType = DelegateFreeRemote<RetType(Args...)>;
    using BaseType = DelegateFree<RetType(Args...)>;

    static_assert(!(std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
        "std::shared_ptr arguments must be passed by value on remote delegates; reference and pointer forms are not allowed");
    static_assert(!(std::disjunction_v<trait::is_double_pointer<Args>...>),
        "Double pointer arguments (Arg**) are not allowed on remote delegates");
    using BaseType::operator=;

    /// @brief Constructor to create a class instance. Typically called by sender. 
    /// @param[in] id The remote delegate identifier.
    DelegateFreeRemote(DelegateRemoteId id) { m_state.SetRemoteId(id); }

    /// @brief Constructor to create a class instance. Typically called by receiver.
    /// @param[in] func The target free function to store.
    /// @param[in] id The remote delegate identifier.
    DelegateFreeRemote(FreeFunc func, DelegateRemoteId id) :
        BaseType(func) { 
        Bind(func, id); 
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFreeRemote(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFreeRemote(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_state(std::move(rhs.m_state)), m_serializer(rhs.m_serializer) {
        rhs.Clear();
        rhs.m_serializer = nullptr;
    }

    DelegateFreeRemote() = default;

    /// @brief Bind a free function to the delegate.
    /// @details This method associates a free function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The free function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] id The remote delegate identifier.
    void Bind(FreeFunc func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_state = rhs.m_state;
        m_serializer = rhs.m_serializer;
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
            m_state = std::move(rhs.m_state);    // Use the resource
            m_serializer = rhs.m_serializer;
            rhs.m_serializer = nullptr;
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
            m_state.Equal(derivedRhs->m_state) &&
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

    /// @brief Invoke the bound delegate function on the remote. Called by the sender.
    /// @details Invoke remote delegate function asynchronously and do not wait for the 
    /// return value. This function is called by the sender. Dispatches the delegate data to 
    /// the destination remote receiver. `Invoke()` must be called by the destination 
    /// remote receiver to invoke the target function. Always safe to call.
    /// 
    /// All argument data is serialized into a binary byte stream. The stream of bytes is 
    /// sent to the receiver. The receivder deserializes the arguments and invokes the remote
    /// target function. 
    /// 
    /// All user-defined argument data must inherit from ISerializer and implement the 
    /// `read()` and `write()` functions to serialize each data member. 
    /// 
    /// Does not throw exceptions. However, the platform-specific `ISerializer` implementation 
    /// might. Register for error callbacks using `SetErrorHandler()`.
    /// 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    virtual RetType operator()(Args... args) override {
        if (m_serializer && m_state.GetStream()) {
            // Only true once the whole send succeeds (write + stream check + dispatch).
            // RaiseSuccess() is deferred to that single point so a write/dispatch failure
            // is never preceded by a spurious success callback, and a failure is never
            // reported more than once for the same call.
            bool writeFailed = false;
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
            // Serialize all target function arguments into a stream
            m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
#else
            try {
                // Serialize all target function arguments into a stream
                m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
            }
            catch (std::exception&) {
                writeFailed = true;
                m_state.RaiseError(DelegateError::ERR_SERIALIZE);
            }
#endif
            // Stream-good check, dispatch, and error/success reporting are identical
            // regardless of signature or target class - see RemoteDispatchState::SendSerialized().
            m_state.SendSerialized(writeFailed);
        }
        else {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
        }

        // Do not wait for remote to invoke function call
        return RetType();
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the remote sender. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination receiver. Called by the 
    /// remote destination. The sender serializes all target function arguments. This
    /// function unserializes the argument data and invokes the remote target function.
    /// @details Each source sender call to `operator()` generate a call to `Invoke()` 
    /// on the destination receiver. 
    /// @param[in] is The delegate argument stream created and sent within 
    /// `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::istream& is) override {
        if (!m_serializer) {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
            return false;
        }

        if (!is.good()) {
            m_state.RaiseError(DelegateError::ERR_STREAM_NOT_GOOD);
            return false;
        }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        if constexpr (ArgCnt::value == 0) {
            BaseType::operator()();
        }
        else {
            // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
            std::tuple<RemoteArg<Args>...> remoteArgs;

            // 2. Use std::apply to unpack the tuple elements
            std::apply([this, &is](auto&... rArgs) {

                // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                // rArgs.Get() returns the reference/pointer to the internal storage
                m_serializer->Read(is, rArgs.Get()...);

                if (!is.bad() && !is.fail()) {
                    // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                    this->BaseType::operator()(rArgs.Get()...);
                }
                else {
                    this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                }

                }, remoteArgs);
        }
#else
        try {
            if constexpr (ArgCnt::value == 0) {
                BaseType::operator()();
            }
            else {
                // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
                std::tuple<RemoteArg<Args>...> remoteArgs;

                // 2. Use std::apply to unpack the tuple elements
                std::apply([this, &is](auto&... rArgs) {

                    // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                    // rArgs.Get() returns the reference/pointer to the internal storage
                    m_serializer->Read(is, rArgs.Get()...);

                    if (!is.bad() && !is.fail()) {
                        // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                        this->BaseType::operator()(rArgs.Get()...);
                    }
                    else {
                        this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                    }

                    }, remoteArgs);
            }
        }
        catch (std::exception&) {
            m_state.RaiseError(DelegateError::ERR_DESERIALIZE_EXCEPTION);
        }
#endif

        return true;
    }

    ///@brief Get the remote identifier.
    // @return The remote identifier.
    DelegateRemoteId GetRemoteId() noexcept { return m_state.GetRemoteId(); }

    ///@brief Set the remote identifier.
    // @param[in] id The remote identifier.
    void SetRemoteId(DelegateRemoteId id) noexcept { m_state.SetRemoteId(id); }

    /// @brief Set the dispatcher instance used to send to remote
    /// @param[in] dispatcher A dispatcher instance
    void SetDispatcher(IDispatcher* dispatcher) {
        m_state.SetDispatcher(dispatcher);
    }

    /// @brief Set the serializer instance used to serialize/deserialize
    /// function arguments.
    /// @param[in] serializer A serializer instance
    void SetSerializer(ISerializer<RetType(Args...)>* serializer) {
        m_serializer = serializer;
    }

    /// @brief Set the serialization stream used to store serialized function
    /// argument data.
    /// @param[in] stream An output stream.
    void SetStream(dmq::xostringstream* stream) {
        m_state.SetStream(stream);
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(const Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>& errorHandler) {
        m_state.SetErrorHandler(errorHandler);  // Copy
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>&& errorHandler) {
        m_state.SetErrorHandler(std::move(errorHandler));  // Moving the temporary
    }

    /// @brief Clear the error handler
    void ClearErrorHandler() {
        m_state.ClearErrorHandler();
    }

    /// @brief Get the last error code
    /// @return The last error detected
    /// @post Error is reset to SUCCESS after call
    DelegateError GetError() {
        return m_state.GetError();
    }

    /// @brief Get the sequence number assigned by the dispatcher to the most
    /// recent successful dispatch.
    /// @return The last dispatched sequence number, or 0 if nothing has been
    /// dispatched yet.
    uint16_t GetLastSeqNum() const noexcept { return m_state.GetLastSeqNum(); }

private:
    /// Destination remote id, dispatcher, output stream, last error, last sequence
    /// number, and error handler. Not templated on Sig/TClass — see
    /// `detail::RemoteDispatchState`.
    detail::RemoteDispatchState m_state;

    /// A pointer to the function argument serializer. Kept outside `m_state` since,
    /// unlike everything else there, its pointee type genuinely varies by signature.
    ISerializer<RetType(Args...)>* m_serializer = nullptr;

    // </common_code>
};

template <class C, class R>
class DelegateMemberRemote; // Not defined

/// @brief `DelegateMemberRemote<>` class asynchronously invokes a class member target function.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class TClass, class RetType, class... Args>
class DelegateMemberRemote<TClass, RetType(Args...)> : public DelegateMember<TClass, RetType(Args...)>, public IRemoteInvoker {
public:
    typedef std::integral_constant<std::size_t, sizeof...(Args)> ArgCnt;
    typedef TClass* ObjectPtr;
    typedef std::shared_ptr<TClass> SharedPtr;
    typedef RetType(TClass::* MemberFunc)(Args...);
    typedef RetType(TClass::* ConstMemberFunc)(Args...) const;
    using ClassType = DelegateMemberRemote<TClass, RetType(Args...)>;
    using BaseType = DelegateMember<TClass, RetType(Args...)>;

    static_assert(!(std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
        "std::shared_ptr arguments must be passed by value on remote delegates; reference and pointer forms are not allowed");
    static_assert(!(std::disjunction_v<trait::is_double_pointer<Args>...>),
        "Double pointer arguments (Arg**) are not allowed on remote delegates");
    using BaseType::operator=;

    /// @brief Constructor to create a class instance. Typically called by sender. 
    /// @param[in] id The remote delegate identifier.
    DelegateMemberRemote(DelegateRemoteId id) { m_state.SetRemoteId(id); }

    /// @brief Constructor to create a class instance. Typically called by receiver. 
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    /// @param[in] id The delegate remote identifier.
    DelegateMemberRemote(SharedPtr object, MemberFunc func, DelegateRemoteId id) : BaseType(object, func) {
        Bind(object, func, id);
    }

    /// @brief Constructor to create a class instance. Typically called by receiver. 
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    /// @param[in] id The delegate remote identifier.
    DelegateMemberRemote(SharedPtr object, ConstMemberFunc func, DelegateRemoteId id) : BaseType(object, func) {
        Bind(object, func, id);
    }

    /// @brief Constructor to create a class instance. Typically called by receiver. 
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target member function to store.
    /// @param[in] id The delegate remote identifier.
    DelegateMemberRemote(ObjectPtr object, MemberFunc func, DelegateRemoteId id) : BaseType(object, func) {
        Bind(object, func, id);
    }

    /// @brief Constructor to create a class instance. Typically called by receiver. 
    /// @param[in] object The target object pointer to store.
    /// @param[in] func The target const member function to store.
    /// @param[in] id The delegate remote identifier.
    DelegateMemberRemote(ObjectPtr object, ConstMemberFunc func, DelegateRemoteId id) : BaseType(object, func) {
        Bind(object, func, id);
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateMemberRemote(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateMemberRemote(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_state(std::move(rhs.m_state)), m_serializer(rhs.m_serializer) {
        rhs.Clear();
        rhs.m_serializer = nullptr;
    }

    DelegateMemberRemote() = default;

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The function to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] id The delegate remote identifier.
    void Bind(SharedPtr object, MemberFunc func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(object, func);
    }

    /// @brief Bind a const member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The member function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] id The delegate remote identifier.
    void Bind(SharedPtr object, ConstMemberFunc func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(object, func);
    }

    /// @brief Bind a member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The function to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] id The delegate remote identifier.
    void Bind(ObjectPtr object, MemberFunc func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(object, func);
    }

    /// @brief Bind a const member function to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] object The target object instance.
    /// @param[in] func The member function to bind to the delegate. This function must 
    /// match the signature of the delegate.
    /// @param[in] id The delegate remote identifier.
    void Bind(ObjectPtr object, ConstMemberFunc func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(object, func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_state = rhs.m_state;
        m_serializer = rhs.m_serializer;
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
            m_state = std::move(rhs.m_state);    // Use the resource
            m_serializer = rhs.m_serializer;
            rhs.m_serializer = nullptr;
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
            m_state.Equal(derivedRhs->m_state) &&
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

    /// @brief Invoke the bound delegate function on the remote. Called by the sender.
    /// @details Invoke remote delegate function asynchronously and do not wait for the 
    /// return value. This function is called by the sender. Dispatches the delegate data to 
    /// the destination remote receiver. `Invoke()` must be called by the destination 
    /// remote receiver to invoke the target function. Always safe to call.
    /// 
    /// All argument data is serialized into a binary byte stream. The stream of bytes is 
    /// sent to the receiver. The receivder deserializes the arguments and invokes the remote
    /// target function. 
    /// 
    /// All user-defined argument data must inherit from ISerializer and implement the 
    /// `read()` and `write()` functions to serialize each data member. 
    /// 
    /// Does not throw exceptions. However, the platform-specific `ISerializer` implementation 
    /// might. Register for error callbacks using `SetErrorHandler()`.
    /// 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    virtual RetType operator()(Args... args) override {
        if (m_serializer && m_state.GetStream()) {
            // Only true once the whole send succeeds (write + stream check + dispatch).
            // RaiseSuccess() is deferred to that single point so a write/dispatch failure
            // is never preceded by a spurious success callback, and a failure is never
            // reported more than once for the same call.
            bool writeFailed = false;
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
            // Serialize all target function arguments into a stream
            m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
#else
            try {
                // Serialize all target function arguments into a stream
                m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
            }
            catch (std::exception&) {
                writeFailed = true;
                m_state.RaiseError(DelegateError::ERR_SERIALIZE);
            }
#endif
            // Stream-good check, dispatch, and error/success reporting are identical
            // regardless of signature or target class - see RemoteDispatchState::SendSerialized().
            m_state.SendSerialized(writeFailed);
        }
        else {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
        }

        // Do not wait for remote to invoke function call
        return RetType();
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the remote sender. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination receiver. Called by the 
    /// remote destination. The sender serializes all target function arguments. This
    /// function unserializes the argument data and invokes the remote target function.
    /// @details Each source sender call to `operator()` generate a call to `Invoke()` 
    /// on the destination receiver. 
    /// @param[in] is The delegate argument stream created and sent within 
    /// `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::istream& is) override {
        if (!m_serializer) {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
            return false;
        }

        if (!is.good()) {
            m_state.RaiseError(DelegateError::ERR_STREAM_NOT_GOOD);
            return false;
        }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        if constexpr (ArgCnt::value == 0) {
            BaseType::operator()();
        }
        else {
            // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
            std::tuple<RemoteArg<Args>...> remoteArgs;

            // 2. Use std::apply to unpack the tuple elements
            std::apply([this, &is](auto&... rArgs) {

                // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                // rArgs.Get() returns the reference/pointer to the internal storage
                m_serializer->Read(is, rArgs.Get()...);

                if (!is.bad() && !is.fail()) {
                    // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                    this->BaseType::operator()(rArgs.Get()...);
                }
                else {
                    this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                }

                }, remoteArgs);
        }
#else
        try {
            if constexpr (ArgCnt::value == 0) {
                BaseType::operator()();
            }
            else {
                // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
                std::tuple<RemoteArg<Args>...> remoteArgs;

                // 2. Use std::apply to unpack the tuple elements
                std::apply([this, &is](auto&... rArgs) {

                    // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                    // rArgs.Get() returns the reference/pointer to the internal storage
                    m_serializer->Read(is, rArgs.Get()...);

                    if (!is.bad() && !is.fail()) {
                        // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                        this->BaseType::operator()(rArgs.Get()...);
                    }
                    else {
                        this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                    }

                    }, remoteArgs);
            }
        }
        catch (std::exception&) {
            m_state.RaiseError(DelegateError::ERR_DESERIALIZE_EXCEPTION);
        }
#endif

        return true;
    }

    ///@brief Get the remote identifier.
    // @return The remote identifier.
    DelegateRemoteId GetRemoteId() noexcept { return m_state.GetRemoteId(); }

    ///@brief Set the remote identifier.
    // @param[in] id The remote identifier.
    void SetRemoteId(DelegateRemoteId id) noexcept { m_state.SetRemoteId(id); }

    /// @brief Set the dispatcher instance used to send to remote
    /// @param[in] dispatcher A dispatcher instance
    void SetDispatcher(IDispatcher* dispatcher) {
        m_state.SetDispatcher(dispatcher);
    }

    /// @brief Set the serializer instance used to serialize/deserialize
    /// function arguments.
    /// @param[in] serializer A serializer instance
    void SetSerializer(ISerializer<RetType(Args...)>* serializer) {
        m_serializer = serializer;
    }

    /// @brief Set the serialization stream used to store serialized function
    /// argument data.
    /// @param[in] stream An output stream.
    void SetStream(dmq::xostringstream* stream) {
        m_state.SetStream(stream);
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(const Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>& errorHandler) {
        m_state.SetErrorHandler(errorHandler);  // Copy
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>&& errorHandler) {
        m_state.SetErrorHandler(std::move(errorHandler));  // Moving the temporary
    }

    /// @brief Clear the error handler
    void ClearErrorHandler() {
        m_state.ClearErrorHandler();
    }

    /// @brief Get the last error code
    /// @return The last error detected
    /// @post Error is reset to SUCCESS after call
    DelegateError GetError() {
        return m_state.GetError();
    }

    /// @brief Get the sequence number assigned by the dispatcher to the most
    /// recent successful dispatch.
    /// @return The last dispatched sequence number, or 0 if nothing has been
    /// dispatched yet.
    uint16_t GetLastSeqNum() const noexcept { return m_state.GetLastSeqNum(); }

private:
    /// Destination remote id, dispatcher, output stream, last error, last sequence
    /// number, and error handler. Not templated on Sig/TClass — see
    /// `detail::RemoteDispatchState`.
    detail::RemoteDispatchState m_state;

    /// A pointer to the function argument serializer. Kept outside `m_state` since,
    /// unlike everything else there, its pointee type genuinely varies by signature.
    ISerializer<RetType(Args...)>* m_serializer = nullptr;

    // </common_code>
};

template <class R>
class DelegateFunctionRemote; // Not defined

/// @brief `DelegateFunctionRemote<>` class asynchronously invokes a `std::function` target function.
/// @details Caution when binding to a `std::function` using this class. `std::function` cannot be 
/// compared for equality directly in a meaningful way using `operator==`.  
/// 
/// See `DelegateFunction<>` base class for important usage limitations.
/// 
/// @tparam RetType The return type of the bound delegate function.
/// @tparam Args The argument types of the bound delegate function.
template <class RetType, class... Args>
class DelegateFunctionRemote<RetType(Args...)> : public DelegateFunction<RetType(Args...)>, public IRemoteInvoker {
public:
    typedef std::integral_constant<std::size_t, sizeof...(Args)> ArgCnt;
    using FunctionType = std::function<RetType(Args...)>;
    using ClassType = DelegateFunctionRemote<RetType(Args...)>;
    using BaseType = DelegateFunction<RetType(Args...)>;

    static_assert(!(std::disjunction_v<trait::is_shared_ptr_reference<Args>...>),
        "std::shared_ptr arguments must be passed by value on remote delegates; reference and pointer forms are not allowed");
    static_assert(!(std::disjunction_v<trait::is_double_pointer<Args>...>),
        "Double pointer arguments (Arg**) are not allowed on remote delegates");
    using BaseType::operator=;

    /// @brief Constructor to create a class instance. Typically called by sender. 
    /// @param[in] id The remote delegate identifier.
    DelegateFunctionRemote(DelegateRemoteId id) { m_state.SetRemoteId(id); }

    /// @brief Constructor to create a class instance. Typically called by receiver.
    /// @param[in] func The target `std::function` to store.
    /// @param[in] id The unique remote delegate identifier.
    DelegateFunctionRemote(FunctionType func, DelegateRemoteId id) :
        BaseType(func) {
        Bind(func, id);
    }

    /// @brief Copy constructor that creates a copy of the given instance.
    /// @details This constructor initializes a new object as a copy of the 
    /// provided `rhs` (right-hand side) object. The `rhs` object is used to 
    /// set the state of the new instance.
    /// @param[in] rhs The object to copy from.
    DelegateFunctionRemote(const ClassType& rhs) :
        BaseType(rhs) {
        Assign(rhs);
    }

    /// @brief Move constructor that transfers ownership of resources.
    /// @param[in] rhs The object to move from.
    DelegateFunctionRemote(ClassType&& rhs) noexcept :
        BaseType(std::move(rhs)), m_state(std::move(rhs.m_state)), m_serializer(rhs.m_serializer) {
        rhs.Clear();
        rhs.m_serializer = nullptr;
    }

    DelegateFunctionRemote() = default;

    /// @brief Bind a `std::function` to the delegate.
    /// @details This method associates a member function (`func`) with the delegate. 
    /// Once the function is bound, the delegate can be used to invoke the function.
    /// @param[in] func The `std::function` to bind to the delegate. This function must match 
    /// the signature of the delegate.
    /// @param[in] id The delegate remote identifier.
    void Bind(FunctionType func, DelegateRemoteId id) {
        m_state.SetRemoteId(id);
        BaseType::Bind(func);
    }

    // <common_code>

    /// @brief Assigns the state of one object to another.
    /// @details Copy the state from the `rhs` (right-hand side) object to the
    /// current object.
    /// @param[in] rhs The object whose state is to be copied.
    void Assign(const ClassType& rhs) {
        m_state = rhs.m_state;
        m_serializer = rhs.m_serializer;
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
            m_state = std::move(rhs.m_state);    // Use the resource
            m_serializer = rhs.m_serializer;
            rhs.m_serializer = nullptr;
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
            m_state.Equal(derivedRhs->m_state) &&
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

    /// @brief Invoke the bound delegate function on the remote. Called by the sender.
    /// @details Invoke remote delegate function asynchronously and do not wait for the 
    /// return value. This function is called by the sender. Dispatches the delegate data to 
    /// the destination remote receiver. `Invoke()` must be called by the destination 
    /// remote receiver to invoke the target function. Always safe to call.
    /// 
    /// All argument data is serialized into a binary byte stream. The stream of bytes is 
    /// sent to the receiver. The receivder deserializes the arguments and invokes the remote
    /// target function. 
    /// 
    /// All user-defined argument data must inherit from ISerializer and implement the 
    /// `read()` and `write()` functions to serialize each data member. 
    /// 
    /// Does not throw exceptions. However, the platform-specific `ISerializer` implementation 
    /// might. Register for error callbacks using `SetErrorHandler()`.
    /// 
    /// @param[in] args The function arguments, if any.
    /// @return A default return value. The return value is *not* returned from the 
    /// target function. Do not use the return value.
    /// @post Do not use the return value as its not valid.
    virtual RetType operator()(Args... args) override {
        if (m_serializer && m_state.GetStream()) {
            // Only true once the whole send succeeds (write + stream check + dispatch).
            // RaiseSuccess() is deferred to that single point so a write/dispatch failure
            // is never preceded by a spurious success callback, and a failure is never
            // reported more than once for the same call.
            bool writeFailed = false;
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
            // Serialize all target function arguments into a stream
            m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
#else
            try {
                // Serialize all target function arguments into a stream
                m_serializer->Write(*m_state.GetStream(), std::forward<Args>(args)...);
            }
            catch (std::exception&) {
                writeFailed = true;
                m_state.RaiseError(DelegateError::ERR_SERIALIZE);
            }
#endif
            // Stream-good check, dispatch, and error/success reporting are identical
            // regardless of signature or target class - see RemoteDispatchState::SendSerialized().
            m_state.SendSerialized(writeFailed);
        }
        else {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
        }

        // Do not wait for remote to invoke function call
        return RetType();
    }

    /// @brief Invoke delegate function asynchronously. Do not wait for return value.
    /// Called by the remote sender. Always safe to call.
    /// @param[in] args The function arguments, if any.
    void AsyncInvoke(Args... args) {
        operator()(std::forward<Args>(args)...);
    }

    /// @brief Invoke the delegate function on the destination receiver. Called by the 
    /// remote destination. The sender serializes all target function arguments. This
    /// function unserializes the argument data and invokes the remote target function.
    /// @details Each source sender call to `operator()` generate a call to `Invoke()` 
    /// on the destination receiver. 
    /// @param[in] is The delegate argument stream created and sent within 
    /// `operator()(Args... args)`.
    /// @return `true` if target function invoked; `false` if error. 
    virtual bool Invoke(std::istream& is) override {
        if (!m_serializer) {
            m_state.RaiseError(DelegateError::ERR_NO_SERIALIZER);
            return false;
        }

        if (!is.good()) {
            m_state.RaiseError(DelegateError::ERR_STREAM_NOT_GOOD);
            return false;
        }

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        if constexpr (ArgCnt::value == 0) {
            BaseType::operator()();
        }
        else {
            // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
            std::tuple<RemoteArg<Args>...> remoteArgs;

            // 2. Use std::apply to unpack the tuple elements
            std::apply([this, &is](auto&... rArgs) {

                // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                // rArgs.Get() returns the reference/pointer to the internal storage
                m_serializer->Read(is, rArgs.Get()...);

                if (!is.bad() && !is.fail()) {
                    // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                    this->BaseType::operator()(rArgs.Get()...);
                }
                else {
                    this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                }

                }, remoteArgs);
        }
#else
        try {
            if constexpr (ArgCnt::value == 0) {
                BaseType::operator()();
            }
            else {
                // 1. Create a tuple of RemoteArg<T> to hold the temporary storage
                std::tuple<RemoteArg<Args>...> remoteArgs;

                // 2. Use std::apply to unpack the tuple elements
                std::apply([this, &is](auto&... rArgs) {

                    // 3. Deserialize: Expand the pack to call Read(is, arg1, arg2...)
                    // rArgs.Get() returns the reference/pointer to the internal storage
                    m_serializer->Read(is, rArgs.Get()...);

                    if (!is.bad() && !is.fail()) {
                        // 4. Invoke: Expand the pack to call operator()(arg1, arg2...)
                        this->BaseType::operator()(rArgs.Get()...);
                    }
                    else {
                        this->m_state.RaiseError(DelegateError::ERR_DESERIALIZE);
                    }

                    }, remoteArgs);
            }
        }
        catch (std::exception&) {
            m_state.RaiseError(DelegateError::ERR_DESERIALIZE_EXCEPTION);
        }
#endif

        return true;
    }

    ///@brief Get the remote identifier.
    // @return The remote identifier.
    DelegateRemoteId GetRemoteId() noexcept { return m_state.GetRemoteId(); }

    ///@brief Set the remote identifier.
    // @param[in] id The remote identifier.
    void SetRemoteId(DelegateRemoteId id) noexcept { m_state.SetRemoteId(id); }

    /// @brief Set the dispatcher instance used to send to remote
    /// @param[in] dispatcher A dispatcher instance
    void SetDispatcher(IDispatcher* dispatcher) {
        m_state.SetDispatcher(dispatcher);
    }

    /// @brief Set the serializer instance used to serialize/deserialize
    /// function arguments.
    /// @param[in] serializer A serializer instance
    void SetSerializer(ISerializer<RetType(Args...)>* serializer) {
        m_serializer = serializer;
    }

    /// @brief Set the serialization stream used to store serialized function
    /// argument data.
    /// @param[in] stream An output stream.
    void SetStream(dmq::xostringstream* stream) {
        m_state.SetStream(stream);
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(const Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>& errorHandler) {
        m_state.SetErrorHandler(errorHandler);  // Copy
    }

    /// @brief Set the error handler
    /// @param[in] errorHandler The delegate error handler called when
    /// an error is detected.
    void SetErrorHandler(Delegate<void(DelegateRemoteId, DelegateError, DelegateErrorAux)>&& errorHandler) {
        m_state.SetErrorHandler(std::move(errorHandler));  // Moving the temporary
    }

    /// @brief Clear the error handler
    void ClearErrorHandler() {
        m_state.ClearErrorHandler();
    }

    /// @brief Get the last error code
    /// @return The last error detected
    /// @post Error is reset to SUCCESS after call
    DelegateError GetError() {
        return m_state.GetError();
    }

    /// @brief Get the sequence number assigned by the dispatcher to the most
    /// recent successful dispatch.
    /// @return The last dispatched sequence number, or 0 if nothing has been
    /// dispatched yet.
    uint16_t GetLastSeqNum() const noexcept { return m_state.GetLastSeqNum(); }

private:
    /// Destination remote id, dispatcher, output stream, last error, last sequence
    /// number, and error handler. Not templated on Sig/TClass — see
    /// `detail::RemoteDispatchState`.
    detail::RemoteDispatchState m_state;

    /// A pointer to the function argument serializer. Kept outside `m_state` since,
    /// unlike everything else there, its pointee type genuinely varies by signature.
    ISerializer<RetType(Args...)>* m_serializer = nullptr;

    // </common_code>
};

/// @brief Creates an asynchronous delegate that binds to a free function.
/// @tparam RetType The return type of the free function.
/// @tparam Args The types of the function arguments.
/// @param[in] func A pointer to the free function to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateFreeRemote` object bound to the specified free function and id.
template <class RetType, class... Args>
auto MakeDelegate(RetType(*func)(Args... args), DelegateRemoteId id) {
    return DelegateFreeRemote<RetType(Args...)>(func, id);
}

/// @brief Creates an asynchronous delegate that binds to a non-const member function.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateMemberRemote` object bound to the specified non-const member function and id.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args... args), DelegateRemoteId id) {
    return DelegateMemberRemote<TClass, RetType(Args...)>(object, func, id);
}

/// @brief Creates an asynchronous delegate that binds to a const member function.
/// @tparam TClass The class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateMemberRemote` object bound to the specified const member function and id.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args... args) const, DelegateRemoteId id) {
    return DelegateMemberRemote<TClass, RetType(Args...)>(object, func, id);
}

/// @brief Creates a delegate that binds to a const member function.
/// @tparam TClass The const class type that contains the const member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateMemberRemote` object bound to the specified non-const member function.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(const TClass* object, RetType(TClass::* func)(Args... args) const, DelegateRemoteId id) {
    return DelegateMemberRemote<const TClass, RetType(Args...)>(object, func, id);
}

/// @brief Creates an asynchronous delegate that binds to a non-const member function using a shared pointer.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the non-const member function of `TClass` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateMemberRemote` shared pointer bound to the specified non-const member function and id.
template <class TClass, class RetVal, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetVal(TClass::* func)(Args... args), DelegateRemoteId id) {
    return DelegateMemberRemote<TClass, RetVal(Args...)>(object, func, id);
}

/// @brief Creates an asynchronous delegate that binds to a const member function using a shared pointer.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetVal The return type of the member function.
/// @tparam Args The types of the function arguments.
/// @param[in] object A shared pointer to the instance of `TClass` that will be used for the delegate.
/// @param[in] func A pointer to the const member function of `TClass` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateMemberRemote` shared pointer bound to the specified const member function and id.
template <class TClass, class RetVal, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetVal(TClass::* func)(Args... args) const, DelegateRemoteId id) {
    return DelegateMemberRemote<TClass, RetVal(Args...)>(object, func, id);
}

/// @brief Creates an asynchronous delegate that binds to a `std::function`.
/// @tparam RetType The return type of the `std::function`.
/// @tparam Args The types of the function arguments.
/// @param[in] func The `std::function` to bind to the delegate.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateFunctionRemote` object bound to the specified `std::function` and id.
template <class RetType, class... Args>
auto MakeDelegate(std::function<RetType(Args...)> func, DelegateRemoteId id) {
    return DelegateFunctionRemote<RetType(Args...)>(func, id);
}

/// @brief Creates an asynchronous delegate that binds to a raw lambda or functor.
/// @tparam F The lambda or functor type.
/// @param[in] func The lambda or functor to bind.
/// @param[in] id The delegate remote identifier.
/// @return A `DelegateFunctionRemote` object bound to the specified lambda or functor and id.
template <typename F, typename = std::enable_if_t<trait::is_callable<F>::value>>
auto MakeDelegate(F&& func, DelegateRemoteId id) {
    using Sig = typename trait::function_traits<decltype(&std::remove_reference_t<F>::operator())>::function_type;
    return DelegateFunctionRemote<Sig>(std::forward<F>(func), id);
}

}

DMQ_OPTIMIZE_OFF

#endif
