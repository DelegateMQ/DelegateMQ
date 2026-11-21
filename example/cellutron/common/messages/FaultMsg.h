#ifndef FAULT_MSG_H
#define FAULT_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message to notify system of a critical fault.
struct FaultMsg : public MessageBase
{
    uint8_t faultCode = 0;

    FaultMsg() = default;
    FaultMsg(uint8_t code) : faultCode(code) {}

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, faultCode);
    }

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, faultCode);
    }
};

} // namespace cellutron

#endif
