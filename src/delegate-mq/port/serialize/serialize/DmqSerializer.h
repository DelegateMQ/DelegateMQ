#ifndef DMQ_DMQ_SERIALIZER_H
#define DMQ_DMQ_SERIALIZER_H

/// @file DmqSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// Serialize callable argument data using serialize class for transport
/// to a remote. Endinaness correctly handled by serialize class. 

#include "delegate/ISerializer.h"
#include "msg_serialize.h"
#include <iostream>

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
#if defined(__cpp_exceptions)
        try {
            (ser.write(os, args), ...);  // C++17 fold expression to serialize each argument
        }
        catch (const std::exception& e) {
            std::cerr << "Serialize error: " << e.what() << std::endl;
            throw;
        }
#else
        // STM32 / No Exceptions
        (ser.write(os, args), ...);
#endif
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
#if defined(__cpp_exceptions)
        try {
            ::serialize ser;
            (ser.read(is, args), ...);  // C++17 fold expression to unserialize each argument
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize error: " << e.what() << std::endl;
            throw;
        }
#else
        // STM32 / No Exceptions
        ::serialize ser;
        (ser.read(is, args), ...);
#endif
        return is;
    }
};

} // namespace dmq::serialization::serializer


#endif // DMQ_DMQ_SERIALIZER_H
