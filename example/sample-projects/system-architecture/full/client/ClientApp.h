#ifndef CLIENT_APP_H
#define CLIENT_APP_H

#include "DelegateMQ.h"
#include "Actuator.h"
#include "Sensor.h"
#include "DataMsg.h"

// ClientApp reads data locally and sends to DataMgr for storage. 
class ClientApp
{
public:
    static ClientApp& Instance()
    {
        static ClientApp instance;
        return instance;
    }

    /// Start data collection locally and remotely.
    /// @return `true` if all async commands sent successfully. 
    bool Start()
    {
        // Send START command
        CommandMsg command;
        command.action = CommandMsg::Action::START;
        command.pollTime = 250;

        // Use the blocking "Wait" function. 
        bool sendCommandMsgSuccess = NetworkMgr::Instance().SendCommandMsgWait(command);

        // Send Actuator command (using Future approach for demo)
        ActuatorMsg msg;
        msg.actuatorId = 1;

        // This launches an async task that calls SendActuatorMsgWait internally
        std::future<bool> futureRetVal = NetworkMgr::Instance().SendActuatorMsgFuture(msg);

        // Start local data collection
        m_pollTimerConn = m_pollTimer.OnExpired.Connect(dmq::MakeDelegate(this, &ClientApp::PollData, m_thread));
        m_pollTimer.Start(std::chrono::milliseconds(500));

        // Start actuator updates
        m_actuatorTimerConn = m_actuatorTimer.OnExpired.Connect(dmq::MakeDelegate(this, &ClientApp::ActuatorUpdate, m_thread));
        m_actuatorTimer.Start(std::chrono::milliseconds(1000), true);

        // Wait for the future to complete
        bool sendActuatorMsgSuccess = futureRetVal.get();

        // Return true if both succeeded
        return (sendCommandMsgSuccess && sendActuatorMsgSuccess);
    }

    void Stop()
    {
        CommandMsg command;
        command.action = CommandMsg::Action::STOP;
        NetworkMgr::Instance().SendCommandMsg(command); // Fire and forget is fine here

        m_pollTimer.Stop();
        m_pollTimerConn.Disconnect();

        m_actuatorTimer.Stop();
        m_actuatorTimerConn.Disconnect();
    }

private:
    ClientApp() :
        m_thread("ClientAppThread"),
        m_actuator3(3),
        m_actuator4(4),
        m_sensor3(3),
        m_sensor4(4)
    {
        m_thread.CreateThread();

        m_onNetworkErrorConn = NetworkMgr::Instance().OnNetworkError.Connect(dmq::MakeDelegate(this, &ClientApp::ErrorHandler, m_thread));
        m_onSendStatusConn = NetworkMgr::Instance().OnSendStatus.Connect(dmq::MakeDelegate(this, &ClientApp::SendStatusHandler, m_thread));
        m_onDeliveryFailureConn = NetworkMgr::Instance().OnDeliveryFailure.Connect(dmq::MakeDelegate(this, &ClientApp::DeliveryFailureHandler, m_thread));
    }

    ~ClientApp()
    {
        m_thread.ExitThread();
    }

    void PollData()
    {
        // Collect sensor and actuator data
        DataMsg dataMsg;
        dataMsg.actuators.push_back(m_actuator3.GetState());
        dataMsg.actuators.push_back(m_actuator4.GetState());
        dataMsg.sensors.push_back(m_sensor3.GetSensorData());
        dataMsg.sensors.push_back(m_sensor4.GetSensorData());

        // Set data collected locally
        DataMgr::Instance().SetDataMsg(dataMsg);
    }

    void ActuatorUpdate()
    {
        static int cnt = 0;
        float position = (++cnt % 2) ? 0.0f : 1.0f;

        // Set local acuator positions
        m_actuator3.SetPosition(position);
        m_actuator4.SetPosition(position);

        // Set remote actuator positions
        ActuatorMsg msg;
        msg.actuatorPosition = position;

        msg.actuatorId = 1;
        NetworkMgr::Instance().SendActuatorMsgWait(msg);

        msg.actuatorId = 2;
        NetworkMgr::Instance().SendActuatorMsgWait(msg);

        // Explicitly start timer for next time
        m_actuatorTimer.Start(std::chrono::milliseconds(1000), true);
    }

    void ErrorHandler(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux)
    {
        if (error != dmq::DelegateError::SUCCESS)
            std::cout << "ClientApp Error: " << id << " " << (int)error << " " << aux << std::endl;
    }

    void SendStatusHandler(dmq::DelegateRemoteId id, uint16_t seqNum, dmq::util::TransportMonitor::Status status)
    {
        if (status != dmq::util::TransportMonitor::Status::SUCCESS)
            std::cout << "ClientApp Timeout: " << id << " " << seqNum << std::endl;
    }

    // Never actually fires for this ZeroMQ-based sample (NetworkMgr has no
    // RetryMonitor attached -- ZeroMQ handles its own reliability), but wired
    // for consistency with the other system-architecture samples.
    void DeliveryFailureHandler(dmq::DelegateRemoteId id, uint16_t seqNum)
    {
        std::cout << "ClientApp Delivery Failed (retries exhausted): " << id << " " << seqNum << std::endl;
    }

    dmq::os::Thread m_thread;

    dmq::util::Timer m_pollTimer;
    dmq::util::Timer m_actuatorTimer;

    dmq::ScopedConnection m_pollTimerConn;
    dmq::ScopedConnection m_actuatorTimerConn;
    dmq::ScopedConnection m_onNetworkErrorConn;
    dmq::ScopedConnection m_onSendStatusConn;
    dmq::ScopedConnection m_onDeliveryFailureConn;

    Actuator m_actuator3;
    Actuator m_actuator4;
    Sensor m_sensor3;
    Sensor m_sensor4;
};

#endif
