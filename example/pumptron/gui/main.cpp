/**
 * @file gui/main.cpp
 * @brief Pumptron operator console (Windows/Linux, FTXUI).
 *
 * Usage:
 *   pumptron_gui --serial <port> [--baud <rate>]   Real STM32F4 Discovery over RS-232
 *   pumptron_gui --udp                             FreeRTOS simulator on localhost
 *   Add --selftest to run a headless end-to-end check instead of the UI.
 *   Add --spy to mirror all DataBus traffic, including messages received over
 *   the link, to dmq-spy (see PUMPTRON.md, "Monitoring with dmq-spy").
 *   Add --mqtt <broker-uri> to publish pump status/telemetry/alarms to MQTT, and
 *   --mqtt-control to also accept pump commands from MQTT (see PUMPTRON.md,
 *   "MQTT Gateway").
 */

#include "DelegateMQ.h"
#include "extras/util/NetworkConnect.h"
#include "system/System.h"
#include "ui/UI.h"
#include "selftest/SelfTest.h"
#include "util/Constants.h"
#include "SpyBridge.h"
#if defined(PUMPTRON_HAVE_MQTT)
#include "system/MqttGateway.h"
#include "JsonTopics.h"
#endif
#if defined(PUMPTRON_HAVE_SERIAL)
#include "libserialport.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace pumptron;

/// dmq-spy's default listen port.
static constexpr int SPY_DEFAULT_PORT = 9999;

struct SpyOptions {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = SPY_DEFAULT_PORT;
};

struct MqttOptions {
    std::string brokerUri;          ///< Empty: MQTT gateway off
    std::string topicPrefix = "pumptron";
    bool allowControl = false;
};

static void PrintUsage(const char* exe)
{
    printf("Pumptron operator console\n\n");
    printf("Usage:\n");
    printf("  %s --serial <port> [--baud <rate>]   STM32F4 Discovery over RS-232 (default %d baud)\n", exe, SERIAL_BAUD);
    printf("  %s --udp                             FreeRTOS simulator (pumptron_controller) on localhost\n", exe);
    printf("  Add --selftest to run a headless end-to-end check of the controller instead of the UI.\n");
    printf("  Add --spy [--spy-address <host:port>] to mirror DataBus traffic to dmq-spy (default 127.0.0.1:%d).\n",
           SPY_DEFAULT_PORT);
#if defined(PUMPTRON_HAVE_MQTT)
    printf("  Add --mqtt <broker-uri> [--mqtt-prefix <name>] [--mqtt-control] to publish pump data to MQTT\n");
    printf("    (e.g. tcp://127.0.0.1:1883, prefix \"pumptron\"); --mqtt-control also accepts pump commands.\n\n");
#else
    printf("  (Built without Paho MQTT: --mqtt is unavailable.)\n\n");
#endif

#if defined(PUMPTRON_HAVE_SERIAL)
    printf("Serial ports found:\n");
    sp_port** ports = nullptr;
    if (sp_list_ports(&ports) == SP_OK && ports && ports[0]) {
        for (int i = 0; ports[i]; ++i) {
            const char* desc = sp_get_port_description(ports[i]);
            printf("  %-12s %s\n", sp_get_port_name(ports[i]), desc ? desc : "");
        }
    } else {
        printf("  (none)\n");
    }
    if (ports) sp_free_port_list(ports);
#else
    printf("(Built without libserialport: --serial is unavailable.)\n");
#endif
}

static bool ParseSpyAddress(const std::string& addr, SpyOptions& spy)
{
    auto colon = addr.rfind(':');
    spy.host = addr.substr(0, colon);
    if (colon != std::string::npos)
        spy.port = atoi(addr.c_str() + colon + 1);
    return !spy.host.empty() && spy.port > 0 && spy.port <= 65535;
}

static bool ParseArgs(int argc, char* argv[], System::Options& options, bool& selfTest, SpyOptions& spy,
                      MqttOptions& mqtt)
{
    bool haveLink = false;
    options.baud = SERIAL_BAUD;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--serial") && i + 1 < argc) {
            options.link = System::LinkType::SERIAL;
            options.serialPort = argv[++i];
            haveLink = true;
        } else if (!strcmp(argv[i], "--baud") && i + 1 < argc) {
            options.baud = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--selftest")) {
            selfTest = true;
        } else if (!strcmp(argv[i], "--spy")) {
            spy.enabled = true;
        } else if (!strcmp(argv[i], "--spy-address") && i + 1 < argc) {
            spy.enabled = true;
            if (!ParseSpyAddress(argv[++i], spy))
                return false;
        } else if (!strcmp(argv[i], "--mqtt") && i + 1 < argc) {
            mqtt.brokerUri = argv[++i];
        } else if (!strcmp(argv[i], "--mqtt-prefix") && i + 1 < argc) {
            mqtt.topicPrefix = argv[++i];
        } else if (!strcmp(argv[i], "--mqtt-control")) {
            mqtt.allowControl = true;
        } else if (!strcmp(argv[i], "--udp")) {
            options.link = System::LinkType::UDP;
            haveLink = true;
        } else {
            return false;
        }
    }
    return haveLink;
}

