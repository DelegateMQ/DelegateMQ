#ifndef DMQ_RAPIDJSON_SERIALIZER_H
#define DMQ_RAPIDJSON_SERIALIZER_H

/// @file RapidJsonSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// Serialize callable argument data using RapidJSON for transport
/// to a remote. JSON is text, so endianness doesn't apply.
///
/// Errors are reported through the stream state (and any exception from an
/// argument's Write()/Read() propagates); DelegateRemote reports them as
/// ERR_SERIALIZE / ERR_DESERIALIZE.

#include "delegate/ISerializer.h"
#include <istream>
#include <ostream>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include <vector>

namespace dmq::serialization::rapidjson {

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
        // Empty a reused stream: transports send the whole buffer, so bytes left
        // over from a longer previous message would follow the JSON and fail
        // the receiver's parse.
        if (auto* ss = dynamic_cast<dmq::xostringstream*>(&os))
            ss->str("");
        ::rapidjson::StringBuffer sb;
        ::rapidjson::PrettyWriter<::rapidjson::StringBuffer> writer(sb);
        (args.Write(writer, os), ...);  // C++17 fold expression to write each argument
        if (writer.IsComplete())
            os << sb.GetString();
        else
            os.setstate(std::ios::failbit);
        return os;
    }

    // Read arguments from a stream
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        // Get stream length
        std::streampos current_pos = is.tellg();
        is.seekg(0, std::ios::end);
        std::streampos end_pos = is.tellg();
        is.seekg(current_pos, std::ios::beg);

        if (end_pos < 0 || !is.good())
        {
            is.setstate(std::ios::failbit);
            return is;
        }

        std::streamsize length = static_cast<std::streamsize>(end_pos);

        // Allocate storage buffer (null terminator appended for RapidJSON)
        std::vector<char> buf(static_cast<size_t>(length) + 1);
        is.rdbuf()->sgetn(buf.data(), length);
        buf[length] = 0;   // null terminate incoming data

        // Parse JSON; a parse error fails the stream (ERR_DESERIALIZE)
        ::rapidjson::Document doc;
        doc.Parse(buf.data());
        if (doc.HasParseError())
        {
            is.setstate(std::ios::failbit);
            return is;
        }

        (args.Read(doc, is), ...);  // C++17 fold expression to read each argument
        return is;
    }
};

} // namespace dmq::serialization::rapidjson


#endif // DMQ_RAPIDJSON_SERIALIZER_H
