/// @file msg_serialize.h
/// @see https://github.com/endurodave/MessageSerialize
/// @author David Lafreniere
/// @date 2024
///
/// @brief A robust, header-only C++ binary serialization library.
///
/// @details
/// This library provides a framework for marshaling C++ objects, primitives, and STL
/// containers into binary streams (std::ostream) and demarshaling them back (std::istream).
/// It is designed for network communication and data persistence where binary compatibility
/// and protocol evolution are required.
///
/// **CRITICAL REQUIREMENTS:**
/// * **Binary Mode:** Streams MUST be opened in binary mode (std::ios::binary). Text-mode 
///   streams will corrupt binary data (e.g., newline conversion on Windows).
/// * **Seekable Streams:** User-defined objects (inheriting from `serialize::I`) require 
///   seekable streams (e.g., `std::stringstream`, `std::fstream`) to backfill object sizes.
///   Non-seekable streams (like `std::cout` or sockets) will trigger a `NON_SEEKABLE_STREAM` error.
///
/// **Key Features:**
/// * **Single Header:** A self-contained, header-only library with no external dependencies
///   beyond the standard C++ library (STL).
/// * **Endianness Handling:** Wire format is little-endian. Automatic byte-swapping on
///   big-endian hosts ensures compatibility between mixed-endian architectures.
/// * **STL Container Support:** Native serialization for std::vector, std::list, std::map,
///   std::set, std::string, and std::wstring.
/// * **Protocol Versioning:** Supports "Forward/Backward Compatibility" by handling
///   size mismatches. If a received user-defined object is larger than expected (newer version),
///   the parser safely discards the extra data.
/// * **Type Safety:** Uses SFINAE and compile-time static assertions to prevent
///   serialization of unsupported types or unsafe pointers.
/// * **Deep Copying:** Supports serializing containers of pointers, automatically
///   allocating memory upon deserialization.
/// * **Error Handling:** Includes a callback-based error reporting mechanism for
///   stream errors, type mismatches, and boundary violations.
///
/// **Usage:**
/// User-defined classes must inherit from `serialize::I` and implement the `read()`
/// and `write()` methods.

#ifndef MSG_SERIALIZE_H_
#define MSG_SERIALIZE_H_

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include "extras/util/Fault.h"
#include <limits>
#include <type_traits>
#include <typeinfo>
#include <iostream>
#include <memory>
#include <array>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <string>

namespace serialize_traits
{
    template <typename T>
    struct is_shared_ptr : std::false_type {};

    template <typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    template <typename U>
    struct is_unsupported_container : std::false_type {};

    /// @brief Trait to check if a stream type is seekable.
    template <typename T, typename = void>
    struct is_seekable : std::false_type {};

    template <typename T>
    struct is_seekable<T, std::void_t<
        decltype(std::declval<T&>().seekp(std::declval<std::streampos>())),
        decltype(std::declval<T&>().tellp())
    >> : std::true_type {};
}

// Define to enable compile-time checking of unsupported container types
// Undefine for less compile-time include file dependencies
#define CHECK_UNSUPPORTED_CONTAINER
#ifdef CHECK_UNSUPPORTED_CONTAINER

#include <deque>
#include <forward_list>
#include <queue>
#include <stack>
#include <unordered_set>
#include <unordered_map>

    template <typename U>
    struct serialize_traits::is_unsupported_container<std::multiset<U>> : std::true_type {};

    template <typename U, typename V>
    struct serialize_traits::is_unsupported_container<std::pair<U, V>> : std::true_type {};

    template <typename T, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::deque<T, Alloc>> : std::true_type {};

    template <typename T, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::forward_list<T, Alloc>> : std::true_type {};

    template <typename T, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::priority_queue<T, std::vector<T, Alloc>>> : std::true_type {};

    template <typename T, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::queue<T, Alloc>> : std::true_type {};

    template <typename T, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::stack<T, Alloc>> : std::true_type {};

    template <typename T, typename Hash, typename KeyEqual, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::unordered_multiset<T, Hash, KeyEqual, Alloc>> : std::true_type {};

    template <typename Key, typename T, typename Hash, typename KeyEqual, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::unordered_map<Key, T, Hash, KeyEqual, Alloc>> : std::true_type {};

    template <typename Key, typename T, typename Hash, typename KeyEqual, typename Alloc>
    struct serialize_traits::is_unsupported_container<std::unordered_multimap<Key, T, Hash, KeyEqual, Alloc>> : std::true_type {};
#endif  // CHECK_UNSUPPORTED_CONTAINER

