#ifndef RECEIVER_H
#define RECEIVER_H

/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief ZeroMQ with remote delegates examples. 

#include "DelegateMQ.h"
#include "data.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;
using namespace dmq::serialization::bitsery;
using namespace std;

/// @brief Receiver receives data from the Sender.
class Receiver
{
public:
    Receiver(DelegateRemoteId id) :
        m_id(id),
        m_thread("Receiver"),
        m_argStream(ios::in | ios::out | ios::binary)
    {
        // Set the delegate interfaces
        m_recvDelegate = MakeDelegate(this, &Receiver::DataUpdate, id);
        m_recvDelegate.SetStream(&m_argStream);
        m_recvDelegate.SetSerializer(&m_serializer);

        // Set the transport
        m_transport.Create(ZeroMqTransport::Type::SUB, "tcp://localhost:5555");

        // Create the receiver thread
        m_thread.CreateThread();
    }

    ~Receiver()
    {
        m_thread.ExitThread();
        m_recvDelegate = nullptr;
    }

    void Start()
    {
        // Start a timer to poll data
        m_recvTimerConn = m_recvTimer.OnExpired.Connect(MakeDelegate(this, &Receiver::Poll, m_thread));
        m_recvTimer.Start(std::chrono::milliseconds(50));
    }

    void Stop()
    {
        m_recvTimerConn.Disconnect();
        m_recvTimer.Stop();
        m_thread.ExitThread();
        m_recvTimer.Stop();
    }

private:
    // Poll called periodically on m_thread context
    void Poll()
    {
        // Get incoming data
        xstringstream arg_data(std::ios::in | std::ios::out | std::ios::binary);
        DmqHeader header;
        int error = m_transport.Receive(arg_data, header);

        // Incoming remote delegate data arrived?
        if (!error && !arg_data.str().empty())
        {
            // Invoke the receiver target function with the sender's argument data
            m_recvDelegate.Invoke(arg_data);
        }
    }

    // Receiver target function called when sender remote delegate is invoked
    void DataUpdate(Data& data)
    {
        if (data.dataPoints.size() > 0)
            cout << data.msg << " " << data.dataPoints[0].x << " " << data.dataPoints[0].y << endl;
        else
            cout << "DataUpdate incoming data error!" << endl;
    }

    DelegateRemoteId m_id = INVALID_REMOTE_ID;
    Thread m_thread;
    Timer m_recvTimer;
    dmq::ScopedConnection m_recvTimerConn;

    xostringstream m_argStream;
    ZeroMqTransport m_transport;
    Serializer<void(Data&)> m_serializer;

    // Receiver remote delegate
    DelegateMemberRemote<Receiver, void(Data&)> m_recvDelegate;
};

#endif
