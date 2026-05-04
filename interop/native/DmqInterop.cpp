/// @file DmqInterop.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
/// 
/// @brief C++ implementation of the DelegateMQ Native Interop DLL.
/// 
/// @details This DLL wraps the C++ DelegateMQ core to provide a C-compatible 
/// API for languages like C# and Python.

#include <iostream>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <sstream>
#include <string>

// DelegateMQ core includes
#include "delegate/IDispatcher.h"  // For DelegateRemoteId and ACK_REMOTE_ID
#include "delegate/Signal.h"

// Port and Extra includes
#include "port/transport/DmqHeader.h"
#include "extras/util/NetworkConnect.h"
#include "extras/util/TransportMonitor.h"

#include "DmqInterop.h"

// ---------------------------------------------------------------------------
// Transport Selection based on DMQ_TRANSPORT_* defines
// ---------------------------------------------------------------------------

#if defined(DMQ_TRANSPORT_ZEROMQ)
    #include "port/transport/zeromq/ZeroMqTransport.h"
    using TransportType = dmq::transport::ZeroMqTransport;
#elif defined(DMQ_TRANSPORT_WIN32_UDP)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    using TransportType = dmq::transport::Win32UdpTransport;
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    using TransportType = dmq::transport::LinuxUdpTransport;
#else
    #error "DmqInterop requires a supported network transport (UDP or ZeroMQ)."
#endif

using namespace dmq::transport;
using namespace dmq::util;

namespace {
    /// @brief RAII container for the native transport state and background threads.
    struct InteropState {
        NetworkContext netContext;
        std::unique_ptr<TransportType> recvTransport;
        std::unique_ptr<TransportType> sendTransport;
        std::unique_ptr<TransportMonitor> monitor;
        std::thread recvThread;
        std::atomic<bool> running{false};
        std::map<uint16_t, DmqMessageCallback> callbacks;
        std::mutex cbMutex;

        ~InteropState() {
            Stop();
        }

        /// @brief Shuts down transports and joins the receive thread.
        void Stop() {
            bool wasRunning = running.exchange(false);
            if (wasRunning) {
                if (recvTransport) recvTransport->Close();
                if (sendTransport) sendTransport->Close();
                if (recvThread.joinable()) recvThread.join();
            }
        }

        void RecvLoop();
    };

    // Global state managed via unique_ptr and protected by g_lifecycleMutex
    std::unique_ptr<InteropState> g_state;

    // Supports registering callbacks before DmqInterop_Start() is called
    std::map<uint16_t, DmqMessageCallback> g_preRegCallbacks;
    std::mutex g_preRegMutex;

    // Error reporting state
    DmqErrorCallback g_errorCallback = nullptr;
    std::mutex g_errorMutex;

    // Synchronizes Start() and Stop() calls
    std::mutex g_lifecycleMutex;

