#include "System.h"
#include "util/Topology.h"
#include "messages/HeartbeatMsg.h"

using namespace dmq;
using namespace dmq::databus;

namespace pumptron {

System::System()
    : m_thread("GUI_System", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::FAULT)
    , m_timerThread("GUI_Timers")
{
}

bool System::Initialize(const Options& options, std::string& error)
{
    m_busErrorConn = DataBus::SubscribeError(MakeDelegate(this, &System::OnBusError));
    m_unhandledConn = DataBus::SubscribeUnhandled(MakeDelegate(this, &System::OnUnhandled));

    if (!m_thread.CreateThread(WATCHDOG_TIMEOUT)) {
        error = "failed to create system thread";
        return false;
    }

    // Timer::ProcessTimers() drives every dmq::util::Timer in the process
    // (heartbeat, deadline watches, link receive/retry polling).
    m_timerRunning = true;
    m_timerThread.CreateThread();
    (void)MakeDelegate(this, &System::TimerLoop, m_timerThread).AsyncInvoke();

    if (options.link == LinkType::SERIAL) {
#if defined(PUMPTRON_HAVE_SERIAL)
        m_serial.emplace();
        if (m_serial->GetTransport().Create(options.serialPort.c_str(), options.baud) != 0) {
            error = "could not open serial port " + options.serialPort;
            m_serial.reset();
            return false;
        }
        ConfigureGuiLink(*m_serial);
        ConnectLinkSignals(*m_serial);
        m_sendDroppedConn = m_serial->OnSendDropped.Connect(MakeDelegate(this, &System::OnSendDropped));
        if (!m_serial->Start("Controller", SerialLinkT::RecvMode::INTERNAL_THREAD, WATCHDOG_TIMEOUT)) {
            error = "failed to start serial link";
            return false;
        }
        m_linkDescription = "Serial " + options.serialPort + " @ " + std::to_string(options.baud);
#else
        error = "this build has no serial support (libserialport not found at configure time)";
        return false;
#endif
    } else {
        m_udp.emplace();
        ConfigureGuiLink(*m_udp);
        ConnectLinkSignals(*m_udp);
        if (!m_udp->Start("GUI", GUI_UDP_PORT)) {
            error = "could not open UDP port " + std::to_string(GUI_UDP_PORT);
            m_udp.reset();
            return false;
        }
        m_udp->AddPeer("Controller", "127.0.0.1", CONTROLLER_UDP_PORT);
        m_linkDescription = "UDP localhost:" + std::to_string(CONTROLLER_UDP_PORT) + " (simulator)";
    }

    m_heartbeatConn = m_heartbeatTimer.OnExpired.Connect(
        util::MakeTimerDelegate(this, &System::OnHeartbeatTick, m_thread));
    m_heartbeatTimer.Start(HEARTBEAT_PERIOD);
    return true;
}

void System::Shutdown()
{
    m_heartbeatTimer.Stop();
    m_heartbeatConn.Disconnect();

#if defined(PUMPTRON_HAVE_SERIAL)
    if (m_serial) m_serial->Stop();
#endif
    if (m_udp) m_udp->Stop();

    m_thread.ExitThread();
    m_timerRunning = false;
    m_timerThread.ExitThread();
}

template <typename Link>
void System::ConnectLinkSignals(Link& link)
{
    m_sendStatusConn = link.OnPeerSendStatus.Connect(MakeDelegate(this, &System::OnSendStatus));
    m_deliveryFailedConn = link.OnDeliveryFailed.Connect(MakeDelegate(this, &System::OnDeliveryFailed));
    m_capExceededConn = link.OnPeerCapExceeded.Connect(MakeDelegate(this, &System::OnCapExceeded));
    m_pendingExceededConn = link.OnPeerPendingExceeded.Connect(MakeDelegate(this, &System::OnPendingExceeded));
}

void System::Emit(const std::string& text)
{
    OnEvent(text);
}

void System::OnBusError(const dmq::xstring& topic, dmq::DelegateError error)
{
    m_stats.busErrors++;
    Emit("ERROR  DataBus error on " + std::string(topic.c_str()) +
         " (code " + std::to_string(static_cast<int>(error)) + ")");
}

void System::OnUnhandled(const dmq::xstring& topic)
{
    m_stats.busErrors++;
    Emit("ERROR  published with no subscriber: " + std::string(topic.c_str()));
}

void System::OnCapExceeded(const dmq::xstring&, size_t count)
{
    Emit("WARN   link backlog: " + std::to_string(count) + " unacknowledged messages");
}

void System::OnPendingExceeded(const dmq::xstring&, size_t remaining)
{
    Emit("WARN   link retries not draining (" + std::to_string(remaining) + " pending)");
}

void System::TimerLoop()
{
    while (m_timerRunning) {
        util::Timer::ProcessTimers();
        os::Thread::Sleep(TIMER_TICK_PERIOD);
    }
}

void System::OnHeartbeatTick()
{
    if (!m_heartbeatEnabled)
        return;
    DataBus::Publish<HeartbeatMsg>(topics::HB_GUI, HeartbeatMsg(++m_heartbeatCount));
}

void System::OnSendStatus(const dmq::xstring&, dmq::DelegateRemoteId, uint16_t,
                          util::TransportMonitor::Status status)
{
    if (status == util::TransportMonitor::Status::SUCCESS)
        m_stats.acked++;
    else if (status == util::TransportMonitor::Status::TIMEOUT)
        m_stats.retries++;
}

void System::OnDeliveryFailed(const dmq::xstring&, dmq::DelegateRemoteId id, uint16_t)
{
    m_stats.deliveryFailed++;
    // Commands are the only RELIABLE GUI -> controller topic.
    Emit(id == RID_CMD ? "ERROR  command NOT delivered to controller (retries exhausted)"
                       : "ERROR  delivery failed, id " + std::to_string(id));
}

void System::OnSendDropped(size_t)
{
    // Rate-limit the event; the header counter shows every drop.
    if (m_stats.sendDropped++ % 50 == 0)
        Emit("WARN   send queue full, frames dropped (" + std::to_string(m_stats.sendDropped.load()) + " total)");
}

} // namespace pumptron
