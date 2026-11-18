#ifndef COMMAND_MSG_H
#define COMMAND_MSG_H

#include "DelegateMQ.h"

class CommandMsg : public serialize::I
{
public:
    enum class Action { START, STOP };

    Action action = Action::START;
    uint32_t pollTime = 0;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, action);
        ms.write(os, pollTime);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override
    {
        ms.read(is, action);
        ms.read(is, pollTime);
        return is;
    }
};

#endif