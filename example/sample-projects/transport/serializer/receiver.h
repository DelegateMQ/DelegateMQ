#ifndef RECEIVER_H
#define RECEIVER_H

/// @file receiver.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "DelegateMQ.h"
#include <iostream>
#include "transport.h"
#include "data.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;
using namespace std;

/// @brief Receiver receives data from the Sender.
class Receiver
{
public:
    Receiver(DelegateRemoteId id) :
        m_id(id),
        m_thread("Receiver")
#if SAMPLE_ACKS
        , m_transportMonitor(std::chrono::milliseconds(2000))
#endif
    {
#if SAMPLE_ACKS
        // The transport sends an ACK for each message it receives
        m_transport.SetTransportMonitor(&m_transportMonitor);
#endif

        // Set the delegate interfaces
        m_recvDelegate = MakeDelegate(this, &Receiver::DataUpdate, id);
        m_recvDelegate.SetStream(&m_argStream);
        m_recvDelegate.SetSerializer(&m_serializer);

        // Set the transport
        CreateReceiverTransport(m_transport);

        // Create the receiver thread
        m_thread.CreateThread();
    }

    ~Receiver()
    {
        Stop();
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
        m_transport.Close();
    }

    /// Number of messages received intact
    int ReceivedCount() const { return m_received.load(); }

private:
    // Poll called periodically on m_thread context
    void Poll()
    {
        // Get incoming data
        dmq::ByteReader arg_data;
        DmqHeader header;
        int error = m_transport.Receive(arg_data, header);

        // Incoming remote delegate data arrived?
        if (!error && arg_data.size() > 0)
        {
            // Invoke the receiver target function with the sender's argument data
            m_recvDelegate.Invoke(arg_data);
        }

#if SAMPLE_ACKS
        m_transportMonitor.Process();
#endif
    }

    // Receiver target function called when sender remote delegate is invoked
#if SAMPLE_AUX
    void DataUpdate(Data& data, DataAux& dataAux)
    {
        if (data.dataPoints.size() == 5 && dataAux.auxMsg == "Aux Message")
        {
            cout << data.msg << " " << data.dataPoints[0].x << " " << data.dataPoints[0].y << " " << dataAux.auxMsg << endl;
            m_received++;
        }
        else
            cout << "DataUpdate incoming data error!" << endl;
    }
#else
    void DataUpdate(Data& data)
    {
        if (data.dataPoints.size() == 5)
        {
            cout << data.msg << " " << data.dataPoints[0].x << " " << data.dataPoints[0].y << endl;
            m_received++;
        }
        else
            cout << "DataUpdate incoming data error!" << endl;
    }
#endif

    DelegateRemoteId m_id = INVALID_REMOTE_ID;
    Thread m_thread;
    Timer m_recvTimer;
    dmq::ScopedConnection m_recvTimerConn;
    std::atomic<int> m_received{ 0 };

    dmq::ByteWriter m_argStream;
    SampleTransport m_transport;
    SampleSerializer<SampleSig> m_serializer;
#if SAMPLE_ACKS
    TransportMonitor m_transportMonitor;
#endif

    // Receiver remote delegate
    DelegateMemberRemote<Receiver, SampleSig> m_recvDelegate;
};

#endif
