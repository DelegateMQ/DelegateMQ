#ifndef RECEIVER_H
#define RECEIVER_H

/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief MQTT with remote delegates examples. 

#include "DelegateMQ.h"
#include "../common/data.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;
using namespace dmq::serialization::rapidjson;
using namespace std;

/// @brief Receiver receives data from the Sender.
class Receiver
{
public:
    dmq::MulticastDelegateSafe<void(Data&)> DataCb;

    Receiver() :
        m_thread("Receiver"),
        m_argStream(ios::in | ios::out | ios::binary)
    {
        // Set the delegate interfaces
        m_recvDelegate = MakeDelegate(this, &Receiver::DataUpdate, Data::DATA_ID);
        m_recvDelegate.SetStream(&m_argStream);
        m_recvDelegate.SetSerializer(&m_serializer);
        m_recvDelegate.SetErrorHandler(MakeDelegate(this, &Receiver::ErrorHandler));

        // Set the transport
        m_transport.Create(MqttTransport::Type::SUB);

        // Create the receiver thread
        m_thread.CreateThread();

        // Start the polling loop on the background thread
        MakeDelegate(this, &Receiver::ReceiveLoop, m_thread).AsyncInvoke();
    }

    ~Receiver()
    {
        // Set exit flag and stop transport to unblock the loop
        m_exit = true;
        m_transport.Close();

        m_thread.ExitThread();
        m_recvDelegate = nullptr;
    }

    void Start()
    {
    }

    void Stop()
    {
        m_exit = true;
        m_transport.Close();
        m_thread.ExitThread();
    }

private:
    void ReceiveLoop()
    {
        while (!m_exit)
        {
            // Get incoming data
            xstringstream arg_data(std::ios::in | std::ios::out | std::ios::binary);
            DmqHeader header;

            // This blocks for up to 1000ms (set in MqttTransport)
            int error = m_transport.Receive(arg_data, header);

            // Incoming remote delegate data arrived?
            if (!error && !m_exit)
            {
                // Invoke the receiver target function with the sender's argument data
                m_recvDelegate.Invoke(arg_data);
            }
        }
    }

    // Receiver target function called when sender remote delegate is invoked
    void DataUpdate(Data& data)
    {
        // Notify registered callback subscribers
        DataCb(data);
    }

    void ErrorHandler(DelegateRemoteId, DelegateError err, DelegateErrorAux)
    {
        if (err != dmq::DelegateError::SUCCESS)
            std::cout << "ErrorHandler " << (int)err << std::endl;
    }

    DelegateRemoteId m_id;
    Thread m_thread;
    std::atomic<bool> m_exit{ false }; 

    xostringstream m_argStream;
    MqttTransport m_transport;
    Serializer<void(Data&)> m_serializer;

    // Receiver remote delegate
    DelegateMemberRemote<Receiver, void(Data&)> m_recvDelegate;
};

#endif