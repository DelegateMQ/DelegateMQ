#ifndef DMQ_CEREAL_SERIALIZER_H
#define DMQ_CEREAL_SERIALIZER_H

/// @file CerealSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using Cereal library for transport
/// to a remote. Endianness handled internally by Cereal.
///
/// Cereal reports errors by throwing; the exception propagates to
/// DelegateRemote, which reports ERR_SERIALIZE / ERR_DESERIALIZE_EXCEPTION.

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "delegate/ISerializer.h"
#include <istream>
#include <ostream>
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
        os.seekp(0);
        if (auto* ss = dynamic_cast<dmq::xostringstream*>(&os))
            ss->str("");
        ::cereal::BinaryOutputArchive archive(os);
        (archive(args), ...); // C++17 fold expression to serialize each argument
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        ::cereal::BinaryInputArchive archive(is);
        (archive(args), ...); // C++17 fold expression to deserialize
        return is;
    }
};

} // namespace dmq::serialization::cereal


#endif // DMQ_CEREAL_SERIALIZER_H
