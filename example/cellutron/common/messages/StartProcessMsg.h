#ifndef START_PROCESS_MSG_H
#define START_PROCESS_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message to command the start of the cell process.
struct StartProcessMsg : public MessageBase
{
    StartProcessMsg() = default;

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        return MessageBase::read(ms, is);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        return MessageBase::write(ms, os);
    }
};

} // namespace cellutron

#endif
