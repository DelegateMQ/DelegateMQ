#include "DelegateMQ.h"
#include <iostream>
#include <vector>
#include <string>

using namespace dmq::databus;

class MockTransport : public dmq::transport::ITransport {
public:
    int Send(dmq::xostringstream&, const dmq::transport::DmqHeader&) override { return 0; }
    int Receive(dmq::xstringstream&, dmq::transport::DmqHeader&) override { return -1; }
};

// A serializer that can be made to fail
template <typename T>
class FailingSerializer : public dmq::ISerializer<void(T)> {
public:
    bool fail = false;
    std::ostream& Write(std::ostream& os, const T& data) override {
        if (fail) {
            // Note: Test 2 assumes this exception is caught by the RemoteChannel layer
            // and converted into a dmq::DelegateError::ERR_SERIALIZE.
            throw std::runtime_error("Serialization failed");
        }
        // Simple mock write
        os.write("x", 1);
        return os;
    }
    std::istream& Read(std::istream& is, T& data) override {
        char c;
        is.read(&c, 1);
        return is;
    }
};

int DataBusErrorTest() {
    std::cout << "Starting DataBusErrorTest..." << std::endl;

    // Test 1: ERR_NO_SERIALIZER
    {
        DataBus::ResetForTesting();
        MockTransport transport;
        auto participant = dmq::xmake_shared<Participant>(transport);
        
        participant->AddRemoteTopic("test/topic", 100);
        DataBus::AddParticipant(participant);

        dmq::DelegateError capturedError = dmq::DelegateError::SUCCESS;
        dmq::xstring capturedTopic;
        auto conn = DataBus::SubscribeError([&](const dmq::xstring& topic, dmq::DelegateError error) {
            capturedTopic = topic;
            capturedError = error;
        });

        // Publish to a remote topic that has no serializer registered
        DataBus::Publish<int>("test/topic", 42);

        if (capturedTopic == "test/topic" && capturedError == dmq::DelegateError::ERR_NO_SERIALIZER) {
            std::cout << "Test 1 Passed: Caught ERR_NO_SERIALIZER" << std::endl;
        } else {
            std::cerr << "Test 1 Failed! Error: " << (int)capturedError << " Topic: " << capturedTopic << std::endl;
            return 1;
        }
    }

    // Test 2: ERR_SERIALIZE (Forwarded from DelegateRemote)
    {
        DataBus::ResetForTesting();
        MockTransport transport;
        auto participant = std::make_shared<Participant>(transport);
        
        // This test assumes synchronous dispatch (no send thread set).
        // If a send thread were used, we would need a sync mechanism here.
        participant->AddRemoteTopic("test/topic", 100);
        DataBus::AddParticipant(participant);

        FailingSerializer<int> serializer;
        serializer.fail = true; // Make it fail
        DataBus::RegisterSerializer<int>("test/topic", serializer);

        dmq::DelegateError capturedError = dmq::DelegateError::SUCCESS;
        dmq::xstring capturedTopic;
        auto conn = DataBus::SubscribeError([&](const dmq::xstring& topic, dmq::DelegateError error) {
            capturedTopic = topic;
            capturedError = error;
        });

        // This should trigger ERR_SERIALIZE inside DelegateRemote, which should be forwarded
        DataBus::Publish<int>("test/topic", 42);

        if (capturedTopic == "test/topic" && capturedError == dmq::DelegateError::ERR_SERIALIZE) {
            std::cout << "Test 2 Passed: Caught ERR_SERIALIZE" << std::endl;
        } else {
            std::cerr << "Test 2 Failed! Error: " << (int)capturedError << " Topic: " << capturedTopic << std::endl;
            return 1;
        }
    }

    // Test 3: ERR_NO_SERIALIZER fires only once per topic regardless of publish count
    {
        DataBus::ResetForTesting();
        MockTransport transport;
        auto participant = std::make_shared<Participant>(transport);

        participant->AddRemoteTopic("test/topic", 100);
        DataBus::AddParticipant(participant);

        int errorCount = 0;
        auto conn = DataBus::SubscribeError([&](const dmq::xstring&, dmq::DelegateError) {
            ++errorCount;
        });

        DataBus::Publish<int>("test/topic", 1);
        DataBus::Publish<int>("test/topic", 2);
        DataBus::Publish<int>("test/topic", 3);

        if (errorCount == 1) {
            std::cout << "Test 3 Passed: ERR_NO_SERIALIZER reported once despite 3 publishes" << std::endl;
        } else {
            std::cerr << "Test 3 Failed! errorCount=" << errorCount << " (expected 1)" << std::endl;
            return 1;
        }
    }

    // Test 4: ResetForTesting() clears dedup state so errors fire again in the next test
    {
        DataBus::ResetForTesting();
        MockTransport transport;
        auto participant = std::make_shared<Participant>(transport);

        participant->AddRemoteTopic("test/topic", 100);
        DataBus::AddParticipant(participant);

        dmq::DelegateError capturedError = dmq::DelegateError::SUCCESS;
        auto conn = DataBus::SubscribeError([&](const dmq::xstring&, dmq::DelegateError error) {
            capturedError = error;
        });

        DataBus::Publish<int>("test/topic", 42);

        if (capturedError == dmq::DelegateError::ERR_NO_SERIALIZER) {
            std::cout << "Test 4 Passed: Error fires again after ResetForTesting()" << std::endl;
        } else {
            std::cerr << "Test 4 Failed! Error: " << (int)capturedError << std::endl;
            return 1;
        }
    }

    std::cout << "DataBusErrorTest Finished Successfully!" << std::endl;
    return 0;
}

int DataBusErrorTestMain() {
    try {
        return DataBusErrorTest();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
