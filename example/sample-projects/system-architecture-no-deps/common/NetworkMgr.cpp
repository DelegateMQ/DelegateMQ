#include "NetworkMgr.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

NetworkMgr::NetworkMgr()
    : m_retryMonitor(m_sendTransport, m_transportMonitor)
    , m_reliableTransport(m_sendTransport, m_retryMonitor)
{
}

int NetworkMgr::Create()
{
    // Hand our transports to the base engine. Raw UDP has no reliability of
    // its own, so the ReliableTransport wrapper (not the raw transport) is
    // attached as the send transport, and AttachRetryMonitor() wires
    // retry-exhaustion through to OnDeliveryFailed() (see class doc comment).
    Attach(m_reliableTransport, m_recvTransport);
    AttachRetryMonitor(m_retryMonitor);

    // Initialize one RemoteChannel per message signature.
    // Each channel owns its Dispatcher, stream, and serializer — and now also
    // owns the delegate binding via Bind(), replacing the separate DelegateMemberRemote.
    m_alarmChannel.emplace(GetSendTransport(), m_alarmSer);
    m_commandChannel.emplace(GetSendTransport(), m_commandSer);
    m_dataChannel.emplace(GetSendTransport(), m_dataSer);
    m_actuatorChannel.emplace(GetSendTransport(), m_actuatorSer);

    // Bind the receive-side handler and wire the send-side infrastructure in one call.
    m_alarmChannel->Bind(this, &NetworkMgr::ForwardAlarm, ids::ALARM_MSG_ID);
    m_commandChannel->Bind(this, &NetworkMgr::ForwardCommand, ids::COMMAND_MSG_ID);
    m_dataChannel->Bind(this, &NetworkMgr::ForwardData, ids::DATA_MSG_ID);
    m_actuatorChannel->Bind(this, &NetworkMgr::ForwardActuator, ids::ACTUATOR_MSG_ID);

    // Register error handlers
    m_alarmChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_commandChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_dataChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_actuatorChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));

    // Register endpoints with the Base Engine (so Incoming() can route by ID)
    RegisterEndpoint(ids::ALARM_MSG_ID,   m_alarmChannel->GetEndpoint());
    RegisterEndpoint(ids::COMMAND_MSG_ID, m_commandChannel->GetEndpoint());
    RegisterEndpoint(ids::DATA_MSG_ID,    m_dataChannel->GetEndpoint());
    RegisterEndpoint(ids::ACTUATOR_MSG_ID, m_actuatorChannel->GetEndpoint());

    if (!this->m_thread.IsCurrentThread())
        return dmq::MakeDelegate(this, &NetworkMgr::OpenTransport, this->m_thread, dmq::WAIT_INFINITE)();
    return OpenTransport();
}

int NetworkMgr::OpenTransport()
{
    int err = 0;
#if defined(DMQ_TRANSPORT_WIN32_UDP)
#ifdef SERVER_APP
    // Server Publishes on 50000, Listens on 50001
    err += m_sendTransport.Create(dmq::transport::Win32UdpTransport::Type::PUB, "127.0.0.1", 50000);
    err += m_recvTransport.Create(dmq::transport::Win32UdpTransport::Type::SUB, "127.0.0.1", 50001);
#else
    // Client Publishes on 50001, Listens on 50000
    err += m_sendTransport.Create(dmq::transport::Win32UdpTransport::Type::PUB, "127.0.0.1", 50001);
    err += m_recvTransport.Create(dmq::transport::Win32UdpTransport::Type::SUB, "127.0.0.1", 50000);
#endif
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
#ifdef SERVER_APP
    err += m_sendTransport.Create(dmq::transport::LinuxUdpTransport::Type::PUB, "127.0.0.1", 50000);
    err += m_recvTransport.Create(dmq::transport::LinuxUdpTransport::Type::SUB, "127.0.0.1", 50001);
#else
    err += m_sendTransport.Create(dmq::transport::LinuxUdpTransport::Type::PUB, "127.0.0.1", 50001);
    err += m_recvTransport.Create(dmq::transport::LinuxUdpTransport::Type::SUB, "127.0.0.1", 50000);
#endif
#endif

    m_sendTransport.SetTransportMonitor(&m_transportMonitor);
    m_recvTransport.SetTransportMonitor(&m_transportMonitor);
    m_sendTransport.SetRecvTransport(&m_recvTransport);
    m_recvTransport.SetSendTransport(&m_sendTransport);

    return err;
}

void NetworkMgr::CloseTransports()
{
    m_recvTransport.Close();
    m_sendTransport.Close();
}

// Override hooks to fire signals
void NetworkMgr::OnError(DelegateRemoteId id, DelegateError error, DelegateErrorAux aux) {
    OnNetworkError(id, error, aux);
}

void NetworkMgr::OnStatus(dmq::DelegateRemoteId id, uint16_t seq, dmq::util::TransportMonitor::Status status) {
    OnSendStatus(id, seq, status);
}

void NetworkMgr::OnDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seqNum) {
    OnDeliveryFailure(id, seqNum);
}

void NetworkMgr::SendAlarmMsg(AlarmMsg& msg, AlarmNote& note) {
    if (!this->m_thread.IsCurrentThread()) {
        dmq::MakeDelegate(this, &NetworkMgr::SendAlarmMsg, this->m_thread)(msg, note);
        return;
    }
    (*m_alarmChannel)(msg, note);
}

bool NetworkMgr::SendAlarmMsgWait(AlarmMsg& msg, AlarmNote& note) {
    return this->RemoteInvokeWait(*m_alarmChannel, msg, note);
}

void NetworkMgr::SendCommandMsg(CommandMsg& command) {
    if (!this->m_thread.IsCurrentThread()) {
        dmq::MakeDelegate(this, &NetworkMgr::SendCommandMsg, this->m_thread)(command);
        return;
    }
    (*m_commandChannel)(command);
}

bool NetworkMgr::SendCommandMsgWait(CommandMsg& command) {
    return this->RemoteInvokeWait(*m_commandChannel, command);
}

void NetworkMgr::SendDataMsg(DataMsg& data) {
    if (!this->m_thread.IsCurrentThread()) {
        dmq::MakeDelegate(this, &NetworkMgr::SendDataMsg, this->m_thread)(data);
        return;
    }
    (*m_dataChannel)(data);
}

bool NetworkMgr::SendDataMsgWait(DataMsg& data) {
    return this->RemoteInvokeWait(*m_dataChannel, data);
}

void NetworkMgr::SendActuatorMsg(ActuatorMsg& msg) {
    if (!this->m_thread.IsCurrentThread()) {
        dmq::MakeDelegate(this, &NetworkMgr::SendActuatorMsg, this->m_thread)(msg);
        return;
    }
    (*m_actuatorChannel)(msg);
}

bool NetworkMgr::SendActuatorMsgWait(ActuatorMsg& msg) {
    return this->RemoteInvokeWait(*m_actuatorChannel, msg);
}

std::future<bool> NetworkMgr::SendActuatorMsgFuture(ActuatorMsg& msg) {
    return std::async(&NetworkMgr::SendActuatorMsgWait, this, std::ref(msg));
}
