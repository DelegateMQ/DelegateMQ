#ifndef NETWORK_H
#define NETWORK_H

#include "DelegateMQ.h"
#include "SpyBridge.h"
#include "NodeBridge.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"
#include "Constants.h"
#include <string>
#include <memory>
#include <array>

// Explicitly include transport headers OUTSIDE the namespace to prevent nesting errors.
#if defined(_WIN32) || defined(_WIN64)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    #include "port/transport/win32-tcp/Win32TcpTransport.h"
#elif defined(__linux__)
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    #include "port/transport/linux-tcp/LinuxTcpTransport.h"
#endif

namespace cellutron {
namespace util {

#if defined(_WIN32) || defined(_WIN64)
    using UdpTransport = dmq::transport::Win32UdpTransport;
    using TcpTransport = dmq::transport::Win32TcpTransport;
#elif defined(__linux__)
    using UdpTransport = dmq::transport::LinuxUdpTransport;
    using TcpTransport = dmq::transport::LinuxTcpTransport;
#endif

class Network {
public:
    enum class Reliability {
        UNRELIABLE,
        RELIABLE,
        TCP // New reliability mode for TCP-guaranteed delivery
    };

    static Network& GetInstance() {
        static Network instance;
        return instance;
    }

    /// Initialize network and start receiving.
    /// @param subPort UDP port for telemetry
    /// @param tcpPort TCP port for commands (server mode)
    void Initialize(uint16_t subPort, uint16_t tcpPort = 0, const dmq::xstring& nodeName = "Unknown", const dmq::xstring& cpuName = "");

    /// Stop network.
    void Shutdown();

    /// Add a remote node to the network.
    void AddRemoteNode(const dmq::xstring& nodeName, const dmq::xstring& addr, uint16_t udpPort, uint16_t tcpPort = 0);

    /// Register a serializer for an outgoing topic.
    template <typename T>
    void RegisterOutgoingTopic(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, Reliability reliability = Reliability::UNRELIABLE) {
        dmq::databus::DataBus::RegisterSerializer<T>(topic, serializer);
        if (m_outgoingTopicCount >= m_outgoingTopics.size())
            ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
        else
            m_outgoingTopics[m_outgoingTopicCount++] = {topic, remoteId, reliability};
        
        // Add to all existing remote nodes
        for (auto& [name, node] : m_remoteNodes) {
            if (reliability == Reliability::TCP && node.tcpParticipant) {
                node.tcpParticipant->AddRemoteTopic(topic, remoteId);
            } else if (reliability == Reliability::RELIABLE) {
                node.reliableParticipant->AddRemoteTopic(topic, remoteId);
            } else {
                node.unreliableParticipant->AddRemoteTopic(topic, remoteId);
            }
        }
    }

    /// Register an incoming topic from the network.
    template <typename T>
    void RegisterIncomingTopic(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer) {
        if (m_incomingTopicCount >= m_incomingTopics.size())
            ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
        else
            m_incomingTopics[m_incomingTopicCount++] = {topic, remoteId, (void*)&serializer,
                [](void* ser, const dmq::xstring& t, dmq::DelegateRemoteId rid, dmq::databus::Participant& p) {
                    dmq::databus::DataBus::AddIncomingTopic<T>(t, rid, p, *(dmq::ISerializer<void(T)>*)ser);
                }
            };

        if (m_subParticipant) {
            dmq::databus::DataBus::AddIncomingTopic<T>(topic, remoteId, *m_subParticipant, serializer);
        }
        if (m_tcpServerParticipant) {
            dmq::databus::DataBus::AddIncomingTopic<T>(topic, remoteId, *m_tcpServerParticipant, serializer);
        }
        for (auto& [name, node] : m_remoteNodes) {
             if (node.tcpParticipant) {
                 dmq::databus::DataBus::AddIncomingTopic<T>(topic, remoteId, *node.tcpParticipant, serializer);
             }
        }
    }

private:
    Network() = default;
    ~Network();

    void ReceiverThread();

    bool m_running = false;
    
    // UDP Telemetry Receiver
    UdpTransport m_subTransport;
    std::shared_ptr<dmq::databus::Participant> m_subParticipant;

    // TCP Command Receiver (Server)
    TcpTransport m_tcpServerTransport;
    std::shared_ptr<dmq::databus::Participant> m_tcpServerParticipant;
    
    // Standardized thread name for Active Object subsystem. 
    std::unique_ptr<dmq::os::Thread> m_thread;

    struct RemoteNode {
        // UDP Pipeline
        std::unique_ptr<UdpTransport> rawTransport;
        std::unique_ptr<dmq::util::TransportMonitor> transportMonitor;
        std::unique_ptr<dmq::util::RetryMonitor> retryMonitor;
        std::unique_ptr<dmq::util::ReliableTransport> reliableTransport;
        std::shared_ptr<dmq::databus::Participant> reliableParticipant;
        std::shared_ptr<dmq::databus::Participant> unreliableParticipant;

        // TCP Pipeline
        std::unique_ptr<TcpTransport> tcpTransport;
        std::shared_ptr<dmq::databus::Participant> tcpParticipant;
    };

    struct OutgoingTopic {
        dmq::xstring topic;
        dmq::DelegateRemoteId remoteId;
        Reliability reliability;
    };

    typedef void (*IncomingTopicAdder)(void* ser, const dmq::xstring& t, dmq::DelegateRemoteId rid, dmq::databus::Participant& p);
    struct IncomingTopic {
        dmq::xstring topic;
        dmq::DelegateRemoteId remoteId;
        void* serializer;
        IncomingTopicAdder adder;
    };

    xmap<dmq::xstring, RemoteNode> m_remoteNodes;
    std::array<OutgoingTopic, MAX_OUTGOING_TOPICS> m_outgoingTopics{};
    size_t m_outgoingTopicCount = 0;
    std::array<IncomingTopic, MAX_INCOMING_TOPICS> m_incomingTopics{};
    size_t m_incomingTopicCount = 0;
    dmq::xstring m_nodeName;
    dmq::util::Timer m_recvTimer;
    dmq::ScopedConnection m_recvConn;
};

} // namespace util
} // namespace cellutron

#endif
