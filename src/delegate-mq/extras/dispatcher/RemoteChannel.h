#ifndef REMOTE_CHANNEL_H
#define REMOTE_CHANNEL_H

/// @file RemoteChannel.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Aggregates transport, serializer, dispatcher, and stream into a single
/// object, mirroring how `IThread`/`Thread` hides async delegate wiring.
///
/// @details
/// Setting up a remote delegate currently requires three separate objects:
/// @code
///   Dispatcher dispatcher;
///   dispatcher.SetTransport(&transport);
///   Serializer<void(int)> serializer;
///   dmq::xostringstream stream(std::ios::in | std::ios::out | std::ios::binary);
///
///   DelegateFreeRemote<void(int)> remote(REMOTE_ID);
///   remote.SetSerializer(&serializer);
///   remote.SetDispatcher(&dispatcher);
///   remote.SetStream(&stream);
/// @endcode
///
/// `RemoteChannel` collapses this into one setup object:
/// @code
///   MyTransport transport;
///   Serializer<void(int)> serializer;
///   RemoteChannel<void(int)> channel(transport, serializer);
///
///   // Mirrors the async pattern exactly:
///   auto async  = MakeDelegate(&MyFunc, thread);             // async
///   auto remote = MakeDelegate(&MyFunc, REMOTE_ID, channel); // remote
/// @endcode
///
/// @tparam Sig The function signature (e.g. `void(int, float)`) that matches both
/// the serializer and the remote delegates that use this channel. One channel
/// instance is required per distinct function signature.

#include "Dispatcher.h"
#include "delegate/DelegateRemote.h"
#include "delegate/Signal.h"
#include "port/transport/ITransport.h"

namespace dmq {

template <class Sig>
class RemoteChannel; // Not defined

/// @brief Aggregates dispatcher, stream, serializer, and delegate binding for a single
/// function signature. The canonical way to configure a remote endpoint.
///
/// @details `RemoteChannel` is the single object a user needs to declare per message
/// signature. It owns the `Dispatcher`, the serialization stream, and the internal
/// `DelegateFunctionRemote` that handles both sending and receiving.
///
/// **Receive-side usage (preferred):**
/// @code
///   // Declare per signature — one channel replaces channel + separate delegate
///   std::optional<RemoteChannel<void(AlarmMsg&)>> m_alarmChannel;
///
///   // In Create():
///   m_alarmChannel.emplace(GetRecvTransport(), m_alarmSer);
///   m_alarmChannel->Bind(this, &MyClass::OnAlarm, ALARM_ID);
///   m_alarmChannel->SetErrorHandler(MakeDelegate(this, &MyClass::OnError));
///   RegisterEndpoint(ALARM_ID, m_alarmChannel->GetEndpoint());
/// @endcode
///
/// **Send-side usage — no Bind() required:**
/// @code
///   m_alarmChannel.emplace(GetSendTransport(), m_alarmSer, ALARM_ID);
///
///   // Send (fire-and-forget):
///   (*m_alarmChannel)(msg);
///
///   // Send (blocking wait):
///   RemoteInvokeWait(*m_alarmChannel, msg);
/// @endcode
///
/// **Legacy MakeDelegate pattern (still supported):**
/// @code
///   auto d = MakeDelegate(&MyFunc, REMOTE_ID, channel);
/// @endcode
///
/// `RemoteChannel` is non-copyable because it owns mutable stream state and the internal
/// delegate holds raw pointers into the channel.
///
/// @tparam RetType The return type of the remote function.
/// @tparam Args    The argument types of the remote function.
template <class RetType, class... Args>
class RemoteChannel<RetType(Args...)>
{
    XALLOCATOR
public:
    /// @brief Construct a RemoteChannel.
    /// @details A send-only channel needs no `Bind()` — pass the remote ID here and
    /// invoke the channel directly. The receive side must invoke `Bind()` to attach the
    /// target function invoked on message arrival (`Bind()` overwrites the ID with the
    /// same value).
    /// @param[in] transport  The transport used to send serialized data. Caller owns it.
    /// @param[in] serializer The serializer matching the delegate signature. Caller owns it.
    /// @param[in] id         The remote delegate identifier shared with the receiver.
    /// Optional for receive channels where `Bind()` supplies the ID.
    RemoteChannel(transport::ITransport& transport, dmq::ISerializer<RetType(Args...)>& serializer,
        DelegateRemoteId id = INVALID_REMOTE_ID)
        : m_stream(std::ios::in | std::ios::out | std::ios::binary)
        , m_serializer(&serializer)
    {
        m_dispatcher.SetTransport(&transport);
        m_delegate.SetRemoteId(id);
        m_delegate.SetDispatcher(&m_dispatcher);
        m_delegate.SetSerializer(m_serializer);
        m_delegate.SetStream(&m_stream);
    }

