/// @file DmqInterop.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief C++ implementation of the DelegateMQ Native Interop DLL.
///
/// @details This DLL wraps the C++ DelegateMQ core to provide a C-compatible
/// API for languages like C# and Python.
///
/// Outgoing messages use the same reliability stack as NetworkNode's RELIABLE
/// mode: ReliableTransport -> RetryMonitor -> TransportMonitor over the raw send
/// transport. A dedicated send-monitor thread drains ACKs and runs
/// TransportMonitor::Process(), so ACK handling isn't held up by the recv
/// channel's blocking receive.

#include <iostream>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <sstream>
#include <string>

// DelegateMQ core includes
#include "delegate/IDispatcher.h"  // For DelegateRemoteId and ACK_REMOTE_ID
#include "delegate/Signal.h"

// Port and Extra includes
#include "port/transport/common/DmqHeader.h"
#include "extras/util/NetworkConnect.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"

#include "DmqInterop.h"

// ---------------------------------------------------------------------------
// Transport Selection based on DMQ_TRANSPORT_* defines
// ---------------------------------------------------------------------------

// The recv (incoming/subscribe) and send (outgoing/command) channels use distinct
// transport types for the UDP ports: recv joins a multicast group so any number of
// clients can receive the same stream concurrently, while send stays a plain unicast
// socket back to the single remote peer. ZeroMQ's PUB/SUB sockets already fan out to
// multiple subscribers without OS-level port sharing, so both channels share one type.
#if defined(DMQ_TRANSPORT_ZEROMQ)
    #include "port/transport/zeromq/ZeroMqTransport.h"
    using SendTransportType = dmq::transport::ZeroMqTransport;
    using RecvTransportType = dmq::transport::ZeroMqTransport;
#elif defined(DMQ_TRANSPORT_WIN32_UDP)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    #include "port/transport/win32-udp/MulticastTransport.h"
    using SendTransportType = dmq::transport::Win32UdpTransport;
    using RecvTransportType = dmq::transport::MulticastTransport;
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    #include "port/transport/linux-udp/MulticastTransport.h"
    using SendTransportType = dmq::transport::LinuxUdpTransport;
    using RecvTransportType = dmq::transport::MulticastTransport;
#else
    #error "DmqInterop requires a supported network transport (UDP or ZeroMQ)."
#endif

using namespace dmq::transport;
using namespace dmq::util;

namespace {
    // How often the send-monitor thread runs TransportMonitor::Process(),
    // matching NetworkNode.
    constexpr auto MONITOR_PROCESS_INTERVAL = std::chrono::milliseconds(100);

    // ---- Process-wide settings and callbacks (survive Start/Stop) ----------

    /// A registered callback and the caller's context, passed back unchanged.
    template <typename Fn>
    struct Registered {
        Fn fn = nullptr;
        void* context = nullptr;
    };

    std::mutex g_cbMutex;
    std::map<uint16_t, Registered<DmqMessageCallback>> g_msgCallbacks;
    Registered<DmqStatusCallback> g_statusCallback;
    Registered<DmqErrorCallback> g_errorCallback;

    struct ReliabilityConfig {
        bool enabled = true;
        int timeoutMs = 0;      // <= 0: library default
        int maxRetries = -1;    // < 0: library default
    };
    ReliabilityConfig g_reliability;   // guarded by g_lifecycleMutex

    /// @brief Thread-safe helper to invoke the user's error callback.
    /// @details Copies the callback pointer under lock and calls it outside,
    /// so the callback may re-register itself without deadlocking.
    void RaiseError(DmqErrorCode code, uint16_t remoteId, const std::string& msg) {
        Registered<DmqErrorCallback> cb;
        {
            std::lock_guard<std::mutex> lock(g_cbMutex);
            cb = g_errorCallback;
        }
        if (cb.fn) {
            cb.fn(cb.context, static_cast<int>(code), remoteId, msg.c_str());
        } else {
            std::cerr << "DmqInterop Error " << static_cast<int>(code) << " (no callback): " << msg << std::endl;
        }
    }

    void RaiseStatus(uint16_t remoteId, uint16_t seqNum, DmqSendStatus status) {
        Registered<DmqStatusCallback> cb;
        {
            std::lock_guard<std::mutex> lock(g_cbMutex);
            cb = g_statusCallback;
        }
        if (cb.fn) {
            cb.fn(cb.context, remoteId, seqNum, static_cast<int>(status));
        }
    }

