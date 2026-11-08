#ifndef ALARM_MGR_H
#define ALARM_MGR_H

#include "DelegateMQ.h"
#include "NetworkMgr.h"
#include "AlarmMsg.h"
#include <string>
#include <iostream> 

// AlarmMgr handles client and server alarms.
class AlarmMgr
{
public:
    static AlarmMgr& Instance()
    {
        static AlarmMgr instance;
        return instance;
    }

    void SetAlarm(AlarmMsg& msg, AlarmNote& note)
    {
#ifdef SERVER_APP
        // Send alarm to client for handling
        NetworkMgr::Instance().SendAlarmMsg(msg, note);
#else
        // Reinvoke function call on the AlarmMgr thread of control
        if (!m_thread.IsCurrentThread())
            return MakeDelegate(this, &AlarmMgr::SetAlarm, m_thread)(msg, note);

        // Client handles alarm locally
        AlarmHandler(msg, note);
#endif
    }

private:
    AlarmMgr() :
        m_thread("AlarmMgr")
    {
        m_thread.CreateThread();

        // Use Connect() and store handle
        m_errorConn = NetworkMgr::Instance().OnNetworkError.Connect(MakeDelegate(this, &AlarmMgr::ErrorHandler, m_thread));

        // Create delegate, set priority, then connect
        auto alarmDel = MakeDelegate(this, &AlarmMgr::RecvAlarmMsg, m_thread);
        alarmDel.SetPriority(dmq::Priority::HIGH);  // Alarm messages high priority

        // Use Connect() and store handle
        m_alarmConn = NetworkMgr::Instance().OnAlarm.Connect(alarmDel);
    }

    ~AlarmMgr()
    {
        // Exit first: queued messages run while this object is still whole.
        // The ScopedConnection members then disconnect as they're destroyed.
        // Safe only because the emitter (NetworkMgr) is stopped before this
        // singleton is destroyed. See docs/APP-GUIDE.md#shutdown-and-object-lifetime.
        m_thread.ExitThread();
    }

    void AlarmHandler(AlarmMsg& msg, AlarmNote& note)
    {
        if (msg.source == AlarmMsg::Source::CLIENT)
        {
            std::cout << "Client Alarm: " << note.note << std::endl;
        }
        else if (msg.source == AlarmMsg::Source::SERVER)
        {
            std::cout << "Server Alarm: " << note.note << std::endl;
        }
    }

    void RecvAlarmMsg(AlarmMsg& msg, AlarmNote& note)
    {
        AlarmHandler(msg, note);
    }

    void ErrorHandler(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux aux)
    {
        if (error != dmq::DelegateError::SUCCESS && id == ids::ALARM_MSG_ID)
            std::cout << "AlarmMgr Error: " << id << " " << (int)error << " " << aux << std::endl;
    }

    dmq::os::Thread m_thread;

    // RAII Connections
    dmq::ScopedConnection m_errorConn;
    dmq::ScopedConnection m_alarmConn;
};

#endif