    ~RemoteChannel() = default;

    // Non-copyable: owns stream state and internal delegate holds raw pointers into this object.
    RemoteChannel(const RemoteChannel&) = delete;
    RemoteChannel& operator=(const RemoteChannel&) = delete;

    // -----------------------------------------------------------------------
    // Delegate binding — preferred user-facing API (Item 2 / Item 4)
    // -----------------------------------------------------------------------

    /// @brief Bind a non-const member function as the receive-side handler.
    /// @details Wires the internal delegate (dispatcher, serializer, stream) and sets
    /// the target function in one call. Call this once in your Create()/Initialize().
    /// @param[in] object  Raw pointer to the target object.
    /// @param[in] func    The non-const member function to call on receive.
    /// @param[in] id      The remote delegate identifier shared with the sender.
    template<class TClass>
    void Bind(TClass* object, RetType(TClass::* func)(Args...), DelegateRemoteId id) {
        m_delegate.Bind([object, func](Args... args) -> RetType {
            return (object->*func)(args...);
        }, id);
        ReconnectDelegate();
    }

    /// @brief Bind a const member function as the receive-side handler.
    template<class TClass>
    void Bind(const TClass* object, RetType(TClass::* func)(Args...) const, DelegateRemoteId id) {
        m_delegate.Bind([object, func](Args... args) -> RetType {
            return (object->*func)(args...);
        }, id);
        ReconnectDelegate();
    }

    /// @brief Bind a `std::function` as the receive-side handler.
    void Bind(std::function<RetType(Args...)> func, DelegateRemoteId id) {
        m_delegate.Bind(func, id);
        ReconnectDelegate();
    }

    /// @brief Bind a raw lambda or functor as the receive-side handler.
    template <typename F, typename = std::enable_if_t<dmq::trait::is_callable<F>::value>>
    void Bind(F&& func, DelegateRemoteId id) {
        m_delegate.Bind(std::forward<F>(func), id);
        ReconnectDelegate();
    }

    /// @brief Invoke the channel (fire-and-forget send).
    /// @pre Bind() must have been called first.
    void operator()(Args... args) {
        dmq::LockGuard<dmq::Mutex> lock(m_sendMutex);
        m_delegate(std::forward<Args>(args)...);
    }

    /// @brief Register an error handler delegate.
    template<class Handler>
    void SetErrorHandler(Handler&& handler) {
        m_delegate.SetErrorHandler(std::forward<Handler>(handler));
    }

    /// @brief Set the remote ID for outbound messages.
    /// @param[in] id The remote delegate identifier.
    void SetRemoteId(DelegateRemoteId id) noexcept { m_delegate.SetRemoteId(id); }

    /// @brief The remote ID set by the most recent Bind() or SetRemoteId() call.
    DelegateRemoteId GetRemoteId() noexcept { return m_delegate.GetRemoteId(); }

    /// @brief The error status of the most recent invocation.
    DelegateError GetError() noexcept { return m_delegate.GetError(); }

    /// @brief The sequence number assigned to the most recent send.
    uint16_t GetLastSeqNum() const noexcept { return m_delegate.GetLastSeqNum(); }

