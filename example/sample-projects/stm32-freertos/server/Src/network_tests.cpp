/**
 * @file network_tests.cpp
 * @brief STM32 Server Application for Network Integration Tests.
 * @see https://github.com/DelegateMQ/DelegateMQ
 * @author David Lafreniere, 2026.
 *
 * @details
 * This file implements the "ServerApp" logic for the DelegateMQ STM32 demo. 
 * It acts as the remote endpoint that communicates with the PC-based ClientApp.
 *
 * **Key Responsibilities:**
 * 1. **Command Handling:** Receives START/STOP commands from the Client to begin/end 
 * telemetry streaming.
 * 2. **Actuator Control:** Listens for `ActuatorMsg` from the Client to toggle simulated 
 * hardware actuators (and local LEDs).
 * 3. **Telemetry Streaming:** Uses a FreeRTOS software timer to periodically poll 
 * local sensors and actuators, packaging the data into `DataMsg` packets sent 
 * back to the Client.
 * 4. **Alarm Simulation:** Periodically generates simulated `AlarmMsg` events to test 
 * asynchronous alarm propagation to the Client.
 *
 * **Architecture:**
 * - **Singleton:** The `ServerApp` class is a singleton that manages its own FreeRTOS thread.
 * - **Asynchronous:** Uses `DelegateMQ` signals and slots to handle incoming network 
 * packets on the application thread, preventing network thread starvation.
 */

#include "DelegateMQ.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../../common/NetworkMgr.h"
#include "../../common/AlarmMgr.h"
#include <stdio.h>

// External reference to the UART handle
extern UART_HandleTypeDef huart6;

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// ============================================================================
// SERVER APP
// ============================================================================
class ServerApp
{
public:
    static ServerApp& Instance()
    {
        static ServerApp instance;
        return instance;
    }

    // Receive and handle client command message
    void CommandMsgRecv(CommandMsg& command)
    {
        if (command.action == CommandMsg::Action::START)
        {
            StartPolling(command.pollTime);
        }
        else if (command.action == CommandMsg::Action::STOP)
        {
            printf("[ServerApp] Stop Cmd Received.\n");
            StopPolling();
        }
    }

    // Receive and handle client actuator message
    void ActuatorMsgRecv(ActuatorMsg& command)
    {
        printf("[ServerApp] Actuator Cmd: ID=%d Pos=%d\n", command.actuatorId, command.actuatorPosition);

        if (command.actuatorId == 1)
            m_actuator1.SetPosition(command.actuatorPosition);
        else if (command.actuatorId == 2)
            m_actuator2.SetPosition(command.actuatorPosition);
        else
            printf("[ServerApp] Unknown actuator ID\n");
    }

private:
    ServerApp() :
        m_thread("ServerApp"),
        m_actuator1(1),
        m_actuator2(2),
        m_sensor1(1),
        m_sensor2(2)
    {
        m_thread.CreateThread();

        // Register for incoming client commands
        m_onCommandConn = NetworkMgr::Instance().OnCommand.Connect(MakeDelegate(this, &ServerApp::CommandMsgRecv, m_thread));
        m_onActuatorConn = NetworkMgr::Instance().OnActuator.Connect(MakeDelegate(this, &ServerApp::ActuatorMsgRecv, m_thread));
        m_onNetworkErrorConn = NetworkMgr::Instance().OnNetworkError.Connect(MakeDelegate(this, &ServerApp::ErrorHandler, m_thread));

        // Critical: Handle Status updates to detect disconnection
        m_onSendStatusConn = NetworkMgr::Instance().OnSendStatus.Connect(MakeDelegate(this, &ServerApp::SendStatusHandler, m_thread));
    }

    ~ServerApp()
    {
        m_thread.ExitThread();
    }

    // Helper to Start Polling
    void StartPolling(uint32_t pollTime)
    {
        // Reset error counter on fresh start
        m_consecutiveErrors = 0;

        if (!m_pollTimerConn.IsConnected()) {
            m_pollTimerConn = m_pollTimer.OnExpired.Connect(MakeTimerDelegate(this, &ServerApp::PollData, m_thread));
            m_pollTimer.Start(std::chrono::milliseconds(pollTime));
            BSP_LED_On(LED4); // Green LED ON = Polling
            printf("[ServerApp] Polling Started (%lu ms)\n", pollTime);
        }
    }

