#ifndef DMQ_DMQ_SERIALIZER_H
#define DMQ_DMQ_SERIALIZER_H

/// @file DmqSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using serialize class for transport
/// to a remote. Endianness correctly handled by serialize class.
///
/// Errors are reported through the stream state (and any exception from a
/// user type's read()/write() propagates); DelegateRemote reports them as
/// ERR_SERIALIZE / ERR_DESERIALIZE. No try/catch here, per the library rules.

#include "delegate/ISerializer.h"
#include <istream>
#include <ostream>
#include "msg_serialize.h"

namespace dmq::serialization::serializer {

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data using serialize class
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Write arguments to a stream
    virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
        os.seekp(0, std::ios::beg);
        if (auto* ss = dynamic_cast<dmq::xostringstream*>(&os))
            ss->str("");
        ::serialize ser;
        (ser.write(os, args), ...);  // C++17 fold expression to serialize each argument
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        ::serialize ser;
        (ser.read(is, args), ...);  // C++17 fold expression to unserialize each argument
        return is;
    }
};

} // namespace dmq::serialization::serializer


#endif // DMQ_DMQ_SERIALIZER_H
