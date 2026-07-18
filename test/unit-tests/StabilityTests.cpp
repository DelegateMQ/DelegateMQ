#include "DelegateMQ.h"
#include "extras/util/TransportMonitor.h"
#include "extras/dispatcher/Dispatcher.h"
#include "port/transport/ITransport.h"
#include <iostream>
#include <cassert>

using namespace dmq;
using namespace dmq::transport;
using namespace dmq::util;

namespace {

// Mock transport to capture Send calls
class MockTransport : public ITransport {
public:
    int lastSentSize = 0;
    int sendCount = 0;
    ITransportMonitor* monitor = nullptr;

    int Send(dmq::xostringstream& os, const DmqHeader& header) override {
        sendCount++;
        lastSentSize = (int)os.str().size();
        if (monitor) {
            if (!monitor->Add(header.GetSeqNum(), header.GetId())) {
                return -1; // Congestion
            }
        }
        return 0;
    }
    int Receive(dmq::xstringstream&, DmqHeader&) override { return 0; }
    void SetTransportMonitor(ITransportMonitor* m) { monitor = m; }
};

void Test_DispatcherStreamReset() {
    std::cout << "  Running: Test_DispatcherStreamReset..." << std::endl;
    
    MockTransport transport;
    dmq::Dispatcher dispatcher;
    dispatcher.SetTransport(&transport);

    dmq::xostringstream stream;
    
    // First dispatch: 10 bytes
    stream << "0123456789"; 
    dispatcher.Dispatch(stream, 1);
    assert(transport.lastSentSize == 10);
    assert(transport.sendCount == 1);

    // Second dispatch: should ONLY be the new 5 bytes, not 15!
    stream << "abcde";
    dispatcher.Dispatch(stream, 2);
    
    if (transport.lastSentSize != 5) {
        std::cerr << "FAIL: Stream did not reset! Size was " << transport.lastSentSize << " (expected 5)" << std::endl;
        ASSERT_TRUE(false);
    }
    assert(transport.sendCount == 2);
}

void Test_TransportMonitorCongestion() {
    std::cout << "  Running: Test_TransportMonitorCongestion..." << std::endl;

    TransportMonitor monitor;
    MockTransport transport;
    transport.SetTransportMonitor(&monitor);

    dmq::Dispatcher dispatcher;
    dispatcher.SetTransport(&transport);

    dmq::xostringstream stream;

    // Fill up to the limit (100)
    for (int i = 0; i < (int)dmq::MAX_TRANSPORT_MONITOR_PENDING; ++i) {
        stream << "test";
        int err = dispatcher.Dispatch(stream, 100);
        assert(err == 0);
    }

    // The 101st message should fail synchronously
    stream << "overflow";
    int err = dispatcher.Dispatch(stream, 101);

    if (err != -1) {
        std::cerr << "FAIL: TransportMonitor did not reject overflow! (Limit: " << dmq::MAX_TRANSPORT_MONITOR_PENDING << ")" << std::endl;
        ASSERT_TRUE(false);
    }
}

} // namespace

void RunStabilityTests() {
    std::cout << "RunStabilityTests..." << std::endl;
    Test_DispatcherStreamReset();
    Test_TransportMonitorCongestion();
}
