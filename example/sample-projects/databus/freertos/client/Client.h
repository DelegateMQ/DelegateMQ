#ifndef CLIENT_H
#define CLIENT_H

/// @file Client.h
/// @brief Linux/Windows DataBus client — subscribes to SensorMsg and AlarmMsg, publishes CmdMsg.
///
/// Runs on Linux or Windows using the stdlib port. Receives sensor readings
/// and alarm state changes from the FreeRTOS server, and periodically sends
/// a command to adjust the server's publish interval.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "../common/Msg.h"
#include "../common/MsgSerializer.h"

#include <atomic>
#include <iostream>

/// @brief Active-object client with a stdlib polling thread.
///
/// - Subscribes to sensor/temp and alarm/status published by the FreeRTOS server.
/// - A dedicated Thread calls ProcessIncoming() on the inbound transport.
/// - Publishes CmdMsg via DataBus::Publish — caller drives the timing.
class Client
{
public:
    Client() : m_pollThread("ClntPoll") {}

    /// Set up transports, DataBus participants, and the polling thread.
    bool Start()
    {
        // Inbound transport (SUB): client receives from server on port 9000.
        // Carries SensorMsg and AlarmMsg — distinguished by DelegateRemoteId.
        if (m_subTransport.Create(dmq::transport::UdpTransport::Type::SUB, "127.0.0.1", 9000) != 0) {
            std::cerr << "[Client] ERROR: Failed to create inbound transport\n";
            return false;
        }

        // Outbound transport (PUB): client publishes to server on port 9001.
        if (m_pubTransport.Create(dmq::transport::UdpTransport::Type::PUB, "127.0.0.1", 9001) != 0) {
            std::cerr << "[Client] ERROR: Failed to create outbound transport\n";
            return false;
        }

        // Register outbound publish side
        m_pubParticipant = std::make_shared<dmq::databus::Participant>(m_pubTransport);
        m_pubParticipant->AddRemoteTopic(topics::CmdRate, topics::CmdRateId);
        dmq::databus::DataBus::AddParticipant(m_pubParticipant);
        dmq::databus::DataBus::RegisterSerializer<CmdMsg>(topics::CmdRate, m_cmdSer);

        // Register sensor and alarm receive side — both share the same transport
        // (port 9000). The DmqHeader ID field distinguishes them on the wire.
        m_subParticipant = std::make_shared<dmq::databus::Participant>(m_subTransport);
        dmq::databus::DataBus::AddIncomingTopic<SensorMsg>(
            topics::SensorTemp,  topics::SensorTempId,  *m_subParticipant, m_sensorSer);
        dmq::databus::DataBus::AddIncomingTopic<AlarmMsg>(
            topics::AlarmStatus, topics::AlarmStatusId, *m_subParticipant, m_alarmSer);

        // Subscribe to sensor readings on the local bus
        m_sensorConn = dmq::databus::DataBus::Subscribe<SensorMsg>(topics::SensorTemp,
            [](const SensorMsg& msg) {
                std::cout << "[Client] sensor/temp: id=" << msg.sensorId
                          << "  temp=" << msg.temp << " C\n";
            });

        // Subscribe to alarm state changes on the local bus
        m_alarmConn = dmq::databus::DataBus::Subscribe<AlarmMsg>(topics::AlarmStatus,
            [](const AlarmMsg& msg) {
                std::cout << "[Client] alarm/status: id=" << msg.alarmId
                          << "  " << (msg.active ? "ACTIVE" : "inactive") << "\n";
            });

        m_running = true;

        // Start the polling thread (stdlib Thread — maps to std::thread on Linux/Windows)
        m_pollThread.CreateThread();
        dmq::MakeDelegate(this, &Client::Poll, m_pollThread).AsyncInvoke();

        std::cout << "[Client] Started. Receiving on sensor/temp and alarm/status.\n";
        return true;
    }

    /// Signal the client to stop and clean up.
    void Stop()
    {
        m_running = false;
        m_pollThread.ExitThread();
        m_subTransport.Close();
        m_pubTransport.Close();
    }

    /// Publish a command to adjust the server's publish interval.
    void SendCmd(int intervalMs)
    {
        CmdMsg cmd;
        cmd.intervalMs = intervalMs;
        std::cout << "[Client] Sending cmd/rate: intervalMs=" << intervalMs << "\n";
        dmq::databus::DataBus::Publish<CmdMsg>(topics::CmdRate, cmd);
    }

private:
    /// Polling loop — runs on m_pollThread.
    void Poll()
    {
        while (m_running)
            m_subParticipant->ProcessIncoming();
    }

    dmq::transport::UdpTransport m_subTransport;
    dmq::transport::UdpTransport m_pubTransport;

    std::shared_ptr<dmq::databus::Participant> m_subParticipant;
    std::shared_ptr<dmq::databus::Participant> m_pubParticipant;

    SensorSerializer m_sensorSer;
    CmdSerializer    m_cmdSer;
    AlarmSerializer  m_alarmSer;

    dmq::ScopedConnection m_sensorConn;
    dmq::ScopedConnection m_alarmConn;

    dmq::os::Thread m_pollThread;

    std::atomic<bool> m_running{false};
};

#endif // CLIENT_H
