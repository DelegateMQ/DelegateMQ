#ifndef REMOTE_ENDPOINT_H
#define REMOTE_ENDPOINT_H

#include "delegate/DelegateRemote.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include <string>

namespace dmq::util {

template <class C, class R>
struct RemoteEndpoint; // Not defined

/// @brief `RemoteEndpoint` wraps a `DelegateMemberRemote` and its associated 
/// transport infrastructure into a single, cohesive unit.
/// @details This structure is designed to simplify the management of remote 
/// endpoints by grouping the delegate, the transport, and the necessary stream 
/// for serialization.
/// @tparam TClass The class type that contains the member function.
/// @tparam RetType The return type of the member function.
/// @tparam Args The types of the function arguments.
template <class TClass, class RetType, class... Args>
struct RemoteEndpoint<TClass, RetType(Args...)>
{
    XALLOCATOR
    /// @brief The `DelegateMemberRemote` object that handles the remote invocation.
    dmq::DelegateMemberRemote<TClass, RetType(Args...)> delegate;

    /// @brief The transport used to send the serialized data.
    dmq::transport::ITransport* transport = nullptr;

    /// @brief The stream used for serializing the function arguments.
    dmq::xostringstream stream;

    /// @brief The identifier for the remote endpoint.
    dmq::DelegateRemoteId id = 0;

    /// @brief Constructor to initialize the remote endpoint.
    /// @param[in] obj A shared pointer to the instance of `TClass`.
    /// @param[in] func A pointer to the member function of `TClass`.
    /// @param[in] _transport The transport to be used for remote communication.
    /// @param[in] _id The remote identifier for this endpoint.
    RemoteEndpoint(std::shared_ptr<TClass> obj, RetType(TClass::* func)(Args...), dmq::transport::ITransport& _transport, dmq::DelegateRemoteId _id)
        : delegate(obj, func, _id), transport(&_transport), stream(std::ios::in | std::ios::out | std::ios::binary), id(_id)
    {
    }
};

/// @brief Partial specialization of `RemoteEndpoint` for free functions.
/// @tparam RetType The return type of the function.
/// @tparam Args The types of the function arguments.
template <class RetType, class... Args>
struct RemoteEndpoint<void, RetType(Args...)>
{
    XALLOCATOR
    /// @brief The `DelegateFreeRemote` object that handles the remote invocation.
    dmq::DelegateFreeRemote<RetType(Args...)> delegate;

    /// @brief The transport used to send the serialized data.
    dmq::transport::ITransport* transport = nullptr;

    /// @brief The stream used for serializing the function arguments.
    dmq::xostringstream stream;

    /// @brief The identifier for the remote endpoint.
    dmq::DelegateRemoteId id = 0;

    /// @brief Constructor to initialize the remote endpoint for a free function.
    /// @param[in] func A pointer to the free function.
    /// @param[in] _transport The transport to be used for remote communication.
    /// @param[in] _id The remote identifier for this endpoint.
    RemoteEndpoint(RetType(*func)(Args...), dmq::transport::ITransport& _transport, dmq::DelegateRemoteId _id)
        : delegate(func, _id), transport(&_transport), stream(std::ios::in | std::ios::out | std::ios::binary), id(_id)
    {
    }
};

} // namespace dmq::util

#endif
