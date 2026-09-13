#include "NetworkMgr.h"

#if defined(STM32F407xx)
    extern UART_HandleTypeDef huart6;
#endif

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

NetworkMgr::NetworkMgr()
{
}

int NetworkMgr::Create()
{
    // Initialize one RemoteChannel per message signature.
    // Each channel owns its Dispatcher, stream, and serializer.
    m_alarmChannel.emplace(GetSendTransport(), m_alarmSer);
    m_commandChannel.emplace(GetSendTransport(), m_commandSer);
    m_dataChannel.emplace(GetSendTransport(), m_dataSer);
    m_actuatorChannel.emplace(GetSendTransport(), m_actuatorSer);

    // Bind the receive-side handler and wire the send-side infrastructure in one call.
    m_alarmChannel->Bind(this, &NetworkMgr::ForwardAlarm, ids::ALARM_MSG_ID);
    m_dataChannel->Bind(this, &NetworkMgr::ForwardData, ids::DATA_MSG_ID);
    m_commandChannel->Bind(this, &NetworkMgr::ForwardCommand, ids::COMMAND_MSG_ID);
    m_actuatorChannel->Bind(this, &NetworkMgr::ForwardActuator, ids::ACTUATOR_MSG_ID);

    // Register error handlers
    m_alarmChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_dataChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_commandChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));
    m_actuatorChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::OnError));

    // Register endpoints with the Base Engine (So Incoming() can find them)
    RegisterEndpoint(ids::ALARM_MSG_ID,    m_alarmChannel->GetEndpoint());
    RegisterEndpoint(ids::DATA_MSG_ID,     m_dataChannel->GetEndpoint());
    RegisterEndpoint(ids::COMMAND_MSG_ID,  m_commandChannel->GetEndpoint());
    RegisterEndpoint(ids::ACTUATOR_MSG_ID, m_actuatorChannel->GetEndpoint());

    // Initialize Base Engine
#ifdef SERVER_APP
    #if defined(STM32F407xx)
        return Initialize(&huart6);
    #endif
#else
    // Connects to STM32 UART @ 115200 baud
    // @TODO Change PC COM port if necessary.
    return Initialize("COM3", 115200);
#endif
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