    /// @brief Native transports, reliability stack and background threads for one
    /// Start()/Stop() session.
    struct InteropState {
        NetworkContext netContext;
        std::unique_ptr<RecvTransportType> recvTransport;
        std::unique_ptr<SendTransportType> sendTransport;

        // Reliability stack (null when reliability is disabled). Declared after the
        // transports so it is destroyed before them.
        std::unique_ptr<TransportMonitor> monitor;
        std::unique_ptr<RetryMonitor> retry;
        std::unique_ptr<ReliableTransport> reliableTransport;
        dmq::ScopedConnection statusConn;
        dmq::ScopedConnection deliveryFailedConn;
        dmq::ScopedConnection capConn;
        dmq::ScopedConnection pendingConn;

        std::thread recvThread;
        std::thread sendMonitorThread;
        std::atomic<bool> running{false};
        std::atomic<uint16_t> nextSeqNum{1};

        ~InteropState() {
            Stop();
        }

        /// @brief Shuts down threads and transports.
        /// @details Stops the send-monitor thread first so no retries or status
        /// callbacks race with closing the sockets, then closes the transports to
        /// unblock the recv thread's receive.
        void Stop() {
            if (!running.exchange(false))
                return;
            if (sendMonitorThread.joinable()) sendMonitorThread.join();
            statusConn.Disconnect();
            deliveryFailedConn.Disconnect();
            capConn.Disconnect();
            pendingConn.Disconnect();
            if (recvTransport) recvTransport->Close();
            if (sendTransport) sendTransport->Close();
            if (recvThread.joinable()) recvThread.join();
        }

        void RecvLoop();
        void SendMonitorLoop();
    };

    // Current session. Swapped under g_stateMutex; Send() takes a reference so a
    // concurrent Stop() can't destroy the state out from under it.
    std::shared_ptr<InteropState> g_state;
    std::mutex g_stateMutex;

    // Serializes Start(), Stop() and SetReliability().
    std::mutex g_lifecycleMutex;

    std::shared_ptr<InteropState> GetState() {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        return g_state;
    }

