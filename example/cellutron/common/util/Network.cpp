#include "Network.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include "extras/util/TimerDelegate.h"
#include <iostream>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace util {

Network::~Network() {
    Shutdown();
}

void Network::Initialize(uint16_t subPort, uint16_t tcpPort, const dmq::xstring& nodeName, const dmq::xstring& cpuName) {
    if (m_running) return;

    m_nodeName = nodeName;

    std::cout << "Network: Initializing node '" << m_nodeName << "' (UDP:" << subPort << ", TCP:" << tcpPort << ")..." << std::endl;

    // 1. Setup UDP Telemetry Subscriber
    if (m_subTransport.Create(UdpTransport::Type::SUB, "127.0.0.1", subPort) != 0) {
        std::cerr << "Network: ERROR - Failed to create subscriber transport on port " << subPort << std::endl; 
        return;
    }
    m_subTransport.SetRecvTimeout(std::chrono::milliseconds(1));
    m_subParticipant = dmq::xmake_shared<dmq::databus::Participant>(m_subTransport);
    for (size_t i = 0; i < m_incomingTopicCount; ++i)
        m_incomingTopics[i].adder(m_incomingTopics[i].serializer, m_incomingTopics[i].topic, m_incomingTopics[i].remoteId, *m_subParticipant);

    // 2. Setup TCP Command Server (if port provided)
    if (tcpPort > 0) {
        if (m_tcpServerTransport.Create(TcpTransport::Type::SERVER, "127.0.0.1", tcpPort) != 0) {
             std::cerr << "Network: ERROR - Failed to create TCP server on port " << tcpPort << std::endl;      
        } else {
             m_tcpServerTransport.SetRecvTimeout(std::chrono::milliseconds(1));
             std::cout << "Network: TCP Server listening on port " << tcpPort << std::endl;
             m_tcpServerParticipant = dmq::xmake_shared<dmq::databus::Participant>(m_tcpServerTransport);
             for (size_t i = 0; i < m_incomingTopicCount; ++i)
                 m_incomingTopics[i].adder(m_incomingTopics[i].serializer, m_incomingTopics[i].topic, m_incomingTopics[i].remoteId, *m_tcpServerParticipant);
        }
    }

    // Initialize thread with CPU name and register for monitoring
    std::string threadName(m_nodeName.c_str());
    threadName += "_NetworkThread";
    m_thread = std::make_unique<Thread>(threadName.c_str(), 100, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, cpuName.c_str());
    ThreadMonitor::Register(m_thread.get());

#ifndef DMQ_THREAD_STDLIB
    m_thread->SetThreadPriority(PRIORITY_NETWORK);
#endif

    m_running = true;

    // Enable watchdog for this thread. ProcessIncoming() timeout ensures periodic check-ins.
    m_thread->CreateThread(WATCHDOG_TIMEOUT);

    // Initialize Bridges for dmq-spy and dmq-monitor
    SpyBridge::Start("127.0.0.1", 9999, m_nodeName.c_str());
    NodeBridge::StartMulticast(m_nodeName.c_str(), "239.1.1.1", 9998);

    // Setup receiver timer (Standard loop inside thread context)
    // Use MakeTimerDelegate to prevent queue flooding if the NetworkThread is busy.
    m_recvConn = m_recvTimer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &Network::ReceiverThread, *m_thread));
    m_recvTimer.Start(TIMER_TICK_PERIOD);
}
void Network::Shutdown() {
    if (!m_running) return;

    std::cout << "Network: Shutting down..." << std::endl;
    m_running = false;
    m_recvTimer.Stop();
    m_recvConn.Disconnect();
    m_subTransport.Close();
    m_tcpServerTransport.Close();
    if (m_thread) {
        m_thread->ExitThread();
    }

    m_remoteNodes.clear();
}

