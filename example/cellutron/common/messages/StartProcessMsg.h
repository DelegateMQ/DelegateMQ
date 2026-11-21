#ifndef START_PROCESS_MSG_H
#define START_PROCESS_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message to command the start of the cell process.
struct StartProcessMsg : public MessageBase
{
    StartProcessMsg() = default;

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        return MessageBase::read(ms, is);
    }

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        return MessageBase::write(ms, os);
    }
};

} // namespace cellutron

#endif
