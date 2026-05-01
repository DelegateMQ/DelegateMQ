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

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, faultCode);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, faultCode);
    }
};

} // namespace cellutron

#endif
