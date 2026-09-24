#ifndef PUMPTRON_GUI_SYSTEM_H
#define PUMPTRON_GUI_SYSTEM_H

#include "DelegateMQ.h"
#include <atomic>
#include <optional>
#include <string>

#if defined(PUMPTRON_HAVE_SERIAL)
    #include "util/SerialLink.h"
    #include "port/transport/serial/SerialTransport.h"
#endif

#if defined(_WIN32)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
#else
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
#endif

namespace pumptron {

/// @brief Top-level coordinator for the GUI node.
///
/// Owns whichever link was selected on the command line -- a SerialLink to the
/// real STM32F4 board (when built with libserialport, PUMPTRON_HAVE_SERIAL),
/// or a UDP NetworkNode to the FreeRTOS simulator -- plus
/// the GUI heartbeat and the Timer::ProcessTimers() pump. Both links are wired
/// by the same ConfigureGuiLink() (Topology.h), so nothing above this class
/// can tell them apart.
class System {
public:
    enum class LinkType { SERIAL, UDP };

    struct Options {
        LinkType    link = LinkType::SERIAL;
        std::string serialPort;
        int         baud = 0;
    };

    /// Link health counters, updated from link signals, read by the UI.
    struct LinkStats {
        std::atomic<uint32_t> acked{0};           ///< RELIABLE frames ACKed by the controller
        std::atomic<uint32_t> retries{0};         ///< RELIABLE frames that timed out and were resent
        std::atomic<uint32_t> deliveryFailed{0};  ///< RELIABLE frames abandoned after all retries
        std::atomic<uint32_t> sendDropped{0};     ///< Frames dropped by a full send queue
        std::atomic<uint32_t> busErrors{0};       ///< DataBus errors + unhandled publishes
    };

    /// Human-readable error/status events (DataBus errors, delivery failures,
    /// link backlog, drops). Fired on the reporting thread; the UI shows them
    /// in its Events pane.
    dmq::Signal<void(const std::string&)> OnEvent;

    static System& GetInstance() {
        static System instance;
        return instance;
    }

    /// Open the link and start background threads.
    /// @return false (with a reason in `error`) if the link could not be opened.
    bool Initialize(const Options& options, std::string& error);

    void Shutdown();

    const LinkStats& GetLinkStats() const { return m_stats; }
    const std::string& GetLinkDescription() const { return m_linkDescription; }

    /// Suspend/resume the GUI heartbeat (self-test: simulate a lost GUI).
    void SetHeartbeatEnabled(bool enabled) { m_heartbeatEnabled = enabled; }

private:
#if defined(_WIN32)
    using UdpLink = dmq::databus::NetworkNode<dmq::transport::Win32UdpTransport>;
#else
    using UdpLink = dmq::databus::NetworkNode<dmq::transport::LinuxUdpTransport>;
#endif
#if defined(PUMPTRON_HAVE_SERIAL)
    using SerialLinkT = SerialLink<dmq::transport::SerialTransport>;
#endif

    System();
    ~System() = default;

    System(const System&) = delete;
    System& operator=(const System&) = delete;

    template <typename Link>
    void ConnectLinkSignals(Link& link);

    void TimerLoop();
    void OnHeartbeatTick();

    void OnSendStatus(const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq,
                      dmq::util::TransportMonitor::Status status);
    void OnDeliveryFailed(const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq);
    void OnSendDropped(size_t depth);
    void OnCapExceeded(const dmq::xstring& peer, size_t count);
    void OnPendingExceeded(const dmq::xstring& peer, size_t remaining);
    void OnBusError(const dmq::xstring& topic, dmq::DelegateError error);
    void OnUnhandled(const dmq::xstring& topic);
    void Emit(const std::string& text);

    dmq::os::Thread m_thread;       ///< Heartbeat publisher
    dmq::os::Thread m_timerThread;  ///< Drives Timer::ProcessTimers()
    std::atomic<bool> m_timerRunning{false};

#if defined(PUMPTRON_HAVE_SERIAL)
    std::optional<SerialLinkT> m_serial;
#endif
    std::optional<UdpLink>     m_udp;
    std::string                m_linkDescription;
    LinkStats                  m_stats;

    dmq::util::Timer      m_heartbeatTimer;
    dmq::ScopedConnection m_heartbeatConn;
    uint32_t              m_heartbeatCount = 0;
    std::atomic<bool>     m_heartbeatEnabled{true};

    dmq::ScopedConnection m_sendStatusConn;
    dmq::ScopedConnection m_deliveryFailedConn;
    dmq::ScopedConnection m_sendDroppedConn;
    dmq::ScopedConnection m_capExceededConn;
    dmq::ScopedConnection m_pendingExceededConn;
    dmq::ScopedConnection m_busErrorConn;
    dmq::ScopedConnection m_unhandledConn;
};

} // namespace pumptron

#endif
