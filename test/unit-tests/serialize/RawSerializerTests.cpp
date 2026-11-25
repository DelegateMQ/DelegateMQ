/// @file RawSerializerTests.cpp
/// @brief Unit tests for port/serialize/raw/RawSerializer.h
///
/// Covers: value, reference and pointer arguments; structs, enums, nested
/// structs and std::array; the size check on read; a null pointer argument;
/// buffer overflow; and a remote delegate round trip through it.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "UnitTestCommon.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

using namespace dmq;
namespace raw = dmq::serialization::raw;

namespace {

enum class Mode : uint8_t { IDLE = 1, RUN = 2 };

struct Point {
    int32_t x = 0;
    int32_t y = 0;
};

struct Telemetry {                    // nested struct, enum, padding
    uint32_t seq = 0;
    Mode mode = Mode::IDLE;
    Point pos;
    float speed = 0.0f;
};

struct AdcBlock {
    uint32_t seq = 0;
    std::array<int16_t, 512> samples{};
};

bool operator==(const Point& a, const Point& b) { return a.x == b.x && a.y == b.y; }

// What the serializer's static_assert accepts and rejects
static_assert(raw::detail::IsCopyable<int32_t>);
static_assert(raw::detail::IsCopyable<const Telemetry&>);
static_assert(raw::detail::IsCopyable<const Point*>);
static_assert(raw::detail::IsCopyable<std::array<int16_t, 8>>);
static_assert(!raw::detail::IsCopyable<std::string>);
static_assert(!raw::detail::IsCopyable<const std::string&>);
static_assert(!raw::detail::IsCopyable<const char*>);
static_assert(!raw::detail::IsCopyable<const void*>);
static_assert(!raw::detail::IsCopyable<const Point**>);

static_assert(raw::Serializer<void(int32_t, uint8_t)>::MESSAGE_SIZE == 5);
static_assert(raw::Serializer<void(const Point*)>::MESSAGE_SIZE == sizeof(Point));
static_assert(raw::Serializer<void()>::MESSAGE_SIZE == 0);

/// Saves what a remote delegate sends, as a transport would
class LoopbackDispatcher : public IDispatcher {
public:
    int Dispatch(ByteWriter& os, DelegateRemoteId, uint16_t* = nullptr) override {
        m_data.assign(os.data(), os.size());
        m_reader.assign(m_data.data(), m_data.size());
        os.reset();
        return 0;
    }
    ByteReader& Received() { return m_reader; }
private:
    std::string m_data;
    ByteReader m_reader;
};

AdcBlock g_received;
int g_calls = 0;
void OnAdcBlock(const AdcBlock& block) { g_received = block; ++g_calls; }

} // namespace

static void ValueTests()
{
    raw::Serializer<void(int32_t, Mode, Telemetry)> ser;
    Telemetry t;
    t.seq = 7;
    t.mode = Mode::RUN;
    t.pos = { -3, 40 };
    t.speed = 1.5f;

    ByteWriter os;
    os.write("stale", 5);                       // Write() replaces what the buffer held
    ser.Write(os, -123456, Mode::RUN, t);
    DMQ_ASSERT_TRUE(os.good());
    DMQ_ASSERT_TRUE(os.size() == sizeof(int32_t) + sizeof(Mode) + sizeof(Telemetry));

    int32_t i = 0;
    Mode m = Mode::IDLE;
    Telemetry out;
    ByteReader is(os);
    ser.Read(is, i, m, out);
    DMQ_ASSERT_TRUE(is.good());
    DMQ_ASSERT_TRUE(is.remaining() == 0);
    DMQ_ASSERT_TRUE(i == -123456);
    DMQ_ASSERT_TRUE(m == Mode::RUN);
    DMQ_ASSERT_TRUE(out.seq == 7 && out.mode == Mode::RUN && out.pos == (Point{ -3, 40 }) && out.speed == 1.5f);
}