    // Helper to Stop Polling (Circuit Breaker)
    void StopPolling()
    {
        if (m_pollTimerConn.IsConnected()) {
            m_pollTimer.Stop();
            m_pollTimerConn.Disconnect();
            BSP_LED_Off(LED4); // Green LED OFF
            printf("[ServerApp] Polling Stopped.\n");
        }
    }

    void PollData()
    {
        static int errCnt = 0;

        // Periodically simulate an alarm
        if (errCnt++ % 10 == 0)
        {
            // printf("[ServerApp] Generating Simulated Alarm...\n");
            AlarmMsg msg;
            msg.alarm = AlarmMsg::Alarm::ACTUATOR_ERROR;
            msg.source = AlarmMsg::Source::SERVER;
            AlarmNote note;
            note.note = "Server simulated alarm!";
            AlarmMgr::Instance().SetAlarm(msg, note);
        }

        // Collect sensor and actuator data
        DataMsg dataMsg;
        dataMsg.actuators.push_back(m_actuator1.GetState());
        dataMsg.actuators.push_back(m_actuator2.GetState());
        dataMsg.sensors.push_back(m_sensor1.GetSensorData());
        dataMsg.sensors.push_back(m_sensor2.GetSensorData());

        // Send data to remote client
        NetworkMgr::Instance().SendDataMsg(dataMsg);

        BSP_LED_Toggle(LED6); // Blue Toggle = Data Sent attempt
    }

    void ErrorHandler(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux)
    {
        if (error != dmq::DelegateError::SUCCESS)
            printf("[ServerApp] Error: ID=%d Err=%d Aux=%d\n", id, (int)error, aux);
    }

    // ------------------------------------------------------------------------
    // STATUS HANDLER: The Circuit Breaker (Cleaner Version)
    // ------------------------------------------------------------------------
    void SendStatusHandler(dmq::DelegateRemoteId id, uint16_t seqNum, TransportMonitor::Status status)
    {
        if (status == TransportMonitor::Status::SUCCESS)
        {
            // If we were previously disconnected, announce restoration
            if (m_consecutiveErrors > 0) {
                printf("[ServerApp] Connection Restored. Resetting errors.\n");
                m_consecutiveErrors = 0;
            }
        }
        else
        {
            // Prevent counter overflow and excessive logging
            if (m_consecutiveErrors <= MAX_CONSECUTIVE_ERRORS) {
                m_consecutiveErrors++;
            }

            // TRIGGER POINT: Exactly when we hit the limit
            if (m_consecutiveErrors == MAX_CONSECUTIVE_ERRORS)
            {
                printf("[ServerApp] CRITICAL: Client lost. Stopping Polling to save Heap.\n");
                StopPolling();
            }
            // WARNING POINT: Approaching the limit
            else if (m_consecutiveErrors < MAX_CONSECUTIVE_ERRORS)
            {
                printf("[ServerApp] Send Failed (Seq %d). Errors: %d/%d\n", seqNum, m_consecutiveErrors, MAX_CONSECUTIVE_ERRORS);
            }
            // SILENT POINT: If > MAX, we already stopped. Don't spam logs.
        }
    }

    Thread m_thread;
    Timer m_pollTimer;

    // RAII Connections
    dmq::ScopedConnection m_pollTimerConn;
    dmq::ScopedConnection m_onCommandConn;
    dmq::ScopedConnection m_onActuatorConn;
    dmq::ScopedConnection m_onNetworkErrorConn;
    dmq::ScopedConnection m_onSendStatusConn;

    Actuator m_actuator1;
    Actuator m_actuator2;
    Sensor m_sensor1;
    Sensor m_sensor2;

    // Error Tracking
    int m_consecutiveErrors = 0;
    static const int MAX_CONSECUTIVE_ERRORS = 5; // Stop after 5 failed messages
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
void StartNetworkTests() {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n=== STARTING STM32 SERVER APP ===\n");

    printf("[Init] NetworkMgr Create...\n");
    int err = NetworkMgr::Instance().Create();
    if (err) {
        printf("[FAIL] NetworkMgr Create Error: %d\n", err);
        return;
    }

    printf("[Init] ServerApp Instance...\n");
    ServerApp::Instance();

    printf("[Init] NetworkMgr Start...\n");
    NetworkMgr::Instance().Start();

    printf("[Ready] Server is running. Waiting for Client commands...\n");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