    /// @brief Returns the internal delegate as an IRemoteInvoker* for RegisterEndpoint().
    IRemoteInvoker* GetEndpoint() noexcept { return &m_delegate; }

private:
    template <class R, class... A>
    friend auto MakeDelegate(R(*)(A...), DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class C, class R, class... A>
    friend auto MakeDelegate(C*, R(C::*)(A...), DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class C, class R, class... A>
    friend auto MakeDelegate(C*, R(C::*)(A...) const, DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class C, class R, class... A>
    friend auto MakeDelegate(const C*, R(C::*)(A...) const, DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class C, class R, class... A>
    friend auto MakeDelegate(std::shared_ptr<C>, R(C::*)(A...), DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class C, class R, class... A>
    friend auto MakeDelegate(std::shared_ptr<C>, R(C::*)(A...) const, DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <class R, class... A>
    friend auto MakeDelegate(std::function<R(A...)>, DelegateRemoteId, RemoteChannel<R(A...)>&);

    template <typename F_, typename Sig_, typename>
    friend auto MakeDelegate(F_&&, DelegateRemoteId, RemoteChannel<Sig_>&);

    /// @internal Used by MakeDelegate overloads. Prefer Bind() in application code.
    dmq::IDispatcher* GetDispatcher() noexcept { return &m_dispatcher; }

    /// @internal Used by MakeDelegate overloads. Prefer Bind() in application code.
    dmq::ISerializer<RetType(Args...)>* GetSerializer() noexcept { return m_serializer; }

    /// @internal Used by MakeDelegate overloads. Prefer Bind() in application code.
    dmq::xostringstream& GetStream() noexcept { return m_stream; }

private:
    void ReconnectDelegate() {
        m_delegate.SetDispatcher(&m_dispatcher);
        m_delegate.SetSerializer(m_serializer);
        m_delegate.SetStream(&m_stream);
    }

    dmq::Mutex m_sendMutex;
    Dispatcher m_dispatcher;
    dmq::xostringstream m_stream;
    dmq::ISerializer<RetType(Args...)>* m_serializer = nullptr;
    DelegateFunctionRemote<RetType(Args...)> m_delegate;
};

/// @brief C++17 deduction guide — lets the compiler deduce `Sig` from the serializer type.
/// @details Enables `RemoteChannel channel(transport, serializer)` and
/// `RemoteChannel channel(transport, serializer, id)` without an explicit template argument.
template <class RetType, class... Args>
RemoteChannel(transport::ITransport&, dmq::ISerializer<RetType(Args...)>&, DelegateRemoteId = INVALID_REMOTE_ID) -> RemoteChannel<RetType(Args...)>;

// ---- MakeDelegate overloads for RemoteChannel ----------------------------------
//
// These mirror the MakeDelegate(func, thread) async overloads exactly, but accept
// a RemoteChannel instead of an IThread, wiring up serializer, dispatcher, and
// stream in one step.

/// @brief Creates a remote delegate bound to a free function via a RemoteChannel.
/// @param[in] func    The free function to bind.
/// @param[in] id      The remote delegate identifier shared with the receiver.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateFreeRemote` ready to invoke remotely.
template <class RetType, class... Args>
auto MakeDelegate(RetType(*func)(Args...), DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateFreeRemote<RetType(Args...)> d(func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a non-const member function via a RemoteChannel.
/// @param[in] object  Raw pointer to the target object instance.
/// @param[in] func    The non-const member function to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateMemberRemote`.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args...), DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateMemberRemote<TClass, RetType(Args...)> d(object, func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a const member function via a RemoteChannel.
/// @param[in] object  Raw pointer to the target object instance.
/// @param[in] func    The const member function to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateMemberRemote`.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(TClass* object, RetType(TClass::* func)(Args...) const, DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateMemberRemote<TClass, RetType(Args...)> d(object, func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a const member function on a const object.
/// @param[in] object  Raw const pointer to the target object instance.
/// @param[in] func    The const member function to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateMemberRemote`.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(const TClass* object, RetType(TClass::* func)(Args...) const, DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateMemberRemote<const TClass, RetType(Args...)> d(object, func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a non-const member function via shared_ptr.
/// @param[in] object  Shared pointer to the target object instance.
/// @param[in] func    The non-const member function to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateMemberRemote`.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetType(TClass::* func)(Args...), DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateMemberRemote<TClass, RetType(Args...)> d(object, func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a const member function via shared_ptr.
/// @param[in] object  Shared pointer to the target object instance.
/// @param[in] func    The const member function to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateMemberRemote`.
template <class TClass, class RetType, class... Args>
auto MakeDelegate(std::shared_ptr<TClass> object, RetType(TClass::* func)(Args...) const, DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateMemberRemote<TClass, RetType(Args...)> d(object, func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a `std::function` via a RemoteChannel.
/// @param[in] func    The `std::function` to bind.
/// @param[in] id      The remote delegate identifier.
/// @param[in] channel The channel that provides dispatcher, serializer, and stream.
/// @return A fully configured `DelegateFunctionRemote`.
template <class RetType, class... Args>
auto MakeDelegate(std::function<RetType(Args...)> func, DelegateRemoteId id, RemoteChannel<RetType(Args...)>& channel)
{
    DelegateFunctionRemote<RetType(Args...)> d(func, id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

/// @brief Creates a remote delegate bound to a raw lambda or functor via a RemoteChannel.
template <typename F, typename Sig, typename = std::enable_if_t<dmq::trait::is_callable<F>::value>>
auto MakeDelegate(F&& func, DelegateRemoteId id, RemoteChannel<Sig>& channel)
{
    DelegateFunctionRemote<Sig> d(std::forward<F>(func), id);
    d.SetDispatcher(channel.GetDispatcher());
    d.SetSerializer(channel.GetSerializer());
    d.SetStream(&channel.GetStream());
    return d;
}

} // namespace dmq

#endif // REMOTE_CHANNEL_H
