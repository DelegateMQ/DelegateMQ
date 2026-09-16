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

#include "extras/rpc/RemoteDispatcher.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#if defined(DMQ_TRANSPORT_STM32_UART)
#include "port/transport/stm32-uart/Stm32UartTransport.h"
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
#include "port/transport/serial/SerialTransport.h"
#endif

/// @brief NetworkMgr sends and receives data using a DelegateMQ transport implemented
/// with Windows UDP sockets and msg_serialize. Class is thread safe. All public APIs are
/// asynchronous.
///
/// @details NetworkMgr holds a RemoteDispatcher as a member (composition, not
/// inheritance -- matching extras/databus's Participant/NetworkNode shape),
/// which manages the internal thread of control. All public APIs are
/// asynchronous (blocking and non-blocking). Register with OnError or
/// OnSendStatus to handle success or errors.
///
/// This file is shared by both sides of the client/server pair: the desktop client
/// builds with DMQ_TRANSPORT_SERIAL_PORT (talking over a virtual COM port), the
/// embedded server builds with DMQ_TRANSPORT_STM32_UART (talking over the real
/// UART peripheral) -- hence the #if below picking the concrete transport type.
/// Either way NetworkMgr owns a single, full-duplex transport instance and hands
/// it to m_dispatcher via Attach() -- RemoteDispatcher only ever sees
/// dmq::transport::ITransport, it neither constructs nor knows the concrete type.
/// Neither UART nor a raw serial link is reliable on its own, so NetworkMgr layers
/// ReliableTransport+RetryMonitor on top and attaches the ReliableTransport (not
/// the raw transport) as the send transport, and calls AttachRetryMonitor() so
/// retry-exhaustion reaches OnDeliveryFailed().
///
/// The underlying UDP transport layer managed by RemoteDispatcher is accessed only by a
/// single internal thread. Therefore, when invoking a remote delegate, the call is
/// automatically dispatched to the internal RemoteDispatcher thread.
///
/// **Key Responsibilities:**
/// * **Asynchronous Communication:** Exposes a fully thread-safe, asynchronous public API for network operations,
///   utilizing an internal thread managed by `RemoteDispatcher` to handle all I/O.
/// * **Transport Abstraction:** Implements specific UDP transport logic (using Windows or Linux sockets) while abstracting
///   these details from the application logic. Two sockets are created: one for sending and one for receiving.
/// * **Message Dispatching:** Automatically marshals all outgoing remote delegate invocations to the internal
///   network thread, ensuring safe single-threaded access to the underlying UDP resources.
/// * **Invocation Modes:** Support for three distinct remote invocation patterns:
///     1. *Fire-and-Forget (Non-blocking):* Sends messages immediately without waiting for confirmation.
///     2. *Synchronous Wait (Blocking):* Blocks the calling thread until an acknowledgment (ACK) is received or a timeout occurs.
/// * **Error & Status Reporting:** Provides registration points (`OnNetworkError`, `OnSendStatus`) for clients to subscribe
///   to transmission results and error notifications.
class NetworkMgr
{
public:
    // Public Signals — thread-safe direct members. Clients Connect() using RAII.
    dmq::Signal<void(AlarmMsg&, AlarmNote&)>                                            OnAlarm;
    dmq::Signal<void(CommandMsg&)>                                                       OnCommand;
    dmq::Signal<void(DataMsg&)>                                                          OnData;
    dmq::Signal<void(ActuatorMsg&)>                                                      OnActuator;
    dmq::Signal<void(dmq::DelegateRemoteId, dmq::DelegateError, dmq::DelegateErrorAux)> OnNetworkError;
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t, dmq::util::TransportMonitor::Status)>   OnSendStatus;
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t)>                                  OnDeliveryFailure;

    static NetworkMgr& Instance() { static NetworkMgr instance; return instance; }

    int Create();

    // Thin forwarders -- app code calls these directly on NetworkMgr.
    void Start() { m_dispatcher.Start(); }
    void Stop()  { m_dispatcher.Stop(); }

    // Send Functions (non-blocking, blocking, and future)
    void SendAlarmMsg(AlarmMsg& msg, AlarmNote& note);
    bool SendAlarmMsgWait(AlarmMsg& msg, AlarmNote& note);
    void SendCommandMsg(CommandMsg& command);
    bool SendCommandMsgWait(CommandMsg& command);
    void SendDataMsg(DataMsg& data);
    bool SendDataMsgWait(DataMsg& data);
    void SendActuatorMsg(ActuatorMsg& msg);
    bool SendActuatorMsgWait(ActuatorMsg& msg);

