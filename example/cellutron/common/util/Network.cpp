#include "Network.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include <iostream>
#include <vector>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace util {

Network::~Network() {
    Shutdown();
}

void Network::Initialize(uint16_t subPort, const std::string& nodeName, const std::string& cpuName) {
    if (m_running) return;

    m_nodeName = nodeName;
    m_subParticipant = std::make_shared<dmq::databus::Participant>(m_subTransport);

    std::cout << "Network: Initializing node '" << m_nodeName << "' on port " << subPort << "..." << std::endl;

    if (m_subTransport.Create(UdpTransport::Type::SUB, "127.0.0.1", subPort) != 0) {
        std::cerr << "Network: ERROR - Failed to create subscriber transport on port " << subPort << std::endl;
        return;
    }

    // Set a short receive timeout so the ReceiverThread can check m_running periodically
    m_subTransport.SetRecvTimeout(std::chrono::milliseconds(100));

    // Initialize thread with CPU name and register for monitoring
    m_thread = std::make_unique<Thread>(m_nodeName + "_NetworkThread", 100, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, cpuName);
    ThreadMonitor::Register(m_thread.get());

#ifndef DMQ_THREAD_STDLIB
    m_thread->SetThreadPriority(PRIORITY_NETWORK);
#endif

    m_running = true;
    
    // Enable watchdog for this thread. ProcessIncoming() timeout ensures periodic check-ins.
    m_thread->CreateThread(WATCHDOG_TIMEOUT);

    // Initialize Bridges for dmq-spy and dmq-monitor
    SpyBridge::Start("127.0.0.1", 9999);
    NodeBridge::StartMulticast(m_nodeName, "239.1.1.1", 9998);

    // Post the receiver loop to the standardized worker thread
    (void)dmq::MakeDelegate(this, &Network::ReceiverThread, *m_thread).AsyncInvoke();
    
    std::cout << "Network: Receiver thread started on port " << subPort << std::endl;
}

void Network::Shutdown() {
    if (!m_running) return;

    std::cout << "Network: Shutting down..." << std::endl;
    m_running = false;
    m_subTransport.Close();
    if (m_thread) {
        m_thread->ExitThread();
    }

    m_remoteNodes.clear();
}

void Network::AddRemoteNode(const std::string& nodeName, const std::string& addr, uint16_t port) {
    std::cout << "Network: Adding remote node '" << nodeName << "' at " << addr << ":" << port << "..." << std::endl;
    
    auto rawTransport = std::make_unique<UdpTransport>();
    if (rawTransport->Create(UdpTransport::Type::PUB, addr.c_str(), port) == 0) {
        
        RemoteNode node;
        node.rawTransport = std::move(rawTransport);

        // 1. Create Reliability Stack for this node
        // TransportMonitor tracks sequence numbers and detects timeouts
        node.transportMonitor = std::make_unique<TransportMonitor>();
        
        // RetryMonitor handles the re-sending of binary data on timeout
        node.retryMonitor = std::make_unique<RetryMonitor>(*node.rawTransport, *node.transportMonitor);
        
        // ReliableTransport is a decorator that routes Send() through the RetryMonitor
        node.reliableTransport = std::make_unique<ReliableTransport>(*node.rawTransport, *node.retryMonitor);

        // 2. Setup Participants
        // One for reliable topics, one for unreliable
        node.reliableParticipant = std::make_shared<dmq::databus::Participant>(*node.reliableTransport);
        node.unreliableParticipant = std::make_shared<dmq::databus::Participant>(*node.rawTransport);

        // 3. Connect raw transport to the monitor for sequence tracking
        node.rawTransport->SetTransportMonitor(node.transportMonitor.get());

        // 4. Apply all outgoing topic-RID mappings registered so far
        for (const auto& out : m_outgoingTopics) {
            if (out.reliability == Reliability::RELIABLE) {
                node.reliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
            } else {
                node.unreliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
            }
        }

        // 5. Register participants with DataBus for global distribution
        dmq::databus::DataBus::AddParticipant(node.reliableParticipant);
        dmq::databus::DataBus::AddParticipant(node.unreliableParticipant);

        m_remoteNodes[nodeName] = std::move(node);
        
        std::cout << "Network: SUCCESS - Remote node '" << nodeName << "' added to DataBus with Reliability Support." << std::endl;
    } else {
        std::cerr << "Network: ERROR - Failed to connect to node " << nodeName << " at " << addr << ":" << port << std::endl;
    }
}

void Network::ReceiverThread() {
    // Process a single batch of incoming network data. This call blocks until 
    // a packet is received or the transport timeout (100ms) expires.
    if (m_subParticipant) {
        int result = m_subParticipant->ProcessIncoming();
        if (result != 0 && result != -1) { 
            std::cerr << "Network: ProcessIncoming returned error " << result << std::endl;
        }
    }

    // Periodically process reliable transport monitors for timeouts/retries.
    // We throttle this to once every ~500ms to reduce CPU load and lock contention
    // on the NetworkThread.
    static auto lastTimeoutCheck = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - lastTimeoutCheck >= std::chrono::milliseconds(500)) {
        for (auto& [name, node] : m_remoteNodes) {
            if (node.transportMonitor) {
                node.transportMonitor->Process();
            }
        }
        lastTimeoutCheck = now;
    }

    // Yield and re-dispatch self to the standardized worker thread. 
    //
    // CRITICAL DESIGN PATTERN: In a DelegateMQ Active Object thread, we avoid 
    // infinite while(true) loops. Instead, we perform one unit of work and then 
    // AsyncInvoke() ourselves again. This returns control to the dmq::os::Thread 
    // dispatcher, allowing it to process other high-priority messages in the 
    // queue (like internal Watchdog Heartbeats or Shutdown requests) before 
    // starting the next network receive cycle.
    //
    // The "loop" speed is governed by the transport receive timeout (100ms).
    if (m_running) {
        Thread::Sleep(std::chrono::milliseconds(10));
        (void)dmq::MakeDelegate(this, &Network::ReceiverThread, *m_thread).AsyncInvoke();
    }
}

} // namespace util
} // namespace cellutron
