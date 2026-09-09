#ifndef SENDER_H
#define SENDER_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "DelegateMQ.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"
#include "data.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;
using namespace dmq::serialization::serializer;
using namespace std;

/// @brief Sender is an active object with a thread. The thread sends data to the 
/// Receiver every time the timer expires. 
class Sender
{
public:
    Sender(DelegateRemoteId id) :
        m_thread("Sender"),
        m_sendDelegate(id),
        m_argStream(ios::in | ios::out | ios::binary),
        m_transportMonitor(std::chrono::milliseconds(500)),
        m_retryMonitor(m_transport, m_transportMonitor, 3),
        m_reliableTransport(m_transport, m_retryMonitor)
    {
        // Link monitors
        m_transport.SetTransportMonitor(&m_transportMonitor);
        m_dispatcher.SetTransport(&m_reliableTransport);

        // Subscribe to the Status Signal
        m_statusHandle = m_transportMonitor.OnSendStatus.Connect(MakeDelegate(this, &Sender::OnSendStatus));

        // Set the delegate interfaces
        m_sendDelegate.SetStream(&m_argStream);
        m_sendDelegate.SetSerializer(&m_serializer);
        m_sendDelegate.SetDispatcher(&m_dispatcher);
        m_sendDelegate.SetErrorHandler(MakeDelegate(this, &Sender::ErrorHandler));

        // Set the transport
        m_transport.Create(UdpTransport::Type::PUB, "127.0.0.1", 8080);
        
        // Create the sender thread
        m_thread.CreateThread();
    }

    ~Sender() { Stop(); }

    void Start()
    {
        // Start a timer to send data
        m_sendTimerConn = m_sendTimer.OnExpired.Connect(MakeDelegate(this, &Sender::Send, m_thread));
        m_sendTimer.Start(std::chrono::milliseconds(50));
    }

    void Stop()
    {
        m_sendTimerConn.Disconnect();
        m_sendTimer.Stop();
        m_thread.ExitThread();
        m_transport.Close();
    }

    // Send data to the remote
    void Send()
    {
        Data data;
        for (int i = 0; i < 5; i++)
        {
            DataPoint dataPoint;
            dataPoint.x = x++;
            dataPoint.y = y++;
            data.dataPoints.push_back(dataPoint);
        }
        data.msg = "Data Message:";

        DataAux dataAux;
        dataAux.auxMsg = "Aux Message";

        // 1. Send the message
        m_sendDelegate(data, dataAux);

        // 2. Poll for the ACK
        // We try to read from the socket. 
        // If an ACK is waiting, Transport::Receive automatically processes it 
        // and calls m_transportMonitor->Remove(seq).
        xstringstream ss;
        DmqHeader h;
        m_transport.Receive(ss, h);

        // 3. Check for timeouts
        m_transportMonitor.Process();
    }

private:
    void ErrorHandler(DelegateRemoteId, DelegateError err, DelegateErrorAux)
    {
        if (err != dmq::DelegateError::SUCCESS)
            std::cout << "ErrorHandler " << (int)err << std::endl;
    }

    void OnSendStatus(DelegateRemoteId id, uint16_t seqNum, TransportMonitor::Status status)
    {
        if (status == TransportMonitor::Status::TIMEOUT)
        {
            std::cout << ">> TIMEOUT! Remote: " << id << " Seq: " << seqNum << " expired. Retrying..." << std::endl;
        }
    }

    Thread m_thread;
    Timer m_sendTimer;
    dmq::ScopedConnection m_sendTimerConn;

    xostringstream m_argStream;
    Dispatcher m_dispatcher;

    UdpTransport m_transport;           // 1. Initialized first
    TransportMonitor m_transportMonitor;// 2. Initialized second (Used by RetryMonitor)
    RetryMonitor m_retryMonitor;        // 3. Initialized third (Depends on above)
    ReliableTransport m_reliableTransport;

    Serializer<void(Data&, DataAux&)> m_serializer;

    // Sender remote delegate
    DelegateMemberRemote<Sender, void(Data&, DataAux&)> m_sendDelegate;

    dmq::ScopedConnection m_statusHandle;

    int x = 0;
    int y = 0;
};

#endif