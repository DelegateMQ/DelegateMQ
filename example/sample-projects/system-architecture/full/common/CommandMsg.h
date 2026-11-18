#ifndef COMMAND_MSG_H
#define COMMAND_MSG_H

#include <msgpack.hpp>

class CommandMsg
{
public:
    enum class Action { START, STOP };

    Action action = Action::START;
    uint32_t pollTime = 0;

    MSGPACK_DEFINE(action, pollTime);
};

// Specialize the msgpack serialization for Command::Action
MSGPACK_ADD_ENUM(CommandMsg::Action);


#endif