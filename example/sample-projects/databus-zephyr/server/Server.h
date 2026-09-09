#ifndef SERVER_H
#define SERVER_H

/// @file Server.h
/// @brief Zephyr DataBus server — publishes SensorMsg and AlarmMsg, subscribes to CmdMsg.
///
/// Runs on Zephyr's official `native_sim` simulation port (ZephyrUdpTransport,
/// Zephyr's native BSD-socket networking). When porting to real hardware, no
/// transport change is needed -- ZephyrUdpTransport already uses Zephyr's own
/// networking subsystem; only prj.conf's link-layer driver selection changes
/// (loopback here, a real Ethernet/Wi-Fi driver on real hardware). The DataBus
/// calls are unchanged, mirroring `databus-freertos/server/Server.h`.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "../common/Msg.h"
#include "../common/MsgSerializer.h"

#include <atomic>
#include <cstdio>

/// @brief Active-object server running on Zephyr's main thread.
///
/// - Owns two UDP transports: PUB outbound (port 9000), SUB inbound (port 9001).
/// - A dedicated poll Thread calls ProcessIncoming() on the inbound transport.
/// - Run() is the main publish loop — call from main() after Start().
class Server
{
public:
    static Server& GetInstance() {
        static Server instance;
        return instance;
    }

    /// Set up transports, DataBus participants, and the polling thread.
    /// Call once from main(), after the Zephyr kernel is up.
    bool Start()
    {
        // Outbound transport (PUB): server publishes to client on port 9000.
        // Carries SensorMsg and AlarmMsg — distinguished by DelegateRemoteId.
        if (m_pubTransport.Create(dmq::transport::UdpTransport::Type::PUB, "127.0.0.1", 9000) != 0) {
            printf("[Server] ERROR: Failed to create outbound transport\n");
            return false;
        }

        // Inbound transport (SUB): server receives from client on port 9001.
        if (m_subTransport.Create(dmq::transport::UdpTransport::Type::SUB, "127.0.0.1", 9001) != 0) {
            printf("[Server] ERROR: Failed to create inbound transport\n");
            return false;
        }

        // Register sensor and alarm publish side — both share the same transport
        // (port 9000). The DmqHeader ID field distinguishes them on the wire.
        m_pubParticipant = std::make_shared<dmq::databus::Participant>(m_pubTransport);
        m_pubParticipant->AddRemoteTopic(topics::SensorTemp,  topics::SensorTempId);
        m_pubParticipant->AddRemoteTopic(topics::AlarmStatus, topics::AlarmStatusId);
        dmq::databus::DataBus::AddParticipant(m_pubParticipant);
        dmq::databus::DataBus::RegisterSerializer<SensorMsg>(topics::SensorTemp,  m_sensorSer);
        dmq::databus::DataBus::RegisterSerializer<AlarmMsg> (topics::AlarmStatus, m_alarmSer);

        // Register inbound receive side — re-publishes to local bus on arrival
        m_subParticipant = std::make_shared<dmq::databus::Participant>(m_subTransport);
        dmq::databus::DataBus::AddIncomingTopic<CmdMsg>(
            topics::CmdRate, topics::CmdRateId, *m_subParticipant, m_cmdSer);

        // Subscribe to commands on the local bus (no thread — fires synchronously
        // in the polling thread that called ProcessIncoming)
        m_cmdConn = dmq::databus::DataBus::Subscribe<CmdMsg>(topics::CmdRate,
            [this](const CmdMsg& cmd) { OnCmd(cmd); });

        m_running = true;

        // Poll thread must be lower priority than the publish loop below (which
        // runs on Zephyr's main thread, default priority 0) so main can preempt
        // it whenever the publish loop's sleep expires. ZephyrThread's own
        // default worker priority (5) is already lower -- set explicitly here
        // for clarity, matching Zephyr's convention (lower number = higher
        // priority, same as ThreadX; opposite of FreeRTOS's).
        m_pollThread.SetThreadPriority(5);

        // Start the polling thread
        m_pollThread.CreateThread();
        dmq::MakeDelegate(this, &Server::Poll, m_pollThread).AsyncInvoke();

        printf("[Server] Started. Publishing on sensor/temp every %dms.\n",
            m_intervalMs.load());
        return true;
    }

    /// Main publish loop — sleeps between sensor publishes.
    /// Call from main(); does not return until Stop() is called.
    void Run()
    {
        while (m_running)
        {
            PublishSensor();

            // Publish alarm every 3 sensor cycles, alternating active/inactive.
            if (++m_alarmTick >= 3)
            {
                m_alarmTick   = 0;
                m_alarmActive = !m_alarmActive;
                PublishAlarm();
            }

            dmq::ThisThread::sleep_for(std::chrono::milliseconds(m_intervalMs.load()));
        }
    }

    /// Signal the server to stop and clean up.
    void Stop()
    {
        m_running = false;
        m_pollThread.ExitThread();
        m_pubTransport.Close();
        m_subTransport.Close();
    }

private:
    Server() : m_pollThread("SrvPoll") {}

    /// Polling loop — runs on m_pollThread.
    /// ProcessIncoming() blocks inside recvfrom until data arrives or timeout.
    void Poll()
    {
        while (m_running)
            m_subParticipant->ProcessIncoming();
    }

    /// Subscriber callback: adjust publish interval when a CmdMsg arrives.
    void OnCmd(const CmdMsg& cmd)
    {
        printf("[Server] Received CmdMsg: intervalMs=%d\n", cmd.intervalMs);
        m_intervalMs = cmd.intervalMs;
    }

    /// Publish one sensor reading to the DataBus (forwarded to remote client).
    void PublishSensor()
    {
        SensorMsg msg;
        msg.sensorId = 1;
        msg.temp     = 20.0f + (m_iteration * 0.1f);
        m_iteration++;
        printf("[Server] Publishing sensor/temp: %.1f C (interval=%dms)\n",
            msg.temp, m_intervalMs.load());
        dmq::databus::DataBus::Publish<SensorMsg>(topics::SensorTemp, msg);
    }

    /// Publish an alarm state change to the DataBus.
    /// Shares the same UDP transport as SensorMsg (port 9000).
    void PublishAlarm()
    {
        AlarmMsg msg;
        msg.alarmId = 1;
        msg.active  = m_alarmActive;
        printf("[Server] Publishing alarm/status: id=%d  %s\n",
            msg.alarmId, msg.active ? "ACTIVE" : "inactive");
        dmq::databus::DataBus::Publish<AlarmMsg>(topics::AlarmStatus, msg);
    }

    dmq::transport::UdpTransport m_pubTransport;
    dmq::transport::UdpTransport m_subTransport;

    std::shared_ptr<dmq::databus::Participant> m_pubParticipant;
    std::shared_ptr<dmq::databus::Participant> m_subParticipant;

    SensorSerializer m_sensorSer;
    CmdSerializer    m_cmdSer;
    AlarmSerializer  m_alarmSer;

    dmq::ScopedConnection m_cmdConn;

    dmq::os::Thread m_pollThread;

    std::atomic<int>  m_intervalMs{1000};
    std::atomic<bool> m_running{false};
    int               m_iteration{0};
    int               m_alarmTick{0};
    bool              m_alarmActive{false};
};

#endif // SERVER_H