static void OnWatchdog()
{
    // Runs until process exit; checks every watchdog-enabled dmq thread.
    for (;;) {
        dmq::os::Thread::WatchdogCheckAll();
        dmq::os::Thread::Sleep(std::chrono::milliseconds(500));
    }
}

int main(int argc, char* argv[])
{
    System::Options options;
    bool selfTest = false;
    SpyOptions spy;
    MqttOptions mqtt;
    if (!ParseArgs(argc, argv, options, selfTest, spy, mqtt)) {
        PrintUsage(argv[0]);
        return 1;
    }
#if !defined(PUMPTRON_HAVE_MQTT)
    if (!mqtt.brokerUri.empty()) {
        fprintf(stderr, "Pumptron GUI: this build has no MQTT support (Paho MQTT C not found at configure time)\n");
        return 1;
    }
#endif

    ::InstallCrashHandlers();
    static dmq::util::NetworkContext networkContext;

    // Before the link starts, so the first messages are already labeled for
    // the Bus Monitor pane and dmq-spy.
    gui::RegisterStringifiers();

    // SpyBridge sends everything on this node's DataBus to dmq-spy over UDP --
    // including every message received from the controller, which the link
    // re-publishes locally. Started before the full-screen UI, since it logs
    // a line to stdout.
    if (spy.enabled)
        SpyBridge::Start(spy.host, static_cast<uint16_t>(spy.port), "GUI");

    std::string error;
    if (!System::GetInstance().Initialize(options, error)) {
        fprintf(stderr, "Pumptron GUI: %s\n", error.c_str());
        if (spy.enabled)
            SpyBridge::Stop();
        System::GetInstance().Shutdown();
        return 1;
    }

#if defined(PUMPTRON_HAVE_MQTT)
    // After the link is up. The bridge re-publishes retained state on connect,
    // so a status received before the broker connection isn't lost.
    static dmq::ScopedConnection mqttEventConn;
    bool mqttStarted = false;
    if (!mqtt.brokerUri.empty()) {
        // In the UI, bridge events (connect, disconnect, rejected commands) go to
        // the Events pane; printing would corrupt the full-screen display. The
        // self-test leaves them on stderr.
        if (!selfTest) {
            mqttEventConn = JsonTopics::OnEvent().Connect(dmq::MakeDelegate([](const std::string& text) {
                System::GetInstance().OnEvent(text);    // already prefixed "MQTT: " by the bridge
            }));
        }
        std::string control = mqtt.allowControl
            ? ", accepting commands on " + mqtt.topicPrefix + "/pump/cmd/set" : std::string();
        printf("MQTT gateway: %s, topics under \"%s/\"%s\n", mqtt.brokerUri.c_str(), mqtt.topicPrefix.c_str(),
               control.c_str());
        MqttGatewayOptions gatewayOptions;
        gatewayOptions.brokerUri = mqtt.brokerUri;
        gatewayOptions.topicPrefix = mqtt.topicPrefix;
        gatewayOptions.allowControl = mqtt.allowControl;
        if (!StartMqttGateway(gatewayOptions)) {
            fprintf(stderr, "Pumptron GUI: could not start the MQTT gateway for %s\n", mqtt.brokerUri.c_str());
            if (spy.enabled)
                SpyBridge::Stop();
            System::GetInstance().Shutdown();
            return 1;
        }
        mqttStarted = true;
    }
#endif

    static dmq::os::Thread watchdogThread{ "GUI_Watchdog" };
    watchdogThread.CreateThread();
    (void)dmq::MakeDelegate(&OnWatchdog, watchdogThread).AsyncInvoke();

    int result = 0;
    if (selfTest) {
        result = gui::RunSelfTest();
    } else {
        // Blocks until the operator quits.
        gui::UI::GetInstance().Run(System::GetInstance().GetLinkDescription());
    }

#if defined(PUMPTRON_HAVE_MQTT)
    if (mqttStarted)
        StopMqttGateway();      // publishes <prefix>/online = false
    mqttEventConn.Disconnect();
#endif
    if (spy.enabled)
        SpyBridge::Stop();
    System::GetInstance().Shutdown();

    // The watchdog thread never returns from OnWatchdog(); skip static
    // destructors that would try to join it.
    std::_Exit(result);
}
