#ifndef RUN_STATUS_MSG_H
#define RUN_STATUS_MSG_H

#include "MessageBase.h"

namespace cellutron {

enum class RunStatus { IDLE, PROCESSING, ABORTING, FAULT };

/// @brief Message containing the current run status.
struct RunStatusMsg : public MessageBase
{
    RunStatus status = RunStatus::IDLE;

    RunStatusMsg() = default;
    RunStatusMsg(RunStatus s) : status(s) {}

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        uint8_t s;
        ms.read(is, s);
        status = static_cast<RunStatus>(s);
        return is;
    }

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        uint8_t s = static_cast<uint8_t>(status);
        ms.write(os, s);
        return os;
    }
};

} // namespace cellutron

#endif