/// @brief The serialize class binary serializes and deserializes C++ objects.
/// @details Each class need to implement the serialize::I abstract interface
/// to allow binary serialization to any stream. A default constructor is required
/// for the serialized object. Most container elements can be stored by value or
/// by pointer. C++ containers supported are:
///
///     vector
///     list
///     map
///     set
///     string
///     wstring
///     char[]
///
/// Always check the input stream or output stream to ensure no errors
/// before using data. e.g.
///
/// if (ss.good())
///     // Do something with input or output data
///
/// This serialization class is not thread safe and a serialize instance should
/// only be accessed from a single task.
///
/// The serialize class support receiving objects that have more or less data fields
/// than what is currently being parsed. If more data is received after parsing an
/// object, the extra data is discarded. If less data is received, the parsing of the
/// extra data fields does not occur. This supports the protocol changing by adding new
/// data elements to an object. Once the protocol is released at a particular version,
/// new data elements can be added but existing ones cannot be removed/changed.
///
/// @note write() requires a seekable stream (i.e. std::ostringstream). The size field
///       for user-defined objects is backfilled via seekp() after the object is written.
///       Using a non-seekable stream produces a corrupt size field and triggers an error.
class serialize
{
public:
    /// @brief Abstract interface that all serialized user defined classes inherit.
    class I
    {
    public:
        virtual ~I() = default;

        /// Inheriting class implements the write function. Write each
        /// class member to the ostream. Write in the same order as read().
        /// Each level within the hierarchy must implement. Ensure base
        /// write() implementation is called if necessary.
        /// @param[in] ms - the message serialize instance
        /// @param[in] os - the output stream
        /// @return The output stream
        virtual std::ostream& write(serialize& ms, std::ostream& os) = 0;

        /// Inheriting class implements the read function. Read each
        /// class member from the istream. Read in the same order as write().
        /// Each level within the hierarchy must implement. Ensure base
        /// read() implementation is called if necessary.
        /// @param[in] ms - the message serialize instance
        /// @param[in] is - the input stream
        /// @return The input stream
        virtual std::istream& read(serialize& ms, std::istream& is) = 0;
    };

    enum class Type
    {
        UNKNOWN = 0,
        LITERAL = 1,
        STRING = 8,
        WSTRING = 9,
        VECTOR = 20,
        MAP = 21,
        LIST = 22,
        SET = 23,
        ENDIAN = 30,
        USER_DEFINED = 31,
    };

    enum class ParsingError
    {
        NONE,
        TYPE_MISMATCH,
        STREAM_ERROR,
        NON_SEEKABLE_STREAM,    // Stream does not support tellp/seekp (required for objects)
        STRING_TOO_LONG,
        CONTAINER_TOO_MANY,
        INVALID_INPUT,
        END_OF_FILE
    };

    serialize() = default;
    ~serialize() = default;

    /// Returns true if the host is little-endian.
    /// @return Returns true if little-endian.
    static bool LE()
    {
        const static int n = 1;
        const static bool le = (*reinterpret_cast<const char*>(&n) == 1);
        return le;
    }

    /// Read endian from stream.
    /// @param[in] is - input stream
    /// @param[out] littleEndian - set to true if stream was written on a little-endian host
    /// @return The input stream.
    std::istream& readEndian(std::istream& is, bool& littleEndian)
    {
        if (read_type(is, Type::ENDIAN))
        {
            is.read(reinterpret_cast<char*>(&littleEndian), sizeof(littleEndian));
        }
        return is;
    }

    /// Write current CPU endian to stream.
    /// @param[in] os - output stream
    void writeEndian(std::ostream& os)
    {
        bool littleEndian = LE();
        write_type(os, Type::ENDIAN);
        os.write(reinterpret_cast<const char*>(&littleEndian), sizeof(littleEndian));
    }

    /// Read a user defined object implementing the serialize::I interface from a stream.
    /// Normally the sender and receiver object is the same size. However, if a newer version of
    /// the object is introduced on one side the sizes will differ. If received object is smaller
    /// than sent object, the extra data in the sent object is discarded.
    /// @param[in] is - the input stream
    /// @param[in] t_ - the object to read
    /// @return The input stream
    std::istream& read(std::istream& is, I* t_)
    {
        if (check_stop_parse(is))
            return is;

        if (check_pointer(is, t_))
        {
            if (read_type(is, Type::USER_DEFINED))
            {
                uint16_t size = 0;
                std::streampos startPos = is.tellg();
                if (startPos == std::streampos(-1))
                {
                    raiseError(ParsingError::NON_SEEKABLE_STREAM, __LINE__, __FILE__);
                    is.setstate(std::ios::failbit);
                    return is;
                }

                read(is, size, false);

                std::streampos targetStopPos = startPos + std::streampos(size);

                // Bounds check: ensure size does not exceed stream EOF
                std::streampos dataPos = is.tellg();
                is.seekg(0, std::ios::end);
                std::streampos endPosStream = is.tellg();
                is.seekg(dataPos);

                if (targetStopPos > endPosStream) {
                    raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                    is.setstate(std::ios::failbit);
                    return is;
                }

                // Bounds check: ensure size does not exceed parent object's bounds
                if (stopParsePosIdx > 0 && targetStopPos > stopParsePosStack[stopParsePosIdx - 1]) {
                    raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                    is.setstate(std::ios::failbit);
                    return is;
                }

                parseStatus(typeid(*t_), size);

                // Save the stop parsing position to prevent parsing overrun
                push_stop_parse_pos(targetStopPos);

                t_->read(*this, is);

                pop_stop_parse_pos();

                if (is.good())
                {
                    std::streampos endPos = is.tellg();
                    uint16_t rcvdSize = static_cast<uint16_t>(endPos - startPos);

                    // Did sender send a larger object than what receiver parsed?
                    if (rcvdSize < size)
                    {
                        // Skip over the extra received data
                        uint16_t seekOffset = size - rcvdSize;
                        is.seekg(seekOffset, std::ios_base::cur);
                    }
                }
                return is;
            }
        }
        return is;
    }