static void ArrayTests()
{
    raw::Serializer<void(const AdcBlock&)> ser;
    AdcBlock block;
    block.seq = 99;
    for (size_t k = 0; k < block.samples.size(); ++k)
        block.samples[k] = static_cast<int16_t>(k * 3 - 700);

    ByteWriter os;
    ser.Write(os, block);
    DMQ_ASSERT_TRUE(os.good() && os.size() == sizeof(AdcBlock));

    AdcBlock out;                               // DelegateRemote reads into non-const storage
    const AdcBlock& ref = out;
    ByteReader is(os);
    ser.Read(is, ref);
    DMQ_ASSERT_TRUE(is.good());
    DMQ_ASSERT_TRUE(out.seq == 99);
    DMQ_ASSERT_TRUE(std::memcmp(out.samples.data(), block.samples.data(), sizeof(block.samples)) == 0);
}

static void PointerTests()
{
    raw::Serializer<void(const Point*)> ser;
    Point p{ 5, -6 };
    const Point* pp = &p;

    ByteWriter os;
    ser.Write(os, pp);
    DMQ_ASSERT_TRUE(os.good() && os.size() == sizeof(Point));   // the object, not the pointer

    Point storage;
    const Point* dest = &storage;
    ByteReader is(os);
    ser.Read(is, dest);
    DMQ_ASSERT_TRUE(is.good());
    DMQ_ASSERT_TRUE(storage == (Point{ 5, -6 }));

    // A null pointer can't be sent
    const Point* null = nullptr;
    ByteWriter os2;
    ser.Write(os2, null);
    DMQ_ASSERT_TRUE(os2.fail());
}

static void SizeCheckTests()
{
    // A sender with a different layout: 4 bytes sent, 8 expected
    raw::Serializer<void(int32_t)> sender;
    raw::Serializer<void(int64_t)> receiver;
    ByteWriter os;
    sender.Write(os, 42);

    int64_t v = 0;
    ByteReader is(os);
    receiver.Read(is, v);
    DMQ_ASSERT_TRUE(is.fail());

    // Too many bytes fails too
    raw::Serializer<void(int16_t)> small;
    int16_t s = 0;
    ByteReader is2(os);
    small.Read(is2, s);
    DMQ_ASSERT_TRUE(is2.fail());

    // No arguments: zero bytes
    raw::Serializer<void()> none;
    ByteWriter os3;
    none.Write(os3);
    DMQ_ASSERT_TRUE(os3.good() && os3.size() == 0);
    ByteReader is3(os3);
    none.Read(is3);
    DMQ_ASSERT_TRUE(is3.good());
}

static void OverflowTests()
{
    // A buffer smaller than the message: the write fails without allocating more
    raw::Serializer<void(const AdcBlock&)> ser;
    AdcBlock block;
    ByteWriter os(64);
    ser.Write(os, block);
    DMQ_ASSERT_TRUE(os.fail());
    DMQ_ASSERT_TRUE(os.overflowed());
}

static void RemoteDelegateTests()
{
    // A remote delegate call carried by the raw serializer, end to end
    constexpr DelegateRemoteId REMOTE_ID = 1;
    raw::Serializer<void(const AdcBlock&)> ser;
    LoopbackDispatcher dispatcher;
    ByteWriter os;

    auto sender = MakeDelegate(&OnAdcBlock, REMOTE_ID);
    sender.SetStream(&os);
    sender.SetDispatcher(&dispatcher);
    sender.SetSerializer(&ser);

    AdcBlock block;
    block.seq = 1234;
    block.samples[0] = -1;
    block.samples[511] = 511;
    g_calls = 0;
    sender(block);

    DelegateFreeRemote<void(const AdcBlock&)> receiver(&OnAdcBlock, REMOTE_ID);
    receiver.SetSerializer(&ser);
    receiver.Invoke(dispatcher.Received());

    DMQ_ASSERT_TRUE(g_calls == 1);
    DMQ_ASSERT_TRUE(g_received.seq == 1234);
    DMQ_ASSERT_TRUE(g_received.samples[0] == -1 && g_received.samples[511] == 511);
}

void RawSerializerTests()
{
    ValueTests();
    ArrayTests();
    PointerTests();
    SizeCheckTests();
    OverflowTests();
    RemoteDelegateTests();
}
