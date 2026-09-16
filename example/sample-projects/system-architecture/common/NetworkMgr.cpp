#include "NetworkMgr.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

NetworkMgr::NetworkMgr()
{
    m_dispatcher.SetCloseHandler(MakeDelegate(this, &NetworkMgr::CloseTransports));

    // m_dispatcher notifies via Signal, not a virtual hook -- forward to our
    // own Signals so client code's existing names don't change.
    m_baseErrorConn = m_dispatcher.OnError.Connect(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_baseStatusConn = m_dispatcher.OnStatus.Connect(MakeDelegate(this, &NetworkMgr::ForwardStatus));
    m_baseDeliveryFailedConn = m_dispatcher.OnDeliveryFailed.Connect(MakeDelegate(this, &NetworkMgr::ForwardDeliveryFailed));
}

int NetworkMgr::Create()
{
    // Hand our transports to m_dispatcher. ZeroMQ handles its own
    // reliability, so send/recv are attached directly (no ReliableTransport
    // wrapping, no AttachRetryMonitor() call -- see class doc comment).
    m_dispatcher.Attach(m_sendTransport, m_recvTransport);

    // Initialize one RemoteChannel per message signature.
    // Each channel owns its Dispatcher, stream, and serializer — and now also
    // owns the delegate binding via Bind(), replacing the separate DelegateMemberRemote.
    m_alarmChannel.emplace(m_dispatcher.GetSendTransport(), m_alarmSer);
    m_commandChannel.emplace(m_dispatcher.GetSendTransport(), m_commandSer);
    m_dataChannel.emplace(m_dispatcher.GetSendTransport(), m_dataSer);
    m_actuatorChannel.emplace(m_dispatcher.GetSendTransport(), m_actuatorSer);

    // Bind the receive-side handler and wire the send-side infrastructure in one call.
    m_alarmChannel->Bind(this, &NetworkMgr::ForwardAlarm, ids::ALARM_MSG_ID);
    m_commandChannel->Bind(this, &NetworkMgr::ForwardCommand, ids::COMMAND_MSG_ID);
    m_dataChannel->Bind(this, &NetworkMgr::ForwardData, ids::DATA_MSG_ID);
    m_actuatorChannel->Bind(this, &NetworkMgr::ForwardActuator, ids::ACTUATOR_MSG_ID);

    // Register error handlers
    m_alarmChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_commandChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_dataChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_actuatorChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));

    // Register endpoints with m_dispatcher (so Incoming() can route by ID)
    m_dispatcher.RegisterEndpoint(ids::ALARM_MSG_ID,    m_alarmChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::COMMAND_MSG_ID,  m_commandChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::DATA_MSG_ID,     m_dataChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::ACTUATOR_MSG_ID, m_actuatorChannel->GetEndpoint());

    if (!m_dispatcher.GetThread().IsCurrentThread())
        return MakeDelegate(this, &NetworkMgr::OpenTransport, m_dispatcher.GetThread(), dmq::WAIT_INFINITE)();
    return OpenTransport();
}

int NetworkMgr::OpenTransport()
{
    int err = 0;
#ifdef SERVER_APP
    auto type = dmq::transport::ZeroMqTransport::Type::PAIR_SERVER;
    err += m_sendTransport.Create(type, "tcp://*:5555");
    err += m_recvTransport.Create(type, "tcp://*:5556");
#else
    auto type = dmq::transport::ZeroMqTransport::Type::PAIR_CLIENT;
    err += m_sendTransport.Create(type, "tcp://localhost:5556");
    err += m_recvTransport.Create(type, "tcp://localhost:5555");
#endif

    m_sendTransport.SetTransportMonitor(&m_dispatcher.GetTransportMonitor());
    m_recvTransport.SetTransportMonitor(&m_dispatcher.GetTransportMonitor());
    m_sendTransport.SetRecvTransport(&m_recvTransport);
    m_recvTransport.SetSendTransport(&m_sendTransport);

    return err;
}

void NetworkMgr::CloseTransports()
{
    m_recvTransport.Close();
    m_sendTransport.Close();
}

void NetworkMgr::SendAlarmMsg(AlarmMsg& msg, AlarmNote& note) {
    if (!m_dispatcher.GetThread().IsCurrentThread()) {
        MakeDelegate(this, &NetworkMgr::SendAlarmMsg, m_dispatcher.GetThread())(msg, note);
        return;
    }
    (*m_alarmChannel)(msg, note);
}

bool NetworkMgr::SendAlarmMsgWait(AlarmMsg& msg, AlarmNote& note) {
    return m_dispatcher.RemoteInvokeWait(*m_alarmChannel, msg, note);
}

void NetworkMgr::SendCommandMsg(CommandMsg& command) {
    if (!m_dispatcher.GetThread().IsCurrentThread()) {
        MakeDelegate(this, &NetworkMgr::SendCommandMsg, m_dispatcher.GetThread())(command);
        return;
    }
    (*m_commandChannel)(command);
}

bool NetworkMgr::SendCommandMsgWait(CommandMsg& command) {
    return m_dispatcher.RemoteInvokeWait(*m_commandChannel, command);
}

void NetworkMgr::SendDataMsg(DataMsg& data) {
    if (!m_dispatcher.GetThread().IsCurrentThread()) {
        MakeDelegate(this, &NetworkMgr::SendDataMsg, m_dispatcher.GetThread())(data);
        return;
    }
    (*m_dataChannel)(data);
}

bool NetworkMgr::SendDataMsgWait(DataMsg& data) {
    return m_dispatcher.RemoteInvokeWait(*m_dataChannel, data);
}

bool NetworkMgr::SendActuatorMsgWait(ActuatorMsg& msg) {
    return m_dispatcher.RemoteInvokeWait(*m_actuatorChannel, msg);
}

std::future<bool> NetworkMgr::SendActuatorMsgFuture(ActuatorMsg& msg) {
    return std::async(&NetworkMgr::SendActuatorMsgWait, this, std::ref(msg));
}