    /// Read a std::string from a stream.
    /// @param[in] is - the input stream
    /// @param[in] s - the string to read
    /// @return The input stream
    template <typename Traits, typename Alloc>
    std::istream& read(std::istream& is, std::basic_string<char, Traits, Alloc>& s)
    {
        if (check_stop_parse(is))
            return is;

        if (read_type(is, Type::STRING))
        {
            uint16_t size = 0;
            read(is, size, false);
            s.clear();
            if (check_stream(is) && check_slength(is, size))
            {
                s.resize(size);
                parseStatus(typeid(s), s.size());
                read_internal(is, &s[0], size, true);
            }
        }
        return is;
    }

    /// Read a std::wstring from a stream.
    /// @param[in] is - the input stream
    /// @param[in] s - the string to read
    /// @return The input stream
    template <typename Traits, typename Alloc>
    std::istream& read(std::istream& is, std::basic_string<wchar_t, Traits, Alloc>& s)
    {
        if (check_stop_parse(is))
            return is;

        if (read_type(is, Type::WSTRING))
        {
            uint16_t size = 0;
            read(is, size, false);
            s.clear();
            if (check_stream(is) && check_slength(is, size))
            {
                s.resize(size);
                parseStatus(typeid(s), s.size());
                for (uint16_t ii = 0; ii < size; ii++)
                {
                    wchar_t c = 0;
                    int offset = LE() ? 0 : (sizeof(wchar_t) - WCHAR_SIZE);
                    read_internal(is, reinterpret_cast<char*>(&c) + offset, WCHAR_SIZE);
                    s[ii] = c;
                }
            }
        }
        return is;
    }