    /// @brief Thread-safe helper to invoke the user's error callback.
    /// @details Copies the callback pointer under lock to avoid deadlocks 
    /// if the callback attempts to re-register itself.
    void RaiseError(const std::string& msg) {
        DmqErrorCallback cb = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_errorMutex);
            cb = g_errorCallback;
        }

        if (cb) {
            cb(msg.c_str());
        } else {
            std::cerr << "DmqInterop Error (No callback): " << msg << std::endl;
        }
    }

    /// @brief The background receive loop that processes incoming packets.
    /// @details Blocks on transport::Receive() and dispatches to registered callbacks.
    void InteropState::RecvLoop() {
        while (running) {
            try {
                dmq::xstringstream is(std::ios::in | std::ios::out | std::ios::binary);
                DmqHeader header;
                
                // Receive blocks (usually with a timeout configured in the transport)
                if (recvTransport->Receive(is, header) == 0) {
                    // Ignore internal ACK packets; only process data
                    if (header.GetId() != dmq::ACK_REMOTE_ID) {
                        std::string payload = is.str();
                        DmqMessageCallback cb = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(cbMutex);
                            auto it = callbacks.find(header.GetId());
                            if (it != callbacks.end()) {
                                cb = it->second;
                            }
                        }
                        if (cb) {
                            cb(header.GetId(), (const uint8_t*)payload.data(), (uint32_t)payload.size());
                        }
                    }
                }
                
                if (monitor) {
                    monitor->Process();
                }
            } catch (const std::exception& e) {
                RaiseError(std::string("Exception in RecvLoop: ") + e.what());
                // Prevent tight-looping on persistent errors
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                RaiseError("Unknown exception in RecvLoop");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

// Provide implementation for the library's fault and watchdog handlers.
// Since we filtered out Fault.cpp, we must provide these to avoid linker errors.
namespace dmq::util {
    void FaultHandler(const char* file, unsigned short line) {
        std::stringstream ss;
        ss << "DelegateMQ Fault at " << file << ":" << line;
        RaiseError(ss.str());
    }
}

extern "C" void FaultHandler(const char* file, unsigned short line) {
    dmq::util::FaultHandler(file, line);
}

extern "C" void WatchdogHandler(const char* threadName) {
    std::stringstream ss;
    ss << "DelegateMQ Watchdog Expired: " << threadName;
    RaiseError(ss.str());
}

extern "C" {
    /// @brief Initialize and start the transport system and background threads.
    /// @return 0 on success, -1 on failure (e.g. already started or bind error).
    int DMQ_CALL DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort) {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        if (g_state) return -1;

        try {
            g_state = std::make_unique<InteropState>();
            
            // Move any pre-registered callbacks into g_state
            {
                std::lock_guard<std::mutex> lock(g_preRegMutex);
                g_state->callbacks = std::move(g_preRegCallbacks);
                g_preRegCallbacks.clear();
            }

            g_state->running = true;

            g_state->recvTransport = std::make_unique<TransportType>();
            g_state->sendTransport = std::make_unique<TransportType>();

#if defined(DMQ_TRANSPORT_ZEROMQ)
            // ZeroMQ expects a full address string.
            // Client side typically CONNECTS both.
            std::string recvAddr = "tcp://" + std::string(remoteHost) + ":" + std::to_string(recvPort);
            std::string sendAddr = "tcp://" + std::string(remoteHost) + ":" + std::to_string(sendPort);

            // Note: ZeroMqTransport uses Type::SUB for connecting to a PUB server
            // and Type::PAIR_CLIENT or Type::PUB for sending.
            // For Databus interop, we use SUB to listen and PUB to send.
            if (g_state->recvTransport->Create(TransportType::Type::SUB, recvAddr.c_str()) != 0) {
                g_state.reset();
                return -1;
            }
            if (g_state->sendTransport->Create(TransportType::Type::PUB, sendAddr.c_str()) != 0) {
                g_state.reset();
                return -1;
            }
#else
            // UDP Transport: host and port are separate
            if (g_state->recvTransport->Create(TransportType::Type::SUB, "", recvPort) != 0) {
                g_state.reset();
                return -1;
            }
            if (g_state->sendTransport->Create(TransportType::Type::PUB, remoteHost, sendPort) != 0) {
                g_state.reset();
                return -1;
            }
#endif

            g_state->monitor = std::make_unique<TransportMonitor>();

            // Route ACKs from the receive transport back through the send transport
            g_state->recvTransport->SetSendTransport(g_state->sendTransport.get());
            g_state->recvTransport->SetTransportMonitor(g_state->monitor.get());
            
            g_state->sendTransport->SetTransportMonitor(g_state->monitor.get());

            g_state->recvThread = std::thread(&InteropState::RecvLoop, g_state.get());

            return 0;
        } catch (const std::exception& e) {
            RaiseError(std::string("Exception in DmqInterop_Start: ") + e.what());
            g_state.reset();
            return -1;
        } catch (...) {
            RaiseError("Unknown exception in DmqInterop_Start");
            g_state.reset();
            return -1;
        }
    }

    /// @brief Register a callback for a specific Remote ID.
    /// @details Supports registration before Start() is called.
    void DMQ_CALL DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb) {
        if (g_state) {
            std::lock_guard<std::mutex> lock(g_state->cbMutex);
            g_state->callbacks[remoteId] = cb;
        } else {
            std::lock_guard<std::mutex> lock(g_preRegMutex);
            g_preRegCallbacks[remoteId] = cb;
        }
    }

    /// @brief Register a global callback for internal library errors and faults.
    void DMQ_CALL DmqInterop_RegisterErrorCallback(DmqErrorCallback cb) {
        std::lock_guard<std::mutex> lock(g_errorMutex);
        g_errorCallback = cb;
    }

    /// @brief Send raw bytes to a Remote ID.
    /// @return 0 on success, -1 on failure.
    int DMQ_CALL DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len) {
        if (!g_state || !g_state->sendTransport) return -1;

        try {
            dmq::xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            os.write((const char*)data, len);

            // Framing handled by the transport/header system
            static std::atomic<uint16_t> seqNum{1};
            DmqHeader header;
            header.SetId(remoteId);
            header.SetSeqNum(seqNum++);

            return g_state->sendTransport->Send(os, header);
        } catch (const std::exception& e) {
            RaiseError(std::string("Exception in DmqInterop_Send: ") + e.what());
            return -1;
        } catch (...) {
            RaiseError("Unknown exception in DmqInterop_Send");
            return -1;
        }
    }

    /// @brief Stop the transport and cleanup all native resources.
    void DMQ_CALL DmqInterop_Stop() {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        if (!g_state) return;
        g_state->Stop();
        g_state.reset();
    }
}
