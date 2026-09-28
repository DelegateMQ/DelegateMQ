// main.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// DataBus <-> MQTT gateway demo: a simulated thermostat node.
//
// The thermostat is an ordinary DataBus application: it publishes room climate
// and its own status, and reacts to setpoint commands. MqttBridge exposes those
// topics to any MQTT tool. The topics are declared once with JsonTopics (the
// shared bridge layer), and MqttBridge serves them over MQTT:
//
//   DataBus "room/climate"        ->  MQTT "<prefix>/room/climate"        {"celsius":20.4,"humidity":41.0}
//   DataBus "thermostat/status"   ->  MQTT "<prefix>/thermostat/status"   {"setpoint":21.0,"heating":true}  (retained)
//   MQTT "<prefix>/thermostat/setpoint/set" {"celsius":23.5}  ->  DataBus "thermostat/setpoint"
//
// Usage: databus_mqtt_gateway_app [broker-uri] [--prefix <name>] [--seconds <n>]
//   broker-uri defaults to tcp://127.0.0.1:1883, prefix to "thermo".
//   Runs until Ctrl-C, or for --seconds.
// See README.md for trying it with mosquitto_sub/mosquitto_pub or MQTT Explorer.

#include "DelegateMQ.h"
#include "JsonTopics.h"
#include "MqttBridge.h"
#include "BridgeJson.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

using namespace dmq;
using namespace dmq::databus;

// ---------------------------------------------------------------------------
// Messages and topics (plain structs; nothing MQTT-specific)
// ---------------------------------------------------------------------------

struct Climate {
    float celsius = 0;
    float humidity = 0;
};

struct ThermostatStatus {
    float setpoint = 0;
    bool heating = false;
};

struct SetpointCmd {
    float celsius = 0;
};

namespace topics {
    constexpr const char* CLIMATE = "room/climate";
    constexpr const char* STATUS = "thermostat/status";
    constexpr const char* SETPOINT = "thermostat/setpoint";
}

// ---------------------------------------------------------------------------
// JSON converters: the only MQTT-facing code the application writes
// ---------------------------------------------------------------------------

std::string ClimateToJson(const Climate& c) {
    return bridgejson::Writer().Add("celsius", c.celsius, 1).Add("humidity", c.humidity, 1).Str();
}

std::string StatusToJson(const ThermostatStatus& s) {
    return bridgejson::Writer().Add("setpoint", s.setpoint, 1).Add("heating", s.heating).Str();
}

// Inbound commands are an entry point into the application: validate them.
bool SetpointFromJson(const std::string& json, SetpointCmd& cmd) {
    bridgejson::Reader r;
    double celsius = 0;
    if (!r.Parse(json) || !r.GetNumber("celsius", celsius)) return false;
    if (celsius < 5.0 || celsius > 35.0) return false;     // accepted range
    cmd.celsius = static_cast<float>(celsius);
    return true;
}

// ---------------------------------------------------------------------------
// Thermostat: an ordinary DataBus component with its own thread
// ---------------------------------------------------------------------------

class Thermostat {
public:
    Thermostat() : m_thread("Thermostat") {}

    void Start() {
        m_thread.CreateThread();
        // Commands arrive on this thread, whether from MQTT or any other publisher.
        m_setpointConn = DataBus::Subscribe<SetpointCmd>(topics::SETPOINT,
            MakeDelegate(this, &Thermostat::OnSetpoint), &m_thread);
        PublishStatus();
    }

    void Stop() {
        m_setpointConn.Disconnect();
        m_thread.ExitThread();
    }

    // Called once per second: simple heating/cooling model.
    void Tick() {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool heating = m_climate.celsius < m_status.setpoint - 0.2f;
        m_climate.celsius += heating ? 0.3f : -0.1f;
        m_climate.humidity = 45.0f - (m_climate.celsius - 18.0f);
        DataBus::Publish<Climate>(topics::CLIMATE, m_climate);
        if (heating != m_status.heating) {
            m_status.heating = heating;
            DataBus::Publish<ThermostatStatus>(topics::STATUS, m_status);
        }
    }

private:
    void OnSetpoint(const SetpointCmd& cmd) {
        std::cout << "[Thermostat] new setpoint " << cmd.celsius << " C" << std::endl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.setpoint = cmd.celsius;
        }
        PublishStatus();
    }

    void PublishStatus() {
        ThermostatStatus s;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            s = m_status;
        }
        DataBus::Publish<ThermostatStatus>(topics::STATUS, s);
    }

    os::Thread m_thread;
    ScopedConnection m_setpointConn;
    std::mutex m_mutex;
    Climate m_climate{ 18.0f, 45.0f };
    ThermostatStatus m_status{ 21.0f, false };
};

// ---------------------------------------------------------------------------

static std::atomic<bool> g_running{ true };
static void OnSigInt(int) { g_running = false; }

int main(int argc, char* argv[]) {
    MqttBridge::Options options;
    options.topicPrefix = "thermo";
    int seconds = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--prefix") && i + 1 < argc) options.topicPrefix = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (argv[i][0] != '-') options.brokerUri = argv[i];
        else {
            std::cout << "Usage: " << argv[0] << " [broker-uri] [--prefix <name>] [--seconds <n>]" << std::endl;
            return 1;
        }
    }
    std::signal(SIGINT, OnSigInt);

    // A local subscriber, to show the DataBus side is unchanged by the bridge.
    os::Thread consoleThread("Console");
    consoleThread.CreateThread();
    auto statusConn = DataBus::Subscribe<ThermostatStatus>(topics::STATUS, [](const ThermostatStatus& s) {
        std::cout << "[DataBus] status: setpoint=" << s.setpoint << " C heating=" << (s.heating ? "on" : "off") << std::endl;
    }, &consoleThread);

    // Bridge events (connect, disconnect, rejected commands) to the console.
    auto eventConn = JsonTopics::OnEvent().Connect(MakeDelegate([](const std::string& text) {
        std::cout << "[Bridge] " << text << std::endl;
    }, consoleThread));

    // Declare the JSON view of the application once; every bridge serves it.
    JsonTopics::Expose<Climate>(topics::CLIMATE, &ClimateToJson);
    JsonTopics::Expose<ThermostatStatus>(topics::STATUS, &StatusToJson, { true /*latched*/, true /*reliable*/ });
    JsonTopics::Accept<SetpointCmd>(topics::SETPOINT, &SetpointFromJson);

    if (!MqttBridge::Start(options))
        return 1;

    std::cout << "Thermostat running. MQTT topics under \"" << options.topicPrefix << "/\" on "
              << options.brokerUri << ". Ctrl-C to stop." << std::endl
              << "  Try: mosquitto_pub -t " << MqttBridge::MqttTopic(topics::SETPOINT)
              << "/set -m '{\"celsius\":23.5}'" << std::endl;

    Thermostat thermostat;
    thermostat.Start();

    auto start = std::chrono::steady_clock::now();
    while (g_running) {
        thermostat.Tick();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (seconds > 0 && std::chrono::steady_clock::now() - start >= std::chrono::seconds(seconds))
            break;
    }

    std::cout << "Stopping..." << std::endl;
    MqttBridge::Stop();
    JsonTopics::Shutdown();
    thermostat.Stop();
    statusConn.Disconnect();
    eventConn.Disconnect();
    consoleThread.ExitThread();
    return 0;
}
