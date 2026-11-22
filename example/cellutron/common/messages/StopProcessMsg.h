#ifndef STOP_PROCESS_MSG_H
#define STOP_PROCESS_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message to command the abort of the cell process.
struct StopProcessMsg : public MessageBase
{
    StopProcessMsg() = default;

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        return MessageBase::read(ms, is);
    }

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        return MessageBase::write(ms, os);
    }
};

} // namespace cellutron

#endif
