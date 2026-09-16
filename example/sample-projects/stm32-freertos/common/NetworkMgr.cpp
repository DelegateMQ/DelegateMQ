#include "NetworkMgr.h"

#if defined(STM32F407xx)
    extern UART_HandleTypeDef huart6;
#endif

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

#if defined(DMQ_TRANSPORT_STM32_UART) && defined(DMQ_THREAD_FREERTOS)
// [STM32-FreeRTOS] Static stack for the network thread. Increased to 2048
// words (8KB) to handle Debug mode call depths.
static StackType_t g_networkThreadStack[2048];
#endif

NetworkMgr::NetworkMgr()
    : m_retryMonitor(m_transport, m_dispatcher.GetTransportMonitor())
    , m_reliableTransport(m_transport, m_retryMonitor)
{
#if defined(DMQ_TRANSPORT_STM32_UART) && defined(DMQ_THREAD_FREERTOS)
    m_dispatcher.GetThread().SetStackMem(g_networkThreadStack, 2048);
#endif
    m_dispatcher.SetCloseHandler(MakeDelegate(this, &NetworkMgr::CloseTransports));

    // m_dispatcher notifies via Signal, not a virtual hook -- forward to our
    // own Signals so client code's existing names don't change.
    m_baseErrorConn = m_dispatcher.OnError.Connect(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_baseStatusConn = m_dispatcher.OnStatus.Connect(MakeDelegate(this, &NetworkMgr::ForwardStatus));
    m_baseDeliveryFailedConn = m_dispatcher.OnDeliveryFailed.Connect(MakeDelegate(this, &NetworkMgr::ForwardDeliveryFailed));
}

int NetworkMgr::Create()
{
    // Hand our transport to m_dispatcher. Neither UART nor raw serial is
    // reliable on its own, so the ReliableTransport wrapper (not the raw
    // transport) is attached as the send transport, and AttachRetryMonitor()
    // wires retry-exhaustion through to OnDeliveryFailed() (see class doc
    // comment). Receive goes straight to the raw transport, same object as
    // send since this link is full-duplex on one instance.
    m_dispatcher.Attach(m_reliableTransport, m_transport);
    m_dispatcher.AttachRetryMonitor(m_retryMonitor);

    // Initialize one RemoteChannel per message signature.
    // Each channel owns its Dispatcher, stream, and serializer.
    m_alarmChannel.emplace(m_dispatcher.GetSendTransport(), m_alarmSer);
    m_commandChannel.emplace(m_dispatcher.GetSendTransport(), m_commandSer);
    m_dataChannel.emplace(m_dispatcher.GetSendTransport(), m_dataSer);
    m_actuatorChannel.emplace(m_dispatcher.GetSendTransport(), m_actuatorSer);

    // Bind the receive-side handler and wire the send-side infrastructure in one call.
    m_alarmChannel->Bind(this, &NetworkMgr::ForwardAlarm, ids::ALARM_MSG_ID);
    m_dataChannel->Bind(this, &NetworkMgr::ForwardData, ids::DATA_MSG_ID);
    m_commandChannel->Bind(this, &NetworkMgr::ForwardCommand, ids::COMMAND_MSG_ID);
    m_actuatorChannel->Bind(this, &NetworkMgr::ForwardActuator, ids::ACTUATOR_MSG_ID);

    // Register error handlers
    m_alarmChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_dataChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_commandChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));
    m_actuatorChannel->SetErrorHandler(MakeDelegate(this, &NetworkMgr::ForwardError));

    // Register endpoints with m_dispatcher (so Incoming() can find them)
    m_dispatcher.RegisterEndpoint(ids::ALARM_MSG_ID,    m_alarmChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::DATA_MSG_ID,     m_dataChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::COMMAND_MSG_ID,  m_commandChannel->GetEndpoint());
    m_dispatcher.RegisterEndpoint(ids::ACTUATOR_MSG_ID, m_actuatorChannel->GetEndpoint());

    if (!m_dispatcher.GetThread().IsCurrentThread())
        return MakeDelegate(this, &NetworkMgr::OpenTransport, m_dispatcher.GetThread(), dmq::WAIT_INFINITE)();
    return OpenTransport();
}

int NetworkMgr::OpenTransport()
{
    int err = 0;

#if defined(DMQ_TRANSPORT_STM32_UART)
    #if defined(STM32F407xx)
        // Call Create() ONCE on the shared, full-duplex object.
        err += m_transport.Create(&huart6);
    #endif

    m_transport.SetTransportMonitor(&m_dispatcher.GetTransportMonitor());
    // Point to self for full-duplex logic.
    m_transport.SetRecvTransport(&m_transport);
    m_transport.SetSendTransport(&m_transport);
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
    // Connects to STM32 UART @ 115200 baud
    // @TODO Change PC COM port if necessary.
    err += m_transport.Create("COM3", 115200);

    if (err == 0) {
        m_transport.SetTransportMonitor(&m_dispatcher.GetTransportMonitor());
        // Serial is full-duplex logic on one object.
        m_transport.SetRecvTransport(&m_transport);
        m_transport.SetSendTransport(&m_transport);
    }
#endif

    return err;
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

void NetworkMgr::SendActuatorMsg(ActuatorMsg& msg) {
    if (!m_dispatcher.GetThread().IsCurrentThread()) {
        MakeDelegate(this, &NetworkMgr::SendActuatorMsg, m_dispatcher.GetThread())(msg);
        return;
    }
    (*m_actuatorChannel)(msg);
}

bool NetworkMgr::SendActuatorMsgWait(ActuatorMsg& msg) {
    return m_dispatcher.RemoteInvokeWait(*m_actuatorChannel, msg);
}
