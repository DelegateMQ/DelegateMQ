#ifndef DMQ_RAPIDJSON_SERIALIZER_H
#define DMQ_RAPIDJSON_SERIALIZER_H

/// @file RapidJsonSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using RapidJSON for transport
/// to a remote. JSON is text, so endianness doesn't apply.
///
/// Each argument type provides:
/// @code
///   void Write(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer, const dmq::ByteWriter& os);
///   void Read(rapidjson::Document& doc, dmq::ByteReader& is);
/// @endcode
/// Read() calls `is.setfail()` on a missing or bad member.
///
/// Errors are reported through the writer/reader flags (and any exception
/// from an argument's Write()/Read() propagates); DelegateRemote reports them
/// as ERR_SERIALIZE / ERR_DESERIALIZE.

#include "delegate/ISerializer.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"

namespace dmq::serialization::rapidjson {

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data using RapidJSON
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Write arguments into the buffer, replacing what it held: transports send
    // the whole buffer, so bytes left over from a longer previous message would
    // follow the JSON and fail the receiver's parse.
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override {
        os.reset();
        ::rapidjson::StringBuffer sb;
        ::rapidjson::PrettyWriter<::rapidjson::StringBuffer> writer(sb);
        (args.Write(writer, os), ...);  // C++17 fold expression to write each argument
        if (writer.IsComplete())
            os.write(sb.GetString(), sb.GetSize());
        else
            os.setfail();
        return os;
    }

    // Read arguments from the received bytes: the rest of them are one JSON
    // document, parsed in place (no copy, no null terminator needed)
    virtual dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override {
        const char* data = is.data() + is.tellg();
        const size_t size = is.remaining();
        if (size == 0 || !is.good())
        {
            is.setfail();
            return is;
        }

        // Parse JSON; a parse error fails the reader (ERR_DESERIALIZE)
        ::rapidjson::Document doc;
        doc.Parse(data, size);
        if (doc.HasParseError())
        {
            is.setfail();
            return is;
        }
        is.skip(size);

        (args.Read(doc, is), ...);  // C++17 fold expression to read each argument
        return is;
    }
};

} // namespace dmq::serialization::rapidjson


#endif // DMQ_RAPIDJSON_SERIALIZER_H
