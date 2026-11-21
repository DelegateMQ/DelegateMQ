#ifndef SENDER_H
#define SENDER_H

/// @file sender.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "DelegateMQ.h"
#include <iostream>
#include "transport.h"
#include "data.h"
#if SAMPLE_ACKS
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"
#include <atomic>
#endif

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;
using namespace std;

/// @brief Sender is an active object with a thread. The thread sends data to the
/// Receiver every time the timer expires.
class Sender
{
public:
    Sender(DelegateRemoteId id) :
        m_thread("Sender"),
#if SAMPLE_ACKS
        m_transportMonitor(std::chrono::milliseconds(500)),
        m_retryMonitor(m_transport, m_transportMonitor, 3),
        m_reliableTransport(m_transport, m_retryMonitor),
#endif
        m_sendDelegate(id)
    {
#if SAMPLE_ACKS
        // Acknowledgments: the monitor tracks each sent message until the
        // receiver acknowledges it; the reliable transport resends on timeout.
        m_transport.SetTransportMonitor(&m_transportMonitor);
        m_dispatcher.SetTransport(&m_reliableTransport);
        m_statusConn = m_transportMonitor.OnSendStatus.Connect(MakeDelegate(this, &Sender::OnSendStatus));
#else
        m_dispatcher.SetTransport(&m_transport);
#endif

        // Set the delegate interfaces
        m_sendDelegate.SetStream(&m_argStream);
        m_sendDelegate.SetSerializer(&m_serializer);
        m_sendDelegate.SetDispatcher(&m_dispatcher);
        m_sendDelegate.SetErrorHandler(MakeDelegate(this, &Sender::ErrorHandler));

        // Set the transport
        CreateSenderTransport(m_transport);

        // Create the sender thread
        m_thread.CreateThread();
    }

    ~Sender() { Stop(); }

    /// Messages the receiver acknowledged, and acknowledgment timeouts (each
    /// retry of a message that isn't acknowledged in time counts once).
    /// Both stay 0 without acknowledgments (SAMPLE_ACKS 0).
    int AckedCount() const { return m_acked; }
    int TimeoutCount() const { return m_timeouts; }

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

#if SAMPLE_AUX
        DataAux dataAux;
        dataAux.auxMsg = "Aux Message";
        m_sendDelegate(data, dataAux);
#else
        m_sendDelegate(data);
#endif

#if SAMPLE_ACKS
        // Read any acknowledgment waiting on the socket. Receive() handles an
        // ACK itself (it tells the monitor the message was delivered).
        dmq::ByteReader ss;
        DmqHeader h;
        m_transport.Receive(ss, h);

        // Report (and retry) messages not acknowledged in time
        m_transportMonitor.Process();
#endif
    }

private:
    void ErrorHandler(DelegateRemoteId, DelegateError err, DelegateErrorAux)
    {
        if (err != dmq::DelegateError::SUCCESS)
            std::cout << "ErrorHandler " << (int)err << std::endl;
    }

#if SAMPLE_ACKS
    void OnSendStatus(DelegateRemoteId id, uint16_t seqNum, TransportMonitor::Status status)
    {
        if (status == TransportMonitor::Status::SUCCESS) {
            m_acked++;
        } else {
            m_timeouts++;
            std::cout << ">> TIMEOUT! Remote: " << id << " Seq: " << seqNum << " expired." << std::endl;
        }
    }
#endif

    std::atomic<int> m_acked{ 0 };
    std::atomic<int> m_timeouts{ 0 };

    Thread m_thread;
    Timer m_sendTimer;
    dmq::ScopedConnection m_sendTimerConn;

    dmq::ByteWriter m_argStream;
    Dispatcher m_dispatcher;
    SampleSerializer<SampleSig> m_serializer;

    // Declaration order is construction order: the monitors take the transport
    SampleTransport m_transport;
#if SAMPLE_ACKS
    TransportMonitor m_transportMonitor;
    RetryMonitor m_retryMonitor;
    ReliableTransport m_reliableTransport;
    dmq::ScopedConnection m_statusConn;
#endif

    // Sender remote delegate
    DelegateMemberRemote<Sender, SampleSig> m_sendDelegate;

    int x = 0;
    int y = 0;
};

#endif
