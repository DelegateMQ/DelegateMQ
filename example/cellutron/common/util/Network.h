#ifndef NETWORK_H
#define NETWORK_H

#include "DelegateMQ.h"
#include "SpyBridge.h"
#include "NodeBridge.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cellutron {
namespace util {

#if defined(DMQ_TRANSPORT_WIN32_UDP)
    using UdpTransport = dmq::transport::Win32UdpTransport;
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
    using UdpTransport = dmq::transport::LinuxUdpTransport;
#endif

class Network {
public:
    enum class Reliability {
        UNRELIABLE,
        RELIABLE
    };

    static Network& GetInstance() {
        static Network instance;
        return instance;
    }

    /// Initialize network and start receiving.
    void Initialize(uint16_t subPort, const std::string& nodeName = "Unknown", const std::string& cpuName = "");

    /// Stop network.
    void Shutdown();

    /// Add a remote node to the network.
    void AddRemoteNode(const std::string& nodeName, const std::string& addr, uint16_t port);

    /// Register a serializer for an outgoing topic.
    template <typename T>
    void RegisterOutgoingTopic(const std::string& topic, dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, Reliability reliability = Reliability::UNRELIABLE) {
        dmq::databus::DataBus::RegisterSerializer<T>(topic, serializer);
        m_outgoingTopics.push_back({topic, remoteId, reliability});
        
        // Add to all existing remote nodes
        for (auto& [name, node] : m_remoteNodes) {
            if (reliability == Reliability::RELIABLE) {
                node.reliableParticipant->AddRemoteTopic(topic, remoteId);
            } else {
                node.unreliableParticipant->AddRemoteTopic(topic, remoteId);
            }
        }
    }

    /// Register an incoming topic from the network.
    template <typename T>
    void RegisterIncomingTopic(const std::string& topic, dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer) {
        if (m_subParticipant) {
            dmq::databus::DataBus::AddIncomingTopic<T>(topic, remoteId, *m_subParticipant, serializer);
        }
    }

private:
    Network() = default;
    ~Network();

    void ReceiverThread();

    bool m_running = false;
    UdpTransport m_subTransport;
    std::shared_ptr<dmq::databus::Participant> m_subParticipant;
    
    // Standardized thread name for Active Object subsystem. 
    std::unique_ptr<dmq::os::Thread> m_thread;

    struct RemoteNode {
        std::unique_ptr<UdpTransport> rawTransport;
        std::unique_ptr<dmq::util::TransportMonitor> transportMonitor;
        std::unique_ptr<dmq::util::RetryMonitor> retryMonitor;
        std::unique_ptr<dmq::util::ReliableTransport> reliableTransport;

        
        std::shared_ptr<dmq::databus::Participant> reliableParticipant;
        std::shared_ptr<dmq::databus::Participant> unreliableParticipant;
    };

    struct OutgoingTopic {
        std::string topic;
        dmq::DelegateRemoteId remoteId;
        Reliability reliability;
    };

    std::unordered_map<std::string, RemoteNode> m_remoteNodes;
    std::vector<OutgoingTopic> m_outgoingTopics;
    std::string m_nodeName;
};

} // namespace util
} // namespace cellutron

#endif
