#include "Logs.h"
#include "messages/CentrifugeStatusMsg.h"
#include "messages/CentrifugeSpeedMsg.h"
#include "messages/RunStatusMsg.h"
#include "messages/StartProcessMsg.h"
#include "messages/StopProcessMsg.h"
#include "messages/FaultMsg.h"
#include "messages/ActuatorStatusMsg.h"
#include "messages/SensorStatusMsg.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include <iomanip>
#include <sstream>
#include <chrono>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace util {

Logs::~Logs() {
    Shutdown();
}

void Logs::Initialize() {
    // Enable DelegateMQ Watchdog (20 second timeout for logging)
    m_thread.CreateThread(std::chrono::seconds(20));

    // Open file and write initial log on the logging thread
    (void)dmq::MakeDelegate(this, &Logs::OnInit, m_thread).AsyncInvoke();

    // Register thread for monitoring
    ThreadMonitor::Register(&m_thread);

    m_startConn = dmq::databus::DataBus::Subscribe<StartProcessMsg>(topics::CMD_RUN, dmq::MakeDelegate(this, &Logs::OnStart), &m_thread);
    m_stopConn = dmq::databus::DataBus::Subscribe<StopProcessMsg>(topics::CMD_ABORT, dmq::MakeDelegate(this, &Logs::OnStop), &m_thread);
    m_speedConn = dmq::databus::DataBus::Subscribe<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, dmq::MakeDelegate(this, &Logs::OnSpeed), &m_thread);
    m_runConn = dmq::databus::DataBus::Subscribe<RunStatusMsg>(topics::STATUS_RUN, dmq::MakeDelegate(this, &Logs::OnRunStatus), &m_thread);
    m_faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, dmq::MakeDelegate(this, &Logs::OnFault), &m_thread);
    m_actuatorConn = dmq::databus::DataBus::Subscribe<ActuatorStatusMsg>(topics::STATUS_ACTUATOR, dmq::MakeDelegate(this, &Logs::OnActuator), &m_thread);
    m_sensorConn = dmq::databus::DataBus::Subscribe<SensorStatusMsg>(topics::STATUS_SENSOR, dmq::MakeDelegate(this, &Logs::OnSensor), &m_thread);

    // Start a 5-second internal heartbeat for the log thread itself
    m_heartbeatTimer = std::make_unique<Timer>();
    m_heartbeatConn = m_heartbeatTimer->OnExpired.Connect(dmq::MakeDelegate(this, &Logs::LogHeartbeat, m_thread));
    m_heartbeatTimer->Start(std::chrono::seconds(5));
}

void Logs::OnInit() {
    m_file.open("logs.txt", std::ios::out | std::ios::trunc);
    WriteToFile("--- Logging System Initializing ---");
    WriteToFile("--- Logging Thread Started ---");
}

void Logs::OnStart(StartProcessMsg msg) {
    dmq::xostringstream ss;
    ss << "[CMD] START Process Command (Seq: " << msg.seq << ")";
    WriteToFile(ss.str().c_str());
}

void Logs::OnStop(StopProcessMsg msg) {
    dmq::xostringstream ss;
    ss << "[CMD] ABORT Process Command (Seq: " << msg.seq << ")";
    WriteToFile(ss.str().c_str());
}

void Logs::OnSpeed(CentrifugeSpeedMsg msg) {
    dmq::xostringstream ss;
    ss << "[STATUS] Centrifuge Speed: " << msg.rpm << " RPM (Seq: " << msg.seq << ")";
    WriteToFile(ss.str().c_str());
}

void Logs::OnRunStatus(RunStatusMsg msg) {
    const char* status_text = "UNKNOWN";
    switch (msg.status) {
        case RunStatus::IDLE:       status_text = "IDLE";       break;
        case RunStatus::PROCESSING: status_text = "PROCESSING"; break;
        case RunStatus::ABORTING:   status_text = "ABORTING";   break;
        case RunStatus::FAULT:      status_text = "FAULT";      break;
    }
    dmq::xostringstream ss;
    ss << "[STATUS] System State: " << status_text;
    WriteToFile(ss.str().c_str());
}

void Logs::OnFault(FaultMsg msg) {
    const char* reason = "UNKNOWN";
    dmq::xostringstream ss;
    switch (msg.faultCode) {
        case FAULT_OVERSPEED:        reason = "CENTRIFUGE OVERSPEED";      break;
        case FAULT_SAFETY_LOST:      reason = "SAFETY HEARTBEAT LOST";     break;
        case FAULT_CONTROLLER_LOST:  reason = "CONTROLLER HEARTBEAT LOST"; break;
        case FAULT_GUI_LOST:         reason = "GUI HEARTBEAT LOST";        break;
        case FAULT_AIR_INLET:        reason = "AIR INLET DETECTED";        break;
        case FAULT_BLOCKAGE:         reason = "PRESSURE BLOCKAGE";         break;
        default:
            ss << "UNKNOWN (Code: " << msg.faultCode << ")";
            WriteToFile((dmq::xstring("[CRITICAL] FAULT DETECTED: ") + ss.str()).c_str());
            return;
    }
    WriteToFile((dmq::xstring("[CRITICAL] FAULT DETECTED: ") + reason).c_str());
}

void Logs::OnActuator(ActuatorStatusMsg msg) {
    dmq::xostringstream ss;
    if (msg.type == ActuatorType::VALVE) {
        ss << "[HW] Valve " << (int)msg.id << " changed to " << (msg.value ? "OPEN" : "CLOSED");
    } else {
        ss << "[HW] Pump " << (int)msg.id << " speed changed to " << (int)msg.value << "%";
    }
    WriteToFile(ss.str().c_str());
}

void Logs::OnSensor(SensorStatusMsg msg) {
    dmq::xostringstream ss;
    if (msg.type == SensorType::PRESSURE) {
        ss << "[HW] Pressure Sensor: " << msg.value;
    } else {
        ss << "[HW] Air Detector: " << (msg.value ? "AIR" : "FLUID");
    }
    WriteToFile(ss.str().c_str());
}

void Logs::LogHeartbeat() {
    WriteToFile("[DIAG] LogsThread Heartbeat - Dispatcher OK");
}

void Logs::Shutdown() {
    // Write shutdown log and close file on the logging thread before exiting
    (void)dmq::MakeDelegate(this, &Logs::OnShutdown, m_thread).AsyncInvoke();

    m_thread.ExitThread();
}

void Logs::OnShutdown() {
    WriteToFile("--- Logging Shutdown ---");
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Logs::WriteToFile(const std::string& msg) {
    if (m_file.is_open()) {
        m_file << GetTimestamp() << " " << msg << "\n";

        // Flush every 10th write to balance performance and data safety.
        if (++m_writeCount >= 10) {
            m_file.flush();
            m_writeCount = 0;
        }
    }
}

std::string Logs::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &in_time_t);
#else
    localtime_r(&in_time_t, &tm_buf);
#endif
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

} // namespace util
} // namespace cellutron
