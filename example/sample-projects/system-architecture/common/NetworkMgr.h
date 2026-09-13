/// @file NetworkMgr.h
/// @brief Application-specific network manager for Alarms, Commands, and Data.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#ifndef NETWORK_MGR_H
#define NETWORK_MGR_H

#include "DelegateMQ.h"
#include "RemoteIds.h"
#include "AlarmMsg.h"
#include "DataMsg.h"
#include "CommandMsg.h"
#include "ActuatorMsg.h"
#include <optional>

/// @brief NetworkMgr sends and receives data using a DelegateMQ transport implemented
/// with the ZeroMQ library. Class is thread safe. All public APIs are
/// asynchronous.
///
/// @details NetworkMgr inherits from NetworkEngine, which manages the internal thread
/// of control. All public APIs are asynchronous (blocking and non-blocking). Register
/// with OnNetworkError or OnSendStatus to handle success or errors.
///
/// The underlying ZeroMQ transport layer managed by NetworkEngine is accessed only by a
/// single internal thread. Therefore, when invoking a remote delegate, the call is
/// automatically dispatched to the internal NetworkEngine thread.
///
/// **Key Responsibilities:**
/// * **Asynchronous Communication:** Exposes a fully thread-safe, asynchronous public API for network operations,
///   utilizing an internal thread managed by `NetworkEngine` to handle all I/O.
/// * **Transport Abstraction:** Implements specific ZeroMQ transport logic (creating and managing separate
///   send/receive sockets) while abstracting these details from the application logic.
/// * **Message Dispatching:** Automatically marshals all outgoing remote delegate invocations to the internal
///   network thread, ensuring safe single-threaded access to the underlying ZeroMQ resources.
/// * **Invocation Modes:** Support for three distinct remote invocation patterns:
///     1. *Fire-and-Forget (Non-blocking):* Sends messages immediately without waiting for confirmation.
///     2. *Synchronous Wait (Blocking):* Blocks the calling thread until an acknowledgment (ACK) is received or a timeout occurs.
///     3. *Future-based:* Returns a `std::future` immediately, allowing retrieval of the result at a later time.
/// * **Error & Status Reporting:** Provides registration points (`OnNetworkError`, `OnSendStatus`) for clients to subscribe
///   to transmission results and error notifications.
class NetworkMgr : public dmq::util::NetworkEngine
{
public:
    // Public Signals — clients Connect() to these using RAII ScopedConnection.
    dmq::Signal<void(AlarmMsg&, AlarmNote&)>                                            OnAlarm;
    dmq::Signal<void(CommandMsg&)>                                                      OnCommand;
    dmq::Signal<void(DataMsg&)>                                                         OnData;
    dmq::Signal<void(ActuatorMsg&)>                                                     OnActuator;
    dmq::Signal<void(dmq::DelegateRemoteId, dmq::DelegateError, dmq::DelegateErrorAux)> OnNetworkError;
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t, dmq::util::TransportMonitor::Status)>   OnSendStatus;
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t)>                                  OnDeliveryFailure;

    static NetworkMgr& Instance() { static NetworkMgr instance; return instance; }

    int Create();

    // Send Functions (non-blocking, blocking, and future)
    void SendAlarmMsg(AlarmMsg& msg, AlarmNote& note);
    bool SendAlarmMsgWait(AlarmMsg& msg, AlarmNote& note);
    void SendCommandMsg(CommandMsg& command);
    bool SendCommandMsgWait(CommandMsg& command);
    void SendDataMsg(DataMsg& data);
    bool SendDataMsgWait(DataMsg& data);
    bool SendActuatorMsgWait(ActuatorMsg& msg);
    std::future<bool> SendActuatorMsgFuture(ActuatorMsg& msg);

protected:
    // Override base class hooks to fire our Signals
    void OnError(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux) override;
    void OnStatus(dmq::DelegateRemoteId id, uint16_t seq, dmq::util::TransportMonitor::Status status) override;
    void OnDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seqNum) override;

private:
    NetworkMgr();
    ~NetworkMgr() = default;

    // Helper functions to forward incoming data to the Signals
    void ForwardAlarm(AlarmMsg& msg, AlarmNote& note)   { OnAlarm(msg, note); }
    void ForwardCommand(CommandMsg& msg)                { OnCommand(msg); }
    void ForwardData(DataMsg& msg)                      { OnData(msg); }
    void ForwardActuator(ActuatorMsg& msg)              { OnActuator(msg); }

    // Per-signature serializers (one per message type)
    dmq::serialization::msgpack::Serializer<void(AlarmMsg&, AlarmNote&)> m_alarmSer;
    dmq::serialization::msgpack::Serializer<void(CommandMsg&)>           m_commandSer;
    dmq::serialization::msgpack::Serializer<void(DataMsg&)>              m_dataSer;
    dmq::serialization::msgpack::Serializer<void(ActuatorMsg&)>          m_actuatorSer;

    // Channels aggregate the dispatcher, stream, serializer, and delegate binding
    // for each signature. Initialized in Create() once the send transport is available.
    std::optional<dmq::RemoteChannel<void(AlarmMsg&, AlarmNote&)>> m_alarmChannel;
    std::optional<dmq::RemoteChannel<void(CommandMsg&)>>           m_commandChannel;
    std::optional<dmq::RemoteChannel<void(DataMsg&)>>              m_dataChannel;
    std::optional<dmq::RemoteChannel<void(ActuatorMsg&)>>          m_actuatorChannel;
};

#endif