    /// @brief Receives incoming data and dispatches it to registered callbacks.
    void InteropState::RecvLoop() {
        while (running) {
            try {
                dmq::xstringstream is(std::ios::in | std::ios::out | std::ios::binary);
                DmqHeader header;

                // Receive blocks up to the transport's receive timeout
                if (recvTransport->Receive(is, header) == 0) {
                    // ACKs are consumed by the transport (TransportMonitor::Remove)
                    if (header.GetId() != dmq::ACK_REMOTE_ID) {
                        std::string payload = is.str();
                        Registered<DmqMessageCallback> cb;
                        {
                            std::lock_guard<std::mutex> lock(g_cbMutex);
                            auto it = g_msgCallbacks.find(header.GetId());
                            if (it != g_msgCallbacks.end()) {
                                cb = it->second;
                            }
                        }
                        if (cb.fn) {
                            cb.fn(cb.context, header.GetId(), (const uint8_t*)payload.data(), (uint32_t)payload.size());
                        }
                    }
                }
            } catch (const std::exception& e) {
                RaiseError(DMQ_ERR_EXCEPTION, 0, std::string("Exception in receive loop: ") + e.what());
                // Prevent tight-looping on persistent errors
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                RaiseError(DMQ_ERR_EXCEPTION, 0, "Unknown exception in receive loop");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    /// @brief Drains ACKs and drives timeouts/retries for outgoing messages.
    /// @details Peers send ACKs to the source address they received from -- the
    /// send socket's OS-assigned port -- so for UDP this thread reads that socket;
    /// Receive() calls TransportMonitor::Remove() on each ACK. Process() then fires
    /// timeouts, which RetryMonitor turns into resends or a final delivery failure.
    void InteropState::SendMonitorLoop() {
        auto lastProcess = dmq::Clock::now();
        while (running) {
            try {
#if !defined(DMQ_TRANSPORT_ZEROMQ)
                // Short recv timeout on the send socket (2 ms) keeps this loop responsive.
                // ZeroMQ PUB sockets can't receive; there ACKs arrive on the recv socket.
                for (int i = 0; i < 16; ++i) {
                    DmqHeader ackHeader;
                    dmq::xstringstream ackStream(std::ios::in | std::ios::out | std::ios::binary);
                    if (sendTransport->Receive(ackStream, ackHeader) != 0) break;
                }
#endif
                // Paces the loop when Receive() returns at once instead of waiting its
                // timeout -- e.g. on Windows, before the first send binds the socket.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                auto now = dmq::Clock::now();
                if (now - lastProcess >= MONITOR_PROCESS_INTERVAL) {
                    monitor->Process();
                    lastProcess = now;
                }
            } catch (const std::exception& e) {
                RaiseError(DMQ_ERR_EXCEPTION, 0, std::string("Exception in send-monitor loop: ") + e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                RaiseError(DMQ_ERR_EXCEPTION, 0, "Unknown exception in send-monitor loop");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

// Provide implementation for the library's fault and watchdog handlers.
// Since we filtered out Fault.cpp, we must provide these to avoid linker errors.
namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line) {
        std::stringstream ss;
        ss << "DelegateMQ Fault at " << file << ":" << line;
        RaiseError(DMQ_ERR_FAULT, 0, ss.str());
        std::cerr << ss.str() << std::endl;
        abort();
    }
}

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line) {
    dmq::util::FaultHandler(file, line);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName) {
    std::stringstream ss;
    ss << "DelegateMQ Watchdog Expired: " << threadName;
    RaiseError(DMQ_ERR_WATCHDOG, 0, ss.str());
    std::cerr << ss.str() << std::endl;
    abort();
}

extern "C" {
    int DMQ_CALL DmqInterop_SetReliability(int enabled, int timeoutMs, int maxRetries) {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        if (GetState()) return -1;
        g_reliability.enabled = enabled != 0;
        g_reliability.timeoutMs = timeoutMs;
        g_reliability.maxRetries = maxRetries;
        return 0;
    }

    /// @brief Initialize and start the transport system and background threads.
    /// @return 0 on success, -1 on failure (e.g. already started or bind error).
    int DMQ_CALL DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort, const char* multicastGroup) {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        if (GetState()) return -1;

        try {
            auto state = std::make_shared<InteropState>();
            state->recvTransport = std::make_unique<RecvTransportType>();
            state->sendTransport = std::make_unique<SendTransportType>();

#if defined(DMQ_TRANSPORT_ZEROMQ)
            // ZeroMQ expects a full address string. Client side connects both:
            // SUB to listen, PUB to send.
            std::string recvAddr = "tcp://" + std::string(remoteHost) + ":" + std::to_string(recvPort);
            std::string sendAddr = "tcp://" + std::string(remoteHost) + ":" + std::to_string(sendPort);
            if (state->recvTransport->Create(RecvTransportType::Type::SUB, recvAddr.c_str()) != 0) {
                RaiseError(DMQ_ERR_TRANSPORT, 0, "Failed to create recv transport for " + recvAddr);
                return -1;
            }
            if (state->sendTransport->Create(SendTransportType::Type::PUB, sendAddr.c_str()) != 0) {
                RaiseError(DMQ_ERR_TRANSPORT, 0, "Failed to create send transport for " + sendAddr);
                return -1;
            }
#else
            // Recv joins the multicast group on recvPort so any number of clients can
            // receive the same stream concurrently. Send stays a unicast socket back to
            // the single remote peer (e.g. commands to a server), which needs no fan-out.
            std::string localIP = NetworkContext::GetLocalAddress();
            if (state->recvTransport->Create(RecvTransportType::Type::SUB, multicastGroup, static_cast<uint16_t>(recvPort), localIP.c_str()) != 0) {
                RaiseError(DMQ_ERR_TRANSPORT, 0, "Failed to join multicast group " + std::string(multicastGroup)
                    + " on port " + std::to_string(recvPort));
                return -1;
            }
            if (state->sendTransport->Create(SendTransportType::Type::PUB, remoteHost, static_cast<uint16_t>(sendPort)) != 0) {
                RaiseError(DMQ_ERR_TRANSPORT, 0, "Failed to create send transport to " + std::string(remoteHost)
                    + ":" + std::to_string(sendPort));
                return -1;
            }
#endif

            if (g_reliability.enabled) {
                state->monitor = g_reliability.timeoutMs > 0
                    ? std::make_unique<TransportMonitor>(std::chrono::milliseconds(g_reliability.timeoutMs))
                    : std::make_unique<TransportMonitor>();

                // Connect before RetryMonitor does (it connects in its constructor), so
                // a timeout is reported before the retry or DELIVERY_FAILED it causes.
                state->statusConn = state->monitor->OnSendStatus.Connect(dmq::MakeDelegate(
                    [](dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
                        RaiseStatus(id, seq, status == TransportMonitor::Status::SUCCESS
                            ? DMQ_STATUS_ACKED : DMQ_STATUS_TIMEOUT);
                    }));

                state->retry = std::make_unique<RetryMonitor>(*state->sendTransport, *state->monitor,
                    g_reliability.maxRetries >= 0 ? g_reliability.maxRetries : dmq::RETRY_MONITOR_MAX_RETRIES);
                state->reliableTransport = std::make_unique<ReliableTransport>(*state->sendTransport, *state->retry);

#if defined(DMQ_TRANSPORT_ZEROMQ)
                // ACKs for our sends arrive on the SUB socket; ACKs for received data
                // go out through the PUB socket.
                state->recvTransport->SetTransportMonitor(state->monitor.get());
                state->recvTransport->SetSendTransport(state->sendTransport.get());
#else
                // ACKs come back to the send socket's source port (drained by SendMonitorLoop).
                state->sendTransport->SetTransportMonitor(state->monitor.get());
#endif

                state->deliveryFailedConn = state->retry->OnDeliveryFailed.Connect(dmq::MakeDelegate(
                    [](dmq::DelegateRemoteId id, uint16_t seq) {
                        RaiseStatus(id, seq, DMQ_STATUS_DELIVERY_FAILED);
                    }));
                state->capConn = state->monitor->OnCapExceeded.Connect(dmq::MakeDelegate(
                    [](size_t count) {
                        RaiseError(DMQ_ERR_CAP_EXCEEDED, 0, "Unacknowledged message limit reached ("
                            + std::to_string(count) + " pending); send rejected");
                    }));
                state->pendingConn = state->monitor->OnPendingExceeded.Connect(dmq::MakeDelegate(
                    [](size_t remaining) {
                        RaiseError(DMQ_ERR_PENDING_EXCEEDED, 0, "Timed-out messages accumulating ("
                            + std::to_string(remaining) + " still expired after one pass)");
                    }));
            }

            state->running = true;
            state->recvThread = std::thread(&InteropState::RecvLoop, state.get());
            if (state->monitor)
                state->sendMonitorThread = std::thread(&InteropState::SendMonitorLoop, state.get());

            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_state = std::move(state);
            return 0;
        } catch (const std::exception& e) {
            RaiseError(DMQ_ERR_EXCEPTION, 0, std::string("Exception in DmqInterop_Start: ") + e.what());
            return -1;
        } catch (...) {
            RaiseError(DMQ_ERR_EXCEPTION, 0, "Unknown exception in DmqInterop_Start");
            return -1;
        }
    }

    void DMQ_CALL DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb, void* context) {
        std::lock_guard<std::mutex> lock(g_cbMutex);
        if (cb)
            g_msgCallbacks[remoteId] = { cb, context };
        else
            g_msgCallbacks.erase(remoteId);
    }

    void DMQ_CALL DmqInterop_RegisterStatusCallback(DmqStatusCallback cb, void* context) {
        std::lock_guard<std::mutex> lock(g_cbMutex);
        g_statusCallback = { cb, context };
    }

    void DMQ_CALL DmqInterop_RegisterErrorCallback(DmqErrorCallback cb, void* context) {
        std::lock_guard<std::mutex> lock(g_cbMutex);
        g_errorCallback = { cb, context };
    }

    int DMQ_CALL DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len, uint16_t* seqNum) {
        auto state = GetState();
        if (!state || !state->running) return -1;

        try {
            dmq::xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            os.write((const char*)data, len);

            DmqHeader header;
            header.SetId(remoteId);
            uint16_t seq = state->nextSeqNum++;
            header.SetSeqNum(seq);
            if (seqNum) *seqNum = seq;

            // With reliability, RetryMonitor tracks the message and reports its
            // outcome (including an immediate failure) through the status callback.
            if (state->reliableTransport)
                return state->reliableTransport->Send(os, header);
            return state->sendTransport->Send(os, header);
        } catch (const std::exception& e) {
            RaiseError(DMQ_ERR_EXCEPTION, remoteId, std::string("Exception in DmqInterop_Send: ") + e.what());
            return -1;
        } catch (...) {
            RaiseError(DMQ_ERR_EXCEPTION, remoteId, "Unknown exception in DmqInterop_Send");
            return -1;
        }
    }

    void DMQ_CALL DmqInterop_Stop() {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        std::shared_ptr<InteropState> state;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            state = std::move(g_state);
        }
        // Outside g_stateMutex, so a callback calling DmqInterop_Send while the
        // threads are being joined can't deadlock.
        if (state) state->Stop();
    }
}
