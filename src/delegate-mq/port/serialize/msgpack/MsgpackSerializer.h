#ifndef DMQ_MSGPACK_SERIALIZER_H
#define DMQ_MSGPACK_SERIALIZER_H

/// @file MsgpackSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using MessagePack library for transport
/// to a remote. Endianness correctly handled by MessagePack library.
///
/// MessagePack reports errors by throwing (e.g. msgpack::type_error); the
/// exception propagates to DelegateRemote, which reports ERR_SERIALIZE /
/// ERR_DESERIALIZE_EXCEPTION.

#include "msgpack.hpp"
#include "delegate/ISerializer.h"
#include <istream>
#include <ostream>
#include <sstream> // Needed for dynamic_cast check
#include <type_traits>
#include <vector>

namespace dmq::serialization::msgpack {

// make_serialized serializes each remote function argument
template<typename... Args>
void make_serialized(::msgpack::sbuffer& buffer, Args&&... args) {
    (::msgpack::pack(buffer, args), ...);  // C++17 fold expression to serialize
}

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data using MessagePack library
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Write arguments to a stream
    virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
        // Reset stream position. DelegateMQ reuses the stream instance.
        // If we don't reset, we append new data to old data, sending [Old][New]
        // and the receiver will always deserialize [Old].
        os.seekp(0, std::ios::beg);

        // Optimization: If it's a stringstream, clear it completely to avoid
        // sending "tail garbage" if the new packet is smaller than the previous one.
        // (Note: Even with garbage tail, msgpack works because it stops reading
        // after the valid object, but clearing saves network bandwidth).
        if (auto* ss = dynamic_cast<dmq::xostringstream*>(&os)) {
            ss->str("");
        }

        ::msgpack::sbuffer buffer;
        make_serialized(buffer, args...);
        os.write(buffer.data(), buffer.size());
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        // Read entire stream into memory buffer
        std::vector<char> buffer_data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());

        // No data for the arguments: fail, so the caller reports a
        // deserialize error instead of invoking with default values.
        if (buffer_data.empty() && sizeof...(Args) > 0) {
            is.setstate(std::ios::failbit);
            return is;
        }

        size_t offset = 0;

        // Helper lambda to unpack one argument at a time from the buffer
        auto unpack_one = [&](auto& arg) {
            // msgpack::unpack parses one object and updates 'offset' to point to the next byte
            ::msgpack::object_handle oh = ::msgpack::unpack(buffer_data.data(), buffer_data.size(), offset);

            // Convert msgpack object to specific C++ type
            arg = oh.get().as<std::decay_t<decltype(arg)>>();
            };

        // Use C++17 fold expression to call unpack_one for each argument in 'args'
        (unpack_one(args), ...);
        return is;
    }
};

} // namespace dmq::serialization::msgpack


#endif // DMQ_MSGPACK_SERIALIZER_H