    /// Read a character string from a stream.
    /// @param[in] is - the input stream
    /// @param[in] str - the character string to read into
    /// @param[in] maxLen - the capacity of str in bytes (including null terminator)
    /// @return The input stream
    std::istream& read(std::istream& is, char* str, size_t maxLen)
    {
        if (check_stop_parse(is))
            return is;

        if (read_type(is, Type::STRING))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_slength(is, size))
            {
                if (check_pointer(is, str))
                {
                    if (size > maxLen)
                    {
                        raiseError(ParsingError::STRING_TOO_LONG, __LINE__, __FILE__);
                        is.setstate(std::ios::failbit);
                        return is;
                    }
                    parseStatus(typeid(str), size);
                    read_internal(is, str, size, true);
                }
            }
        }
        return is;
    }

    /// Read a vector<bool> container from a stream. The vector<bool> items are
    /// stored differently and therefore need special handling to serialize.
    /// Unlike other specializations of vector, std::vector<bool> does not manage a
    /// dynamic array of bool objects. Instead, it is supposed to pack the boolean
    /// values into a single bit each.
    /// @param[in] is - the input stream
    /// @param[in] container - the vector container to read into
    /// @return The input stream
    std::istream& read(std::istream& is, std::vector<bool>& container)
    {
        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::VECTOR))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    bool t;
                    read(is, t);
                    container.push_back(t);
                }
            }
        }
        return is;
    }

    /// Write a user defined object implementing the serialize::I interface to a stream.
    /// @note The stream must support seekp(). The object size is backfilled after writing.
    /// @param[in] os - the output stream
    /// @param[in] t_ - the object to write
    /// @return The output stream
    std::ostream& write(std::ostream& os, I* t_)
    {
        if (check_pointer(os, t_))
        {
            uint16_t elementSize = 0;

            write_type(os, Type::USER_DEFINED);
            std::streampos elementSizePos = os.tellp();
            if (elementSizePos == std::streampos(-1))
            {
                raiseError(ParsingError::NON_SEEKABLE_STREAM, __LINE__, __FILE__);
                os.setstate(std::ios::failbit);
                return os;
            }
            write(os, elementSize, false);

            // Write user defined object
            t_->write(*this, os);

            if (os.good())
            {
                // Backfill the object size into the stream (requires seekable stream)
                std::streampos currentPos = os.tellp();
                os.seekp(elementSizePos);
                auto sizeDiff = currentPos - elementSizePos;
                if (sizeDiff > UINT16_MAX) {
                    raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                    os.setstate(std::ios::failbit);
                    return os;
                }
                elementSize = static_cast<uint16_t>(sizeDiff);
                write(os, elementSize, false);
                os.seekp(currentPos);
            }
            return os;
        }
        return os;
    }

    /// Write a const std::string to a stream.
    /// @param[in] os - the output stream
    /// @param[in] s - the string to write
    /// @return The output stream
    template <typename Traits, typename Alloc>
    std::ostream& write(std::ostream& os, const std::basic_string<char, Traits, Alloc>& s)
    {
        if (s.size() > MAX_STRING_SIZE) {
            raiseError(ParsingError::STRING_TOO_LONG, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(s.size());
        write_type(os, Type::STRING);
        write(os, size, false);
        if (check_stream(os) && check_slength(os, size))
        {
            write_internal(os, s.c_str(), size, true);
        }
        return os;
    }

    /// Write a std::string to a stream.
    /// @param[in] os - the output stream
    /// @param[in] s - the string to write
    /// @return The output stream
    template <typename Traits, typename Alloc>
    std::ostream& write(std::ostream& os, std::basic_string<char, Traits, Alloc>& s)
    {
        return write(os, static_cast<const std::basic_string<char, Traits, Alloc>&>(s));
    }

    /// Write a const std::wstring to a stream.
    /// @param[in] os - the output stream
    /// @param[in] s - the string to write
    /// @return The output stream
    template <typename Traits, typename Alloc>
    std::ostream& write(std::ostream& os, const std::basic_string<wchar_t, Traits, Alloc>& s)
    {
        if (s.size() > MAX_STRING_SIZE) {
            raiseError(ParsingError::STRING_TOO_LONG, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(s.size());
        write_type(os, Type::WSTRING);
        write(os, size, false);
        if (check_stream(os) && check_slength(os, size))
        {
            for (uint16_t ii = 0; ii < size; ii++)
            {
                wchar_t c = s[ii];
                int offset = LE() ? 0 : (sizeof(wchar_t) - WCHAR_SIZE);
                write_internal(os, reinterpret_cast<char*>(&c) + offset, WCHAR_SIZE);
            }
        }
        return os;
    }

    /// Write a std::wstring to a stream.
    /// @param[in] os - the output stream
    /// @param[in] s - the string to write
    /// @return The output stream
    template <typename Traits, typename Alloc>
    std::ostream& write(std::ostream& os, std::basic_string<wchar_t, Traits, Alloc>& s)
    {
        return write(os, static_cast<const std::basic_string<wchar_t, Traits, Alloc>&>(s));
    }

    /// Write a character string to a stream.
    /// @param[in] os - the output stream
    /// @param[in] str - the character string to write.
    /// @return The output stream
    std::ostream& write(std::ostream& os, char* str)
    {
        return write(os, static_cast<const char*>(str));
    }

    /// Write a const character string to a stream.
    /// @param[in] os - the output stream
    /// @param[in] str - the character string to write.
    /// @return The output stream
    std::ostream& write(std::ostream& os, const char* str)
    {
        if (check_pointer(os, str))
        {
            uint16_t size = static_cast<uint16_t>(strlen(str)) + 1;
            write_type(os, Type::STRING);
            write(os, size, false);
            if (check_stream(os) && check_slength(os, size))
            {
                write_internal(os, str, size, true);
            }
        }
        return os;
    }

    /// Write a vector<bool> container to a stream. The vector<bool> items are
    /// stored differently and therefore need special handling to serialize.
    /// Unlike other specializations of vector, std::vector<bool> does not manage a
    /// dynamic array of bool objects. Instead, it is supposed to pack the boolean
    /// values into a single bit each.
    /// @param[in] os - the output stream
    /// @param[in] container - the vector container to write
    /// @return The output stream
    std::ostream& write(std::ostream& os, std::vector<bool>& container)
    {
        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::VECTOR);
        write(os, size, false);
        if (check_stream(os) && check_container_size(os, size))
        {
            for (const bool& c : container)
            {
                write(os, c);
            }
        }
        return os;
    }

    /// Read an object from a stream.
    /// @param[in] is - the input stream
    /// @param[in] t_ - the object to read into
    /// @return The input stream
    template<typename T>
    std::istream& read(std::istream& is, T& t_, bool readPrependedType = true)
    {
        static_assert(!serialize_traits::is_unsupported_container<T>::value, "Unsupported C++ container type");

        static_assert(!(std::is_pointer<T>::value &&
            (std::is_arithmetic<typename std::remove_pointer<T>::type>::value ||
                std::is_class<typename std::remove_pointer<T>::type>::value)),
            "T cannot be a pointer to a built-in or custom data type. Use values instead.");

        if (check_stop_parse(is))
            return is;

        // If C++17 or higher
#if defined(_MSVC_LANG) && _MSVC_LANG >= 201703L || __cplusplus >= 201703L
        // Check if T is a user-defined class
        if constexpr (std::is_class<T>::value)
        {
            // C-style cast required: T may be const-qualified (e.g. const AlarmMsg)
            // when called from a const context, and neither static_cast nor
            // reinterpret_cast can strip const. A C-style cast combines const_cast
            // with the pointer conversion in one step. The static_assert above
            // guarantees T derives from I whenever this branch is executed.
            read(is, (serialize::I*)(&t_));
            return is;
        }
        else
        {
            // If T is not a class, handle built-in data types
            if constexpr (std::is_pointer<T>::value == false)
            {
                if (readPrependedType)
                {
                    if (!read_type(is, Type::LITERAL))
                    {
                        return is;
                    }
                }

                if (readPrependedType)
                    parseStatus(typeid(t_), sizeof(t_));
                read_internal(is, reinterpret_cast<char*>(&t_), sizeof(t_));
                return is;
            }
            else
            {
                // Can't read pointers to built-in types
                raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                is.setstate(std::ios::failbit);
                return is;
            }
        }
#else
        // Is T a built-in data type (e.g. float, int, ...)?
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 6326)
#endif
        if (std::is_class<T>::value == false)
        {
            // Is T is not a pointer type
            if (std::is_pointer<T>::value == false)
            {
                if (readPrependedType)
                {
                    if (!read_type(is, Type::LITERAL))
                    {
                        return is;
                    }
                }

                if (readPrependedType)
                    parseStatus(typeid(t_), sizeof(t_));
                read_internal(is, reinterpret_cast<char*>(&t_), sizeof(t_));
                return is;
            }
            else
            {
                // Can't read pointers to built-in type
                raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                is.setstate(std::ios::failbit);
                return is;
            }
        }
        // Else T is a user defined data type (e.g. MyData)
        else
        {
            // C-style cast required: without if constexpr (C++14) this branch is
            // compiled for all T including primitives, so static_cast is rejected
            // for non-class types. The branch is only reached when is_class<T> is
            // true, making the cast safe at runtime.
            read(is, (serialize::I*)(&t_));
            return is;
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif
    }

    /// Write an object to a stream.
    /// @param[in] os - the output stream
    /// @param[in] t_ - the object to write
    /// @return The output stream
    template<typename T>
    std::ostream& write(std::ostream& os, T& t_, bool prependType = true)
    {
        static_assert(!serialize_traits::is_unsupported_container<T>::value, "Unsupported C++ container type");

        static_assert(!(std::is_pointer<T>::value &&
            (std::is_arithmetic<typename std::remove_pointer<T>::type>::value ||
                std::is_class<typename std::remove_pointer<T>::type>::value)),
            "T cannot be a pointer to a built-in or custom data type. Use values instead.");

        // When T is a class it must inherit from serialize::I.
        // The combined form (!is_class || is_base_of) is valid for all T — non-class
        // types satisfy the first clause so the assertion always passes for them.
        static_assert(!std::is_class<T>::value || std::is_base_of<serialize::I, T>::value,
            "T must inherit from serialize::I");

        // If C++17 or higher
#if defined(_MSVC_LANG) && _MSVC_LANG >= 201703L || __cplusplus >= 201703L
        // Check if T is a user-defined class
        if constexpr (std::is_class<T>::value)
        {
            // C-style cast required: T may be const-qualified (e.g. const AlarmMsg)
            // when called from a const context, and neither static_cast nor
            // reinterpret_cast can strip const. A C-style cast combines const_cast
            // with the pointer conversion in one step. The static_assert above
            // guarantees T derives from I whenever this branch is executed.
            return write(os, (serialize::I*)(&t_));
        }
        else
        {
            // If T is not a class, handle built-in data types
            if constexpr (std::is_pointer<T>::value == false)
            {
                if (prependType)
                {
                    write_type(os, Type::LITERAL);
                }
                return write_internal(os, reinterpret_cast<const char*>(&t_), sizeof(t_));
            }
            else
            {
                // Can't write pointers to built-in types
                raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                os.setstate(std::ios::failbit);
                return os;
            }
        }
#else
        // Is T type a built-in data type (e.g. float, int, ...)?
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 6326)
#endif
        if (std::is_class<T>::value == false)
        {
            // Is T is not a pointer type
            if (std::is_pointer<T>::value == false)
            {
                if (prependType)
                {
                    write_type(os, Type::LITERAL);
                }
                return write_internal(os, reinterpret_cast<const char*>(&t_), sizeof(t_));
            }
            else
            {
                // Can't write pointers to built-in type
                raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                os.setstate(std::ios::failbit);
                return os;
            }
        }
        // Else T type is a user defined data type (e.g. MyData)
        else
        {
            // C-style cast required: without if constexpr (C++14) this branch is
            // compiled for all T including primitives, so static_cast and static_assert
            // are rejected for non-class types. The branch is only reached when
            // is_class<T> is true, making the cast safe at runtime.
            return write(os, (serialize::I*)(&t_));
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif
    }

    /// Write a vector container to a stream. The items in vector are stored
    /// by value.
    /// @param[in] os - the output stream
    /// @param[in] container - the vector container to write
    /// @return The output stream
    template <class T, class Alloc>
    std::ostream& write(std::ostream& os, std::vector<T, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::VECTOR);
        write(os, size, false);
        if (check_stream(os) && check_container_size(os, size))
        {
            for (const auto& item : container)
            {
                write(os, item, false);
            }
        }
        return os;
    }

    /// Read into a vector container from a stream. Items in vector are stored
    /// by value.
    /// @param[in] is - the input stream
    /// @param[in] container - the vector container to read into
    /// @return The input stream
    template <class T, class Alloc>
    std::istream& read(std::istream& is, std::vector<T, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::VECTOR))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    T t;
                    read(is, t, false);
                    container.push_back(t);
                }
            }
        }
        return is;
    }

    /// Write a vector container to a stream. The items in vector are stored
    /// by pointer.
    /// @param[in] os - the output stream
    /// @param[in] container - the vector container to write
    /// @return The output stream
    template <class T, class Alloc>
    std::ostream& write(std::ostream& os, std::vector<T*, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::VECTOR);
        write(os, size, false);

        if (check_stream(os) && check_container_size(os, size))
        {
            for (auto* ptr : container)
            {
                if (ptr != nullptr)
                {
                    bool notNULL = true;
                    write(os, notNULL, false);

                    auto* i = static_cast<I*>(ptr);
                    write(os, i);
                }
                else
                {
                    bool notNULL = false;
                    write(os, notNULL, false);
                }
            }

        }
        return os;
    }

    /// Read into a vector container from a stream. Items in vector stored
    /// by pointer. Operator new called to create object instances.
    /// @param[in] is - the input stream
    /// @param[in] container - the vector container to read into
    /// @return The input stream
    template <class T, class Alloc>
    std::istream& read(std::istream& is, std::vector<T*, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::VECTOR))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    bool notNULL = false;
                    read(is, notNULL, false);

                    if (notNULL)
                    {
                        T* object = new T;
                        auto* iface = static_cast<I*>(object);
                        read(is, iface);
                        container.push_back(object);
                    }
                    else
                    {
                        container.push_back(nullptr);
                    }
                }
            }
        }
        return is;
    }

    /// Write a map container to a stream. The items in map are stored
    /// by value.
    /// @param[in] os - the output stream
    /// @param[in] container - the map container to write
    /// @return The output stream
    template <class K, class V, class P, class Alloc>
    std::ostream& write(std::ostream& os, std::map<K, V, P, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<V>::value, "Type V must not be a shared_ptr type");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::MAP);
        write(os, size, false);
        if (check_stream(os) && check_container_size(os, size))
        {
            for (const auto& entry : container)
            {
                write(os, entry.first, false);
                write(os, entry.second, false);
            }
        }
        return os;
    }

    /// Read into a map container from a stream. Items in map are stored
    /// by value.
    /// @param[in] is - the input stream
    /// @param[in] container - the map container to read into
    /// @return The input stream
    template <class K, class V, class P, class Alloc>
    std::istream& read(std::istream& is, std::map<K, V, P, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<V>::value, "Type V must not be a shared_ptr type");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::MAP))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    K key;
                    V value;
                    read(is, key, false);
                    read(is, value, false);
                    container[key] = value;
                }
            }
        }
        return is;
    }

    /// Write a map container to a stream. The items in map are stored
    /// by pointer.
    /// @param[in] os - the output stream
    /// @param[in] container - the map container to write
    /// @return The output stream
    template <class K, class V, class P, class Alloc>
    std::ostream& write(std::ostream& os, std::map<K, V*, P, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, V>::value, "Type V must be derived from serialize::I");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::MAP);
        write(os, size, false);

        if (check_stream(os) && check_container_size(os, size))
        {
            for (auto& entry : container)
            {
                write(os, entry.first, false);

                if (entry.second != nullptr)
                {
                    bool notNULL = true;
                    write(os, notNULL, false);

                    auto* i = static_cast<I*>(entry.second);
                    write(os, i);
                }
                else
                {
                    bool notNULL = false;
                    write(os, notNULL, false);
                }
            }
        }
        return os;
    }

    /// Read into a map container from a stream. Items in map stored
    /// by pointer. Operator new called to create object instances.
    /// @param[in] is - the input stream
    /// @param[in] container - the map container to read into
    /// @return The input stream
    template <class K, class V, class P, class Alloc>
    std::istream& read(std::istream& is, std::map<K, V*, P, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, V>::value, "Type V must be derived from serialize::I");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::MAP))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    K key;
                    read(is, key, false);
                    bool notNULL;
                    read(is, notNULL, false);
                    if (notNULL)
                    {
                        V* object = new V;
                        auto* iface = static_cast<I*>(object);
                        read(is, iface);
                        container[key] = static_cast<V*>(object);
                    }
                    else
                    {
                        container[key] = nullptr;
                    }
                }
            }
        }
        return is;
    }

    /// Write a set container to a stream. The items in set are stored
    /// by value.
    /// @param[in] os - the output stream
    /// @param[in] container - the set container to write
    /// @return The output stream
    template <class T, class P, class Alloc>
    std::ostream& write(std::ostream& os, std::set<T, P, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::SET);
        write(os, size, false);

        if (check_stream(os) && check_container_size(os, size))
        {
            for (const auto& item : container)
            {
                write(os, item, false);
            }
        }
        return os;
    }

    /// Read into a set container from a stream. Items in set are stored
    /// by value.
    /// @param[in] is - the input stream
    /// @param[in] container - the set container to read into
    /// @return The input stream
    template <class T, class P, class Alloc>
    std::istream& read(std::istream& is, std::set<T, P, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::SET))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    T t;
                    read(is, t, false);
                    container.insert(t);
                }
            }
        }
        return is;
    }

    /// Write a set container to a stream. The items in set are stored
    /// by pointer.
    /// @param[in] os - the output stream
    /// @param[in] container - the set container to write
    /// @return The output stream
    template <class T, class P, class Alloc>
    std::ostream& write(std::ostream& os, std::set<T*, P, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::SET);
        write(os, size, false);
        if (check_stream(os) && check_container_size(os, size))
        {
            for (auto ptr : container)
            {
                if (ptr != nullptr)
                {
                    bool notNULL = true;
                    write(os, notNULL, false);

                    auto* i = static_cast<I*>(ptr);
                    write(os, i);
                }
                else
                {
                    bool notNULL = false;
                    write(os, notNULL, false);
                }
            }
        }
        return os;
    }

    /// Read into a set container from a stream. Items in set stored
    /// by pointer. Operator new called to create object instances.
    /// @param[in] is - the input stream
    /// @param[in] container - the set container to read into
    /// @return The input stream
    template <class T, class P, class Alloc>
    std::istream& read(std::istream& is, std::set<T*, P, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::SET))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    bool notNULL = false;
                    read(is, notNULL, false);
                    if (notNULL)
                    {
                        T* object = new T;
                        auto* iface = static_cast<I*>(object);
                        read(is, iface);
                        container.insert(object);
                    }
                    else
                    {
                        container.insert(nullptr);
                    }
                }
            }
        }
        return is;
    }

    /// Write a list container to a stream. The items in list are stored
    /// by value.
    /// @param[in] os - the output stream
    /// @param[in] container - the list container to write
    /// @return The output stream
    template <class T, class Alloc>
    std::ostream& write(std::ostream& os, std::list<T, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::LIST);
        write(os, size, false);

        if (check_stream(os) && check_container_size(os, size))
        {
            for (const auto& item : container)
            {
                write(os, item, false);
            }
        }
        return os;
    }

    /// Read into a list container from a stream. Items in list are stored
    /// by value.
    /// @param[in] is - the input stream
    /// @param[in] container - the list container to read into
    /// @return The input stream
    template <class T, class Alloc>
    std::istream& read(std::istream& is, std::list<T, Alloc>& container)
    {
        static_assert(!serialize_traits::is_shared_ptr<T>::value, "Type T must not be a shared_ptr type");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::LIST))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    T t;
                    read(is, t, false);
                    container.push_back(t);
                }
            }
        }
        return is;
    }

    /// Write a list container to a stream. The items in list are stored
    /// by pointer.
    /// @param[in] os - the output stream
    /// @param[in] container - the list container to write
    /// @return The output stream
    template <class T, class Alloc>
    std::ostream& write(std::ostream& os, std::list<T*, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (container.size() > MAX_CONTAINER_SIZE) {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            os.setstate(std::ios::failbit);
            return os;
        }
        uint16_t size = static_cast<uint16_t>(container.size());
        write_type(os, Type::LIST);
        write(os, size, false);

        if (check_stream(os) && check_container_size(os, size))
        {
            for (auto* ptr : container)
            {
                if (ptr != nullptr)
                {
                    bool notNULL = true;
                    write(os, notNULL, false);

                    auto* i = static_cast<I*>(ptr);
                    write(os, i);
                }
                else
                {
                    bool notNULL = false;
                    write(os, notNULL, false);
                }
            }
        }
        return os;
    }

    /// Read into a list container from a stream. Items in list stored
    /// by pointer. Operator new called to create object instances.
    /// @param[in] is - the input stream
    /// @param[in] container - the list container to read into
    /// @return The input stream
    template <class T, class Alloc>
    std::istream& read(std::istream& is, std::list<T*, Alloc>& container)
    {
        static_assert(std::is_base_of<serialize::I, T>::value, "Type T must be derived from serialize::I");

        if (check_stop_parse(is))
            return is;

        container.clear();
        if (read_type(is, Type::LIST))
        {
            uint16_t size = 0;
            read(is, size, false);
            if (check_stream(is) && check_container_size(is, size))
            {
                parseStatus(typeid(container), size);
                for (uint16_t i = 0; i < size; ++i)
                {
                    bool notNULL = false;
                    read(is, notNULL, false);
                    if (notNULL)
                    {
                        T* object = new T;
                        auto* iface = static_cast<I*>(object);
                        read(is, iface);
                        container.push_back(object);
                    }
                    else
                    {
                        container.push_back(nullptr);
                    }
                }
            }
        }
        return is;
    }

    typedef void (*ErrorHandler)(ParsingError error, int line, const char* file);
    void setErrorHandler(ErrorHandler error_handler_)
    {
        error_handler = error_handler_;
    }

    ParsingError getLastError() const { return lastError; }
    void clearLastError() { lastError = ParsingError::NONE; }

    typedef void (*ParseHandler)(const std::type_info& typeId, size_t size);
    void setParseHandler(ParseHandler parse_handler_)
    {
        parse_handler = parse_handler_;
    }

