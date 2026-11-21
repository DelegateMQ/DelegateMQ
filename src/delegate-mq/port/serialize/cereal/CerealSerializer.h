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
#include <streambuf>

namespace dmq::serialization::cereal {

namespace detail {

/// Cereal's archives need std streams; DelegateMQ hands serializers a
/// ByteWriter / ByteReader. These stream buffers bridge the two, without a
/// copy. Only an app that picks Cereal pays for iostreams.
class WriterStreamBuf : public std::streambuf {
public:
    explicit WriterStreamBuf(dmq::ByteWriter& w) : m_w(w) {}
protected:
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        m_w.write(s, static_cast<size_t>(n));
        return m_w.good() ? n : 0;
    }
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);
        const char c = traits_type::to_char_type(ch);
        m_w.write(&c, 1);
        return m_w.good() ? ch : traits_type::eof();
    }
private:
    dmq::ByteWriter& m_w;
};

class ReaderStreamBuf : public std::streambuf {
public:
    explicit ReaderStreamBuf(dmq::ByteReader& r) {
        char* begin = const_cast<char*>(r.data() + r.tellg());  // read only: no put area
        setg(begin, begin, begin + r.remaining());
    }
    size_t consumed() const { return static_cast<size_t>(gptr() - eback()); }
};

} // namespace detail

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data using Cereal
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
public:
    // Write arguments into the buffer, replacing what it held
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override {
        os.reset();
        detail::WriterStreamBuf buf(os);
        std::ostream stream(&buf);
        {
            ::cereal::BinaryOutputArchive archive(stream);
            (archive(args), ...); // C++17 fold expression to serialize each argument
        }
        if (!stream.good())
            os.setfail();
        return os;
    }

    // Read arguments from the received bytes
    virtual dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override {
        detail::ReaderStreamBuf buf(is);
        std::istream stream(&buf);
        {
            ::cereal::BinaryInputArchive archive(stream);
            (archive(args), ...); // C++17 fold expression to deserialize
        }
        is.skip(buf.consumed());
        if (stream.fail())
            is.setfail();
        return is;
    }
};

} // namespace dmq::serialization::cereal


#endif // DMQ_CEREAL_SERIALIZER_H
