#ifndef DMQ_BITSERY_SERIALIZER_H
#define DMQ_BITSERY_SERIALIZER_H

/// @file BitserySerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using Bitsery for transport
/// to a remote. Bitsery provides fast, compact binary serialization.

#include "delegate/ISerializer.h"

#if defined(DMQ_SERIALIZE_BITSERY)

// Core Bitsery
#include <bitsery/bitsery.h>
#include <bitsery/adapter/buffer.h>

// Common Traits (Include these so standard types work out of the box)
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/list.h>

#include <type_traits>

namespace dmq::serialization::bitsery {

template <class R>
struct Serializer; // Not defined

template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Bitsery buffer adapters: writes go to an xstring (bitsery's string traits
    // grow it), then into the ByteWriter; reads run over the received bytes in
    // place (BufferAdapterTraits<const T*>), with no copy.
    using OutputAdapter = ::bitsery::OutputBufferAdapter<dmq::xstring>;
    using InputAdapter = ::bitsery::InputBufferAdapter<const char*>;

    // No try/catch here: this adapter must build without exceptions (-fno-exceptions),
    // the common embedded setting. Failures are reported through the writer/reader
    // flags, which DelegateRemote checks (ERR_DESERIALIZE / ERR_BUFFER).
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override {
        // Replace what the buffer held: DelegateMQ reuses it per remote delegate
        os.reset();

        dmq::xstring buffer;
        ::bitsery::Serializer<OutputAdapter> writer{ buffer };

        // Serialize each argument using C++17 fold expression
        (writer.object(args), ...);

        writer.adapter().flush();
        os.write(buffer.data(), writer.adapter().writtenBytesCount());
        return os;
    }

    virtual dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override {
        ::bitsery::Deserializer<InputAdapter> reader{ is.data() + is.tellg(), is.remaining() };

        // Deserialize each argument using fold expression
        (reader.object(args), ...);

        // A truncated or malformed message: fail the reader.
        if (reader.adapter().error() != ::bitsery::ReaderError::NoError) {
            is.setfail();
        } else {
            is.skip(reader.adapter().currentReadPos());
        }
        return is;
    }
};

} // namespace dmq::serialization::bitsery

#endif // DMQ_SERIALIZE_BITSERY

#endif // DMQ_BITSERY_SERIALIZER_H