private:
    /// Read from stream into caller's character buffer.
    /// Wire format is little-endian. On a big-endian host the bytes are reversed
    /// after the read so that multi-byte values are correctly stored in memory.
    /// @param[in] is - input stream
    /// @param[out] p - buffer to receive the bytes
    /// @param[in] size - number of bytes to read
    /// @param[in] no_swap - true suppresses byte-order reversal (used for char arrays)
    /// @return The input stream.
    std::istream& read_internal(std::istream& is, char* p, uint32_t size, bool no_swap = false)
    {
        if (check_stop_parse(is))
            return is;

        if (!check_pointer(is, p))
            return is;

        // Single read for all bytes, then reverse in-place when host is big-endian
        is.read(p, size);
        if (!LE() && !no_swap && size > 1)
            std::reverse(p, p + size);

        return is;
    }

    /// Write bytes from caller's buffer to stream.
    /// Wire format is little-endian. On a big-endian host the bytes are reversed
    /// before writing so that multi-byte values appear in little-endian order on the wire.
    /// @param[in] os - output stream
    /// @param[in] p - bytes to write
    /// @param[in] size - number of bytes to write
    /// @param[in] no_swap - true suppresses byte-order reversal (used for char arrays)
    /// @return The output stream.
    std::ostream& write_internal(std::ostream& os, const char* p, uint32_t size, bool no_swap = false)
    {
        if (!check_pointer(os, p))
            return os;

        if (!LE() && !no_swap && size > 1)
        {
            if (size > 8)
            {
                raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
                os.setstate(std::ios::failbit);
                return os;
            }

            // Reverse bytes into a small stack buffer then write in a single call.
            // Primitives are at most 8 bytes (double / int64_t).
            char tmp[8];
            for (uint32_t i = 0; i < size; ++i)
                tmp[i] = p[size - 1 - i];
            os.write(tmp, size);
        }
        else
        {
            os.write(p, size);
        }
        return os;
    }

    // Maximum sizes allowed by parser
    static const uint16_t MAX_STRING_SIZE = 1024;
    static const uint16_t MAX_CONTAINER_SIZE = 64;

    // Keep wchar_t serialize size consistent on any platform
    static const size_t WCHAR_SIZE = 2;

    // LIFO stack used to track stop-parse positions for versioned object reads.
    static const size_t MAX_PARSE_STACK_DEPTH = 5;
    std::array<std::streampos, MAX_PARSE_STACK_DEPTH> stopParsePosStack;
    size_t stopParsePosIdx = 0;

    ErrorHandler error_handler = nullptr;
    ParsingError lastError = ParsingError::NONE;
    void raiseError(ParsingError error, int line, const char* file)
    {
        lastError = error;
        if (error_handler)
            error_handler(error, line, file);
    }

    ParseHandler parse_handler = nullptr;
    void parseStatus(const std::type_info& typeId, size_t size = 0) const
    {
        if (parse_handler)
            parse_handler(typeId, size);
    }

    void write_type(std::ostream& os, Type type_)
    {
        uint8_t type = static_cast<uint8_t>(type_);
        write_internal(os, reinterpret_cast<const char*>(&type), sizeof(type));
    }

    bool read_type(std::istream& is, Type type_)
    {
        // Check for end-of-stream before attempting the cast
        auto peeked = is.peek();
        if (peeked == std::istream::traits_type::eof())
        {
            raiseError(ParsingError::END_OF_FILE, __LINE__, __FILE__);
            is.setstate(std::ios::failbit);
            return false;
        }
        Type type = static_cast<Type>(peeked);
        if (type == type_)
        {
            uint8_t typeByte = 0;
            read_internal(is, reinterpret_cast<char*>(&typeByte), sizeof(typeByte));
            return true;
        }
        else
        {
            raiseError(ParsingError::TYPE_MISMATCH, __LINE__, __FILE__);
            is.setstate(std::ios::failbit);
            return false;
        }
    }

    bool check_stream(std::ios& stream)
    {
        if (!stream.good())
        {
            raiseError(ParsingError::STREAM_ERROR, __LINE__, __FILE__);
            stream.setstate(std::ios::failbit);
        }
        return stream.good();
    }

    bool check_slength(std::ios& stream, int stringSize)
    {
        bool sizeOk = stringSize <= MAX_STRING_SIZE;
        if (!sizeOk)
        {
            raiseError(ParsingError::STRING_TOO_LONG, __LINE__, __FILE__);
            stream.setstate(std::ios::failbit);
        }
        if (stringSize == 0)
            return false;
        return sizeOk;
    }

    bool check_container_size(std::ios& stream, int containerSize)
    {
        bool sizeOk = containerSize <= MAX_CONTAINER_SIZE;
        if (!sizeOk)
        {
            raiseError(ParsingError::CONTAINER_TOO_MANY, __LINE__, __FILE__);
            stream.setstate(std::ios::failbit);
        }
        return sizeOk;
    }

    bool check_pointer(std::ios& stream, const void* ptr)
    {
        if (!ptr)
        {
            raiseError(ParsingError::INVALID_INPUT, __LINE__, __FILE__);
            stream.setstate(std::ios::failbit);
        }
        return ptr != nullptr;
    }

    void push_stop_parse_pos(std::streampos stopParsePos)
    {
        ASSERT_TRUE(stopParsePosIdx < MAX_PARSE_STACK_DEPTH);
        stopParsePosStack[stopParsePosIdx++] = stopParsePos;
    }

    std::streampos pop_stop_parse_pos()
    {
        ASSERT_TRUE(stopParsePosIdx > 0);
        return stopParsePosStack[--stopParsePosIdx];
    }

    bool check_stop_parse(std::istream& is)
    {
        if (is.eof())
        {
            raiseError(ParsingError::END_OF_FILE, __LINE__, __FILE__);
            return true;
        }
        if (stopParsePosIdx > 0)
        {
            std::streampos stopParsePos = stopParsePosStack[stopParsePosIdx - 1];
            if (is.tellg() >= stopParsePos)
            {
                return true;
            }
        }
        return false;
    }
};

#endif // MSG_SERIALIZE_H_