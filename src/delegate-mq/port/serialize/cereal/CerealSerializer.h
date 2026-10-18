#ifndef DMQ_CEREAL_SERIALIZER_H
#define DMQ_CEREAL_SERIALIZER_H

/// @file CerealSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// Serialize callable argument data using Cereal library for transport
/// to a remote. Endianness handled internally by Cereal.

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "delegate/ISerializer.h"
#include <iostream>
#include <sstream>

namespace dmq::serialization::cereal {

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data using Cereal
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Write arguments to a stream
    virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
        try {
            os.seekp(0);
            if (auto* ss = dynamic_cast<xostringstream*>(&os))
                ss->str("");
            ::cereal::BinaryOutputArchive archive(os);
            (archive(args), ...); // C++17 fold expression to serialize each argument
        }
        catch (const std::exception& e) {
            std::cerr << "Cereal serialize error: " << e.what() << std::endl;
            throw;
        }
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        try {
            ::cereal::BinaryInputArchive archive(is);
            (archive(args), ...); // C++17 fold expression to deserialize
        }
        catch (const std::exception& e) {
            std::cerr << "Cereal deserialize error: " << e.what() << std::endl;
            throw;
        }
        return is;
    }
};

} // namespace dmq::serialization::cereal


#endif // DMQ_CEREAL_SERIALIZER_H
