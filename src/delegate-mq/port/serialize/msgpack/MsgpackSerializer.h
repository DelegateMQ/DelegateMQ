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
#include <type_traits>

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
    // Write arguments into the buffer, replacing what it held (DelegateMQ
    // reuses one buffer per remote delegate)
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override {
        os.reset();

        ::msgpack::sbuffer buffer;
        make_serialized(buffer, args...);
        os.write(buffer.data(), buffer.size());
        return os;
    }

    // Read arguments from the received bytes (unpacked in place, no copy)
    virtual dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override {
        const char* data = is.data() + is.tellg();
        const size_t size = is.remaining();

        // No data for the arguments: fail, so the caller reports a
        // deserialize error instead of invoking with default values.
        if (size == 0 && sizeof...(Args) > 0) {
            is.setfail();
            return is;
        }

        size_t offset = 0;

        // Helper lambda to unpack one argument at a time from the buffer
        auto unpack_one = [&](auto& arg) {
            // msgpack::unpack parses one object and updates 'offset' to point to the next byte
            ::msgpack::object_handle oh = ::msgpack::unpack(data, size, offset);

            // Convert msgpack object to specific C++ type
            arg = oh.get().as<std::decay_t<decltype(arg)>>();
            };

        // Use C++17 fold expression to call unpack_one for each argument in 'args'
        (unpack_one(args), ...);
        is.skip(offset);
        return is;
    }
};

} // namespace dmq::serialization::msgpack


#endif // DMQ_MSGPACK_SERIALIZER_H
