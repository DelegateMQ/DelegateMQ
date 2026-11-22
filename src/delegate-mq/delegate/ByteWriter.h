#ifndef DMQ_BYTE_WRITER_H
#define DMQ_BYTE_WRITER_H

/// @file ByteWriter.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief A growable byte buffer that serializers write into: the bytes of one
/// message, before a transport sends them.
///
/// @details Replaces the std::ostream used before DelegateMQ 3.0 in
/// the serialization path, without iostreams' cost: no virtual dispatch, no
/// sentry objects, no locales. Only what a binary serializer needs:
/// - write() at the write position, appending at the end or overwriting after
///   a seekp() (msg_serialize writes a placeholder length, then goes back and
///   fills it in);
/// - tellp() / seekp() for that;
/// - an error flag: setfail() / fail() / good(), sticky until clear() or reset();
/// - data() / size(): the bytes, for a transport to send without a copy.
///
/// Size cap: the buffer never grows past maxsize() (dmq::MAX_MESSAGE_SIZE,
/// DMQ_MAX_MESSAGE_SIZE, unless the constructor asks for less). A write that
/// would go past it writes nothing and sets fail() and overflowed(), before
/// anything is allocated; a remote delegate reports ERR_MESSAGE_TOO_LARGE.
///
/// Storage is a dmq::xstring, so under DMQ_ALLOCATOR it comes from the
/// fixed-block allocator. reset() keeps the capacity for the next message,
/// so once the buffer has grown, sending allocates nothing for it. reserve()
/// at startup does that growth in one allocation, before the system runs.
///
/// The method names follow std::ostream's, so code written for a stream reads
/// the same.
///
/// @note Included by DelegateOpt.h, after the xstring alias it needs: include
/// DelegateOpt.h (or DelegateMQ.h), not this header.

#include <cstddef>
#include <cstring>

namespace dmq {

#ifdef DMQ_ALLOCATOR
// The buffer is an xstring. std::string may grow its capacity to twice what was
// asked, plus one for the terminator, and the fixed-block allocator adds its
// block header and footer: all of that must fit the largest block (16 KB,
// xallocator.cpp), or growing the buffer faults.
static_assert(2 * MAX_MESSAGE_SIZE + 1 + XALLOC_BLOCK_HEADER_SIZE + XALLOC_BLOCK_FOOTER_SIZE <= 16384,
    "DMQ_MAX_MESSAGE_SIZE too large for DMQ_ALLOCATOR's largest block (16 KB): at most 8181");
#endif

class ByteWriter
{
public:
    /// @param[in] maxSize The largest size this buffer may reach; capped at
    /// dmq::MAX_MESSAGE_SIZE.
    explicit ByteWriter(size_t maxSize = MAX_MESSAGE_SIZE) noexcept
        : m_maxSize(maxSize < MAX_MESSAGE_SIZE ? maxSize : MAX_MESSAGE_SIZE) {}

    /// Write size bytes at the write position: append at the end, or overwrite
    /// (and extend past the end, if needed) after a seekp() back.
    ByteWriter& write(const char* data, size_t size)
    {
        if (m_fail)
            return *this;
        if (size == 0)
            return *this;
        if (data == nullptr) {
            m_fail = true;
            return *this;
        }
        // Past the cap: write nothing (and allocate nothing)
        if (size > m_maxSize || m_pos > m_maxSize - size) {
            m_fail = m_overflow = true;
            return *this;
        }
        const size_t end = m_pos + size;
        if (end > m_buf.capacity())
            Grow(end);
        if (m_pos == m_buf.size()) {
            m_buf.append(data, size);
        } else {
            const size_t overlap = (m_buf.size() - m_pos < size) ? m_buf.size() - m_pos : size;
            m_buf.replace(m_pos, overlap, data, size);
        }
        m_pos += size;
        return *this;
    }

    /// The write position
    size_t tellp() const noexcept { return m_pos; }

    /// Move the write position; beyond the end of the data fails.
    ByteWriter& seekp(size_t pos)
    {
        if (pos > m_buf.size())
            m_fail = true;
        else
            m_pos = pos;
        return *this;
    }

    bool good() const noexcept { return !m_fail; }
    bool fail() const noexcept { return m_fail; }
    /// A write would have gone past maxsize() (fail() is set too)
    bool overflowed() const noexcept { return m_overflow; }
    explicit operator bool() const noexcept { return !m_fail; }

    /// Mark the data as bad (a serializer that couldn't write a value)
    void setfail() noexcept { m_fail = true; }

    /// Clear the error flags; the data stays.
    void clear() noexcept { m_fail = m_overflow = false; }

    /// Empty the buffer and clear the error flags, keeping the capacity
    void reset() noexcept
    {
        m_buf.clear();
        m_pos = 0;
        m_fail = m_overflow = false;
    }

    /// Allocate room for n bytes now (at most maxsize()), so the buffer
    /// doesn't grow, and allocate, while messages are sent.
    void reserve(size_t n)
    {
        m_buf.reserve(n < m_maxSize ? n : m_maxSize);
    }

    /// The bytes the buffer holds without growing
    size_t capacity() const noexcept { return m_buf.capacity(); }

    /// The largest size the buffer may reach
    size_t maxsize() const noexcept { return m_maxSize; }

    /// The bytes written, for a transport to send
    const char* data() const noexcept { return m_buf.data(); }
    size_t size() const noexcept { return m_buf.size(); }

private:
    /// Grow geometrically, but never past the cap
    void Grow(size_t needed)
    {
        size_t cap = m_buf.capacity() * 2;
        if (cap < needed)
            cap = needed;
        if (cap > m_maxSize)
            cap = m_maxSize;
        m_buf.reserve(cap);
    }

    xstring m_buf;
    size_t m_maxSize = MAX_MESSAGE_SIZE;
    size_t m_pos = 0;
    bool m_fail = false;
    bool m_overflow = false;
};

} // namespace dmq

#endif // DMQ_BYTE_WRITER_H
