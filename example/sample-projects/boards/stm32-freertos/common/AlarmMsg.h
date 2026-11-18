#ifndef ALARM_MSG_H
#define ALARM_MSG_H

#include "DelegateMQ.h"

class AlarmMsg : public serialize::I
{
public:
    enum class Source { CLIENT, SERVER };
    enum class Alarm { SENSOR_ERROR, ACTUATOR_ERROR };

    virtual ~AlarmMsg() = default;

    Source source;
    Alarm alarm;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, source);
        ms.write(os, alarm);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override
    {
        ms.read(is, source);
        ms.read(is, alarm);
        return is;
    }
};

class AlarmNote : public serialize::I
{
public:
    std::string note;

    virtual ~AlarmNote() = default;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, note);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override 
    {
        ms.read(is, note);
        return is;
    }
};

#endif