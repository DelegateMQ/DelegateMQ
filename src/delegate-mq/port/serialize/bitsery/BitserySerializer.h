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
#include <bitsery/adapter/stream.h>

// Common Traits (Include these so standard types work out of the box)
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/list.h>

#include <istream>
#include <ostream>
#include <type_traits>

namespace dmq::serialization::bitsery {

template <class R>
struct Serializer; // Not defined

template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Bitsery Adapters for std::ostream / std::istream
    using OutputAdapter = ::bitsery::OutputStreamAdapter;
    using InputAdapter = ::bitsery::InputStreamAdapter;

    // No try/catch here: this adapter must build without exceptions (-fno-exceptions),
    // the common embedded setting. Failures are reported through the stream state,
    // which DelegateRemote checks (ERR_DESERIALIZE / ERR_STREAM_NOT_GOOD).
    virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
        // Reset stream position.
        os.seekp(0, std::ios::beg);

        // Clear stringstreams explicitly to avoid appending new data to old data.
        // DelegateMQ often reuses the stream object.
        if (auto* ss = dynamic_cast<dmq::xostringstream*>(&os)) {
            ss->str("");
        }

        // Construct the adapter properly passing the stream
        ::bitsery::Serializer<OutputAdapter> writer{ OutputAdapter{os} };

        // Serialize each argument using C++17 fold expression
        (writer.object(args), ...);

        // Ensure buffer is flushed to the stream
        writer.adapter().flush();
        return os;
    }

    virtual std::istream& Read(std::istream& is, Args&... args) override {
        // Construct the adapter properly passing the stream
        ::bitsery::Deserializer<InputAdapter> reader{ InputAdapter{is} };

        // Deserialize each argument using fold expression
        (reader.object(args), ...);

        // A truncated or malformed message: fail the stream.
        if (reader.adapter().error() != ::bitsery::ReaderError::NoError) {
            is.setstate(std::ios::failbit);
        }
        return is;
    }
};

} // namespace dmq::serialization::bitsery

#endif // DMQ_SERIALIZE_BITSERY

#endif // DMQ_BITSERY_SERIALIZER_H
