#ifndef COMMAND_MSG_H
#define COMMAND_MSG_H

#include "DelegateMQ.h"

class CommandMsg : public serialize::I
{
public:
    enum class Action { START, STOP };

    Action action = Action::START;
    uint32_t pollTime = 0;

    virtual ~CommandMsg() = default;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override
    {
        ms.write(os, action);
        ms.write(os, pollTime);
        return os;
    }

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override
    {
        ms.read(is, action);
        ms.read(is, pollTime);
        return is;
    }
};

#endif