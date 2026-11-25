#ifndef DMQ_RAW_SERIALIZER_H
#define DMQ_RAW_SERIALIZER_H

/// @file RawSerializer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Copies each argument's bytes instead of encoding them: one copy per
/// argument, no type tags, no endianness marker, no size prefixes. For peers
/// that share an ABI: two cores of one chip over shared memory, two processes
/// of one build on one machine, two identical boards.
///
/// @details Usage, like any serializer (per remote delegate, or per DataBus
/// topic with DataBus::RegisterSerializer()):
/// @code
/// struct AdcBlock { uint32_t seq; std::array<int16_t, 512> samples; };
/// dmq::serialization::raw::Serializer<void(const AdcBlock&)> rawSer;
/// @endcode
///
/// Always available, whichever DMQ_SERIALIZE_* is selected: it needs nothing
/// but ISerializer.h, so an app can use it for its high-rate messages and
/// another serializer for the rest.
///
/// Arguments:
/// - Every argument (for a pointer argument, the object it points to) must be
///   trivially copyable: plain structs of fixed-width fields, enums,
///   std::array of those, nested structs of those. std::string, xstring,
///   containers, smart pointers and classes with virtual functions don't
///   compile (static_assert).
/// - Values, references (`const T&`) and pointers (`const T*`) are supported;
///   for a pointer the one object it points to is copied (not an array behind
///   it: send a std::array instead), and a null pointer fails the write.
///   Pointer-to-pointer, `void*` and text pointers (`const char*`, which would
///   copy one character) don't compile.
///
/// @note Same ABI, not just the same endianness. Both sides must lay the
/// types out identically: the same padding, alignment and type sizes. Use
/// fixed-width types (`int32_t`, not `long`, which is 4 bytes on Windows and
/// ARM but 8 on 64-bit Linux), give enums a fixed underlying type, and build
/// both sides from the same headers. A 32-bit and a 64-bit process, or MSVC
/// and MinGW, can disagree. There is no versioning: a field added on one side
/// only is not tolerated.
/// @note A struct with a pointer member is trivially copyable, so it compiles,
/// but the pointer means nothing on the other side (unless it points into
/// memory shared at the same address). C++17 can't detect it; don't send one.
///
/// Errors: Read() fails (`is.setfail()`) unless exactly the expected number of
/// bytes remains, which catches most layout mismatches (a struct changed on
/// one side only), though not two same-size fields swapped. Write() fails on a
/// null pointer argument or when the buffer overflows (DMQ_MAX_MESSAGE_SIZE).
/// DelegateRemote reports either as ERR_SERIALIZE / ERR_DESERIALIZE.

#include "delegate/ISerializer.h"
#include <cstddef>
#include <type_traits>

namespace dmq::serialization::raw {

namespace detail {

/// The argument type without reference and cv: T for T, const T&, T&
template <class A>
using Stored = std::remove_cv_t<std::remove_reference_t<A>>;

/// True for a pointer argument (const T*): the pointed-to object is copied
template <class A>
inline constexpr bool IsPointerArg = std::is_pointer_v<Stored<A>>;

/// The type whose bytes are copied: the argument, or what it points to
template <class A>
using Payload = std::conditional_t<IsPointerArg<A>,
    std::remove_cv_t<std::remove_pointer_t<Stored<A>>>, Stored<A>>;

/// char-like types: a pointer to one is text, of unknown length
template <class T>
inline constexpr bool IsCharLike =
    std::is_same_v<T, char> || std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char> ||
    std::is_same_v<T, wchar_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

template <class A>
inline constexpr bool IsCopyable =
    !std::is_void_v<Payload<A>> &&                          // void*: no size to copy
    !std::is_pointer_v<Payload<A>> &&                       // T**: the inner pointer means nothing remotely
    !(IsPointerArg<A> && IsCharLike<Payload<A>>) &&         // const char*: would copy one character
    std::is_trivially_copyable_v<Payload<A>>;

template <class A>
void WriteOne(dmq::ByteWriter& os, const A& arg)
{
    if constexpr (IsPointerArg<A>) {
        if (arg == nullptr) {
            os.setfail();
            return;
        }
        os.write(reinterpret_cast<const char*>(arg), sizeof(Payload<A>));
    } else {
        os.write(reinterpret_cast<const char*>(&arg), sizeof(Payload<A>));
    }
}

/// arg refers to DelegateRemote's own non-const storage (or points to it),
/// even when the signature says const, as ISerializer::Read() documents.
template <class A>
void ReadOne(dmq::ByteReader& is, A& arg)
{
    void* dest = nullptr;
    if constexpr (IsPointerArg<A>) {
        if (arg == nullptr) {
            is.setfail();
            return;
        }
        dest = const_cast<void*>(static_cast<const void*>(arg));
    } else {
        dest = const_cast<void*>(static_cast<const void*>(&arg));
    }
    is.read(static_cast<char*>(dest), sizeof(Payload<A>));
}

} // namespace detail

template <class R>
class Serializer; // Not defined

/// @brief Raw-copy serializer for a remote delegate or DataBus signature.
template <class RetType, class... Args>
class Serializer<RetType(Args...)> : public dmq::ISerializer<RetType(Args...)>
{
    static_assert((detail::IsCopyable<Args> && ...),
        "raw::Serializer copies bytes: every argument (or, for a pointer argument, "
        "the object it points to) must be trivially copyable, and not a void*, T** or "
        "const char*. Use another serializer for strings, containers and classes with "
        "virtual functions.");

public:
    /// Bytes one call occupies: the sum of the arguments' sizes
    static constexpr size_t MESSAGE_SIZE = (size_t{0} + ... + sizeof(detail::Payload<Args>));

    /// Write the arguments into the buffer, replacing what it held
    dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override
    {
        os.reset();
        (detail::WriteOne<Args>(os, args), ...);
        return os;
    }

    /// Read the arguments from the received bytes. Fails unless exactly
    /// MESSAGE_SIZE bytes remain (a sender with a different layout).
    dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override
    {
        if (is.remaining() != MESSAGE_SIZE) {
            is.setfail();
            return is;
        }
        (detail::ReadOne<Args>(is, args), ...);
        return is;
    }
};

} // namespace dmq::serialization::raw

#endif // DMQ_RAW_SERIALIZER_H