void Network::AddRemoteNode(const dmq::xstring& nodeName, const dmq::xstring& addr, uint16_t udpPort, uint16_t tcpPort) {
    std::cout << "Network: Adding remote node '" << nodeName << "' (UDP:" << udpPort << ", TCP:" << tcpPort << ")..." << std::endl;
    
    RemoteNode node;

    // 1. Setup UDP Pipeline
    node.rawTransport = std::make_unique<UdpTransport>();
    if (node.rawTransport->Create(UdpTransport::Type::PUB, addr.c_str(), udpPort) == 0) {
        node.transportMonitor = std::make_unique<TransportMonitor>();
        node.capExceededConn = node.transportMonitor->OnCapExceeded.Connect(
            dmq::MakeDelegate([nodeName](size_t size) {
                std::cerr << "Network: [" << nodeName << "] TransportMonitor cap exceeded ("
                          << size << " unacked messages). Comm link may be down." << std::endl;
            }));
        node.pendingExceededConn = node.transportMonitor->OnPendingExceeded.Connect(
            dmq::MakeDelegate([nodeName](size_t remaining) {
                std::cerr << "Network: [" << nodeName << "] TransportMonitor pending exceeded ("
                          << remaining << " expired entries remain). Process() call rate may be too low." << std::endl;
            }));
        node.retryMonitor = std::make_unique<RetryMonitor>(*node.rawTransport, *node.transportMonitor);
        node.reliableTransport = std::make_unique<ReliableTransport>(*node.rawTransport, *node.retryMonitor);
        node.reliableParticipant = dmq::xmake_shared<dmq::databus::Participant>(*node.reliableTransport);
        node.unreliableParticipant = dmq::xmake_shared<dmq::databus::Participant>(*node.rawTransport);
        
        node.reliableParticipant->SetSendThread(m_thread.get());
        node.unreliableParticipant->SetSendThread(m_thread.get());
        dmq::databus::DataBus::AddParticipant(node.reliableParticipant);
        dmq::databus::DataBus::AddParticipant(node.unreliableParticipant);
    }

    // 2. Setup TCP Pipeline (Client)
    if (tcpPort > 0) {
        node.tcpTransport = std::make_unique<TcpTransport>();
        if (node.tcpTransport->Create(TcpTransport::Type::CLIENT, addr.c_str(), tcpPort) == 0) {
            node.tcpTransport->SetRecvTimeout(std::chrono::milliseconds(1));
            node.tcpParticipant = dmq::xmake_shared<dmq::databus::Participant>(*node.tcpTransport);
            node.tcpParticipant->SetSendThread(m_thread.get());
            dmq::databus::DataBus::AddParticipant(node.tcpParticipant);
            
            // Register all existing incoming topics on this new TCP client connection
            for (size_t i = 0; i < m_incomingTopicCount; ++i)
                m_incomingTopics[i].adder(m_incomingTopics[i].serializer, m_incomingTopics[i].topic, m_incomingTopics[i].remoteId, *node.tcpParticipant);

            std::cout << "Network: TCP Client connected to " << nodeName << std::endl;
        }
    }

    // 3. Apply all outgoing topic-RID mappings
    for (size_t i = 0; i < m_outgoingTopicCount; ++i) {
        const auto& out = m_outgoingTopics[i];
        if (out.reliability == Reliability::TCP && node.tcpParticipant) {
            node.tcpParticipant->AddRemoteTopic(out.topic, out.remoteId);
        } else if (out.reliability == Reliability::RELIABLE) {
            node.reliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
        } else {
            node.unreliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
        }
    }

    if (m_remoteNodes.size() >= MAX_REMOTE_NODES)
        ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
    else
        m_remoteNodes[nodeName] = std::move(node);
}

void Network::ReceiverThread() {
    // Process multiple units of work per cycle to clear backlogs efficiently
    const int MAX_WORK_PER_CYCLE = 20;

    // 1. Process UDP Telemetry
    if (m_subParticipant) {
        for (int i = 0; i < MAX_WORK_PER_CYCLE; ++i) {
            if (m_subParticipant->ProcessIncoming() != 0) break;
        }
    }

    // 2. Process TCP Commands (Server)
    if (m_tcpServerParticipant) {
        for (int i = 0; i < MAX_WORK_PER_CYCLE; ++i) {
            if (m_tcpServerParticipant->ProcessIncoming() != 0) break;
        }
    }

    // 3. Process TCP Client connections
    for (auto& [name, node] : m_remoteNodes) {
        if (node.tcpParticipant) {
            for (int i = 0; i < MAX_WORK_PER_CYCLE; ++i) {
                if (node.tcpParticipant->ProcessIncoming() != 0) break;
            }
        }
    }

    // 4. Process reliable transport monitors
    static auto lastTimeoutCheck = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - lastTimeoutCheck >= std::chrono::milliseconds(100)) {
        for (auto& [name, node] : m_remoteNodes) {
            if (node.transportMonitor) {
                node.transportMonitor->Process();
            }
        }
        lastTimeoutCheck = now;
    }
}

} // namespace util
} // namespace cellutron
