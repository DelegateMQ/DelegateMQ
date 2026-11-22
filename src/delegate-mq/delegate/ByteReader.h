#ifndef DMQ_BYTE_READER_H
#define DMQ_BYTE_READER_H

/// @file ByteReader.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief A read cursor over one received message's bytes, for serializers to
/// read from.
///
/// @details Replaces the std::istream used before DelegateMQ 3.0 in
/// the deserialization path, without iostreams' cost. It doesn't own the
/// bytes: it views a transport's receive buffer, or a ByteWriter's data, so
/// receiving needs no copy. The bytes must outlive the reader.
///
/// Flags work like a stream's: reading past the end reads what's there and
/// sets eof() and fail(); peek() at the end sets eof() and returns END_OF_DATA.
/// good() is false once either is set. setfail() marks bad data (a serializer
/// rejecting a value); clear() resets the flags.
///
/// The method names follow std::istream's, so code written for a stream reads
/// the same.
///
/// @note Included by DelegateOpt.h, after ByteWriter.h: include DelegateOpt.h
/// (or DelegateMQ.h), not this header.

#include <cstddef>
#include <cstring>

namespace dmq {

class ByteReader
{
public:
    /// What peek() returns at the end of the data
    static constexpr int END_OF_DATA = -1;

    ByteReader() = default;
    ByteReader(const char* data, size_t size) noexcept { assign(data, size); }
    /// Read what a ByteWriter wrote (the writer must outlive the reader)
    explicit ByteReader(const ByteWriter& writer) noexcept { assign(writer.data(), writer.size()); }

    /// View other bytes, from the start, with the flags cleared
    void assign(const char* data, size_t size) noexcept
    {
        m_data = data;
        m_size = (data != nullptr) ? size : 0;
        m_pos = 0;
        m_fail = m_eof = false;
    }

    /// Copy size bytes into dest. Past the end: copies what's left, then sets
    /// eof() and fail().
    ByteReader& read(char* dest, size_t size)
    {
        if (m_fail || size == 0)
            return *this;
        if (dest == nullptr) {
            m_fail = true;
            return *this;
        }
        const size_t left = m_size - m_pos;
        const size_t n = (size <= left) ? size : left;
        if (n > 0)
            std::memcpy(dest, m_data + m_pos, n);
        m_pos += n;
        if (n < size)
            m_fail = m_eof = true;
        return *this;
    }

    /// The next byte without consuming it, or END_OF_DATA (and eof()) at the end
    int peek() noexcept
    {
        if (m_fail || m_pos >= m_size) {
            m_eof = (m_pos >= m_size) || m_eof;
            return END_OF_DATA;
        }
        return static_cast<unsigned char>(m_data[m_pos]);
    }

    /// The read position
    size_t tellg() const noexcept { return m_pos; }

    /// Move the read position; beyond the end fails.
    ByteReader& seekg(size_t pos) noexcept
    {
        if (pos > m_size) {
            m_fail = true;
        } else {
            m_pos = pos;
            m_eof = false;
        }
        return *this;
    }

    /// Skip n bytes (a field this version doesn't know); beyond the end fails.
    ByteReader& skip(size_t n) noexcept
    {
        if (n > m_size - m_pos)
            m_fail = true;
        else
            m_pos += n;
        return *this;
    }

    /// All the bytes, and those not read yet
    size_t size() const noexcept { return m_size; }
    size_t remaining() const noexcept { return m_size - m_pos; }
    const char* data() const noexcept { return m_data; }

    bool good() const noexcept { return !m_fail && !m_eof; }
    bool fail() const noexcept { return m_fail; }
    bool eof() const noexcept { return m_eof; }
    explicit operator bool() const noexcept { return !m_fail; }

    /// Mark the data as bad (a serializer rejecting a value)
    void setfail() noexcept { m_fail = true; }

    /// Clear the flags; the position stays.
    void clear() noexcept { m_fail = m_eof = false; }

private:
    const char* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    bool m_fail = false;
    bool m_eof = false;
};

} // namespace dmq

#endif // DMQ_BYTE_READER_H
