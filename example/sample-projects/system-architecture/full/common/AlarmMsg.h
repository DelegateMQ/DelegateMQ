#ifndef ALARM_MSG_H
#define ALARM_MSG_H

#include <msgpack.hpp>

class AlarmMsg
{
public:
    enum class Source { CLIENT, SERVER };
    enum class Alarm { SENSOR_ERROR, ACTUATOR_ERROR };

    Source source;
    Alarm alarm;

    MSGPACK_DEFINE(source, alarm);
};

MSGPACK_ADD_ENUM(AlarmMsg::Source);
MSGPACK_ADD_ENUM(AlarmMsg::Alarm);

class AlarmNote
{
public:
    std::string note;

    MSGPACK_DEFINE(note);
};

#endif