private:
    NetworkMgr();
    ~NetworkMgr() = default;

    // Opens the UART/serial link and wires ACK/status routing. Must run on
    // the network thread, same as the transport-creation calls RemoteDispatcher's
    // old per-transport Initialize() used to marshal there itself.
    int OpenTransport();

    // ITransport has no Close(); close our own concrete transport here.
    // Wired to m_dispatcher via SetCloseHandler() in the constructor; called
    // by RemoteDispatcher::Stop() before the receive thread is joined.
    void CloseTransports() { m_transport.Close(); }

    // Helper functions to forward incoming data to the Signals
    void ForwardAlarm(AlarmMsg& msg, AlarmNote& note)   { OnAlarm(msg, note); }
    void ForwardCommand(CommandMsg& msg)                 { OnCommand(msg); }
    void ForwardData(DataMsg& msg)                       { OnData(msg); }
    void ForwardActuator(ActuatorMsg& msg)               { OnActuator(msg); }

    // Forward m_dispatcher's OnError/OnStatus/OnDeliveryFailed Signals (and,
    // for ForwardError, each channel's SetErrorHandler() too) to our own
    // OnNetworkError/OnSendStatus/OnDeliveryFailure.
    void ForwardError(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux) { OnNetworkError(id, error, aux); }
    void ForwardStatus(dmq::DelegateRemoteId id, uint16_t seq, dmq::util::TransportMonitor::Status status) { OnSendStatus(id, seq, status); }
    void ForwardDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seqNum) { OnDeliveryFailure(id, seqNum); }

    // Composed, not inherited -- declared first so it's fully constructed
    // before m_retryMonitor below references m_dispatcher.GetTransportMonitor().
    dmq::rpc::RemoteDispatcher m_dispatcher;

    // Per-signature serializers (one per message type)
    dmq::serialization::serializer::Serializer<void(AlarmMsg&, AlarmNote&)> m_alarmSer;
    dmq::serialization::serializer::Serializer<void(CommandMsg&)>           m_commandSer;
    dmq::serialization::serializer::Serializer<void(DataMsg&)>              m_dataSer;
    dmq::serialization::serializer::Serializer<void(ActuatorMsg&)>          m_actuatorSer;

    // Channels aggregate the dispatcher, stream, serializer, and delegate binding
    std::optional<dmq::RemoteChannel<void(AlarmMsg&, AlarmNote&)>> m_alarmChannel;
    std::optional<dmq::RemoteChannel<void(CommandMsg&)>>           m_commandChannel;
    std::optional<dmq::RemoteChannel<void(DataMsg&)>>              m_dataChannel;
    std::optional<dmq::RemoteChannel<void(ActuatorMsg&)>>          m_actuatorChannel;

    // Single full-duplex transport instance -- RemoteDispatcher only ever sees it
    // through the ITransport& passed to Attach()/AttachRetryMonitor().
#if defined(DMQ_TRANSPORT_STM32_UART)
    dmq::transport::Stm32UartTransport m_transport;
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
    dmq::transport::SerialTransport m_transport;
#endif
    // Neither UART nor raw serial is reliable on its own -- wrap in ACK/retry.
    dmq::util::RetryMonitor m_retryMonitor;
    dmq::util::ReliableTransport m_reliableTransport;

    // Connections for the ForwardError/Status/DeliveryFailed forwarding
    // wired in the constructor -- see those methods' comment above.
    dmq::ScopedConnection m_baseErrorConn;
    dmq::ScopedConnection m_baseStatusConn;
    dmq::ScopedConnection m_baseDeliveryFailedConn;
};

#endif
