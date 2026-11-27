/// @file FramedStreamTransportTests.cpp
/// @brief Unit tests for port/transport/framed-stream: FrameCodec.h (the
/// frame and the incremental decoder), FramedStreamTransport.h (an ITransport
/// over any byte stream) and IsrByteStream.h (the interrupt-to-thread ring).
/// No hardware: two in-memory pipes stand in for a serial line.
///
/// Covers: the wire format byte for byte; decoding one byte at a time and in
/// blocks; resync after noise, a stray 0xAA before a marker, a bad CRC, a bad
/// header and an oversized length; zero-length frames (ACKs); a frame cut short
/// (TRUNCATED, then the next frame is read); RECEIVE_NO_DATA on an idle line;
/// ACKs clearing a TransportMonitor; the error handler and counters; a remote
/// delegate call end to end; IsrByteStream's wake-up, timeout and overflow
/// count; and two threads sending at once without interleaving frames.
///
/// @see https://github.com/DelegateMQ/DelegateMQ

#include "DelegateMQ.h"
#include "port/transport/framed-stream/FramedStreamTransport.h"
#include "port/transport/framed-stream/IsrByteStream.h"
#include "UnitTestCommon.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace dmq;
using namespace dmq::transport;
using namespace std::chrono_literals;

namespace {

constexpr size_t MAX_PAYLOAD = 64;

/// One direction of a serial line: bytes written at one end, read at the other.
class Pipe
{
public:
    void Push(const uint8_t* data, size_t len)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_bytes.insert(m_bytes.end(), data, data + len);
            m_written += len;
        }
        m_cv.notify_all();
    }

    int Pop(uint8_t* buf, size_t len, dmq::Duration timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, timeout, [this] { return !m_bytes.empty(); });
        size_t n = 0;
        while (n < len && !m_bytes.empty()) {
            buf[n++] = m_bytes.front();
            m_bytes.pop_front();
        }
        return static_cast<int>(n);
    }

    size_t Written()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_written;
    }

    std::vector<uint8_t> Snapshot()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::vector<uint8_t>(m_bytes.begin(), m_bytes.end());
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<uint8_t> m_bytes;
    size_t m_written = 0;
};

/// One end of the line as an IByteStream.
class PipeEnd : public IByteStream
{
public:
    PipeEnd(Pipe& tx, Pipe& rx) : m_tx(tx), m_rx(rx) {}

    bool Write(const uint8_t* data, size_t len) override
    {
        if (failWrites)
            return false;
        m_tx.Push(data, len);
        return true;
    }

    int Read(uint8_t* buf, size_t len, dmq::Duration timeout) override
    {
        if (failReads)
            return -1;
        return m_rx.Pop(buf, len, timeout);
    }

    std::atomic<bool> failWrites{ false };
    std::atomic<bool> failReads{ false };

private:
    Pipe& m_tx;
    Pipe& m_rx;
};

/// A FramedStreamTransport with its stream attached (SetStream() is protected)
class TestTransport : public FramedStreamTransport<MAX_PAYLOAD>
{
public:
    explicit TestTransport(IByteStream& stream) { SetStream(&stream); }
    void Detach() { SetStream(nullptr); }
};

/// Two transports joined by a line: a <-> b
struct Link
{
    Pipe aToB, bToA;
    PipeEnd aEnd{ aToB, bToA };
    PipeEnd bEnd{ bToA, aToB };
    TestTransport a{ aEnd };
    TestTransport b{ bEnd };

    Link()
    {
        a.SetRecvTimeout(20ms);
        b.SetRecvTimeout(20ms);
        a.SetFrameTimeout(20ms);
        b.SetFrameTimeout(20ms);
    }
};

int SendU32(ITransport& t, DelegateRemoteId id, uint16_t seq, uint32_t value)
{
    ByteWriter os;
    os.write(reinterpret_cast<const char*>(&value), sizeof(value));
    DmqHeader h;
    h.SetId(id);
    h.SetSeqNum(seq);
    return t.Send(os, h);
}

int RecvU32(ITransport& t, DmqHeader& h, uint32_t& value)
{
    ByteReader is;
    const int rc = t.Receive(is, h);
    if (rc == 0) {
        DMQ_ASSERT_TRUE(is.remaining() == sizeof(value));
        is.read(reinterpret_cast<char*>(&value), sizeof(value));
    }
    return rc;
}

/// A whole frame's bytes, built by hand from the documented layout
std::vector<uint8_t> HandFrame(DelegateRemoteId id, uint16_t seq, const std::vector<uint8_t>& payload)
{
    DmqHeader h;
    h.SetId(id);
    h.SetSeqNum(seq);
    h.SetLength(static_cast<uint16_t>(payload.size()));
    std::vector<uint8_t> frame(DmqHeader::HEADER_SIZE);
    h.Encode(frame.data());
    frame.insert(frame.end(), payload.begin(), payload.end());
    const uint16_t crc = dmq::util::Crc16CalcBlock(frame.data(), static_cast<int>(frame.size()), 0xFFFF);
    frame.push_back(static_cast<uint8_t>(crc >> 8));      // big-endian
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    return frame;
}

// --------------------------------------------------------------------------
// FrameCodec
// --------------------------------------------------------------------------

/// The sender's bytes are exactly header, payload, CRC: the wire format the
/// existing serial peers use
void Codec_WireFormat()
{
    Link link;
    const uint32_t value = 0x11223344;
    DMQ_ASSERT_TRUE(SendU32(link.a, 0x0102, 7, value) == 0);

    std::vector<uint8_t> payload(sizeof(value));
    std::memcpy(payload.data(), &value, sizeof(value));
    DMQ_ASSERT_TRUE(link.aToB.Snapshot() == HandFrame(0x0102, 7, payload));
    DMQ_ASSERT_TRUE(link.aToB.Snapshot()[0] == 0xAA && link.aToB.Snapshot()[1] == 0x55);
    std::cout << "Codec_WireFormat() complete!" << std::endl;
}

/// Byte at a time, with noise, a stray 0xAA, and each kind of bad frame in
/// between good ones: every good frame comes out, every bad one is reported
void Codec_ResyncAndErrors()
{
    FrameDecoder<MAX_PAYLOAD> dec;
    std::vector<uint8_t> stream = { 0x00, 0x13, 0xAA, 0x01 };            // noise, a lone 0xAA
    auto good1 = HandFrame(1, 1, { 1, 2, 3 });
    stream.push_back(0xAA);                                              // 0xAA 0xAA 0x55: syncs on the second
    stream.insert(stream.end(), good1.begin(), good1.end());

    auto badCrc = HandFrame(2, 2, { 9, 9 });
    badCrc.back() ^= 0xFF;
    stream.insert(stream.end(), badCrc.begin(), badCrc.end());

    auto badHeader = HandFrame(3, 3, {});
    badHeader[3] = 99;                                                   // version byte
    stream.insert(stream.end(), badHeader.begin(), badHeader.end());

    std::vector<uint8_t> tooLarge = HandFrame(4, 4, std::vector<uint8_t>(MAX_PAYLOAD + 1, 0x5A));
    stream.insert(stream.end(), tooLarge.begin(), tooLarge.end());

    auto ack = HandFrame(dmq::ACK_REMOTE_ID, 5, {});                    // zero-length payload
    stream.insert(stream.end(), ack.begin(), ack.end());

    int frames = 0, crc = 0, header = 0, large = 0;
    std::vector<uint16_t> seqs;
    for (uint8_t b : stream) {
        switch (dec.Put(b)) {
        case FrameStatus::FRAME:
            frames++;
            seqs.push_back(dec.Header().GetSeqNum());
            if (dec.Header().GetSeqNum() == 1) {
                DMQ_ASSERT_TRUE(dec.PayloadSize() == 3 && dec.Payload()[2] == 3);
            }
            if (dec.Header().GetSeqNum() == 5) {
                DMQ_ASSERT_TRUE(dec.PayloadSize() == 0 && dec.Header().GetId() == dmq::ACK_REMOTE_ID);
            }
            break;
        case FrameStatus::BAD_CRC: crc++; break;
        case FrameStatus::BAD_HEADER: header++; break;
        case FrameStatus::TOO_LARGE: large++; break;
        case FrameStatus::NEED_MORE: break;
        }
    }
    DMQ_ASSERT_TRUE(frames == 2 && crc == 1 && header == 1 && large == 1);
    DMQ_ASSERT_TRUE(seqs.size() == 2 && seqs[0] == 1 && seqs[1] == 5);
    std::cout << "Codec_ResyncAndErrors() complete!" << std::endl;
}

/// Reset() drops a partial frame; the next whole one decodes
void Codec_ResetDropsPartial()
{
    FrameDecoder<MAX_PAYLOAD> dec;
    auto frame = HandFrame(8, 8, { 4, 5, 6, 7 });
    for (size_t i = 0; i < 6; i++)
        DMQ_ASSERT_TRUE(dec.Put(frame[i]) == FrameStatus::NEED_MORE);
    DMQ_ASSERT_TRUE(dec.InFrame());
    dec.Reset();
    DMQ_ASSERT_TRUE(!dec.InFrame());
    FrameStatus last = FrameStatus::NEED_MORE;
    for (uint8_t b : frame)
        last = dec.Put(b);
    DMQ_ASSERT_TRUE(last == FrameStatus::FRAME && dec.Header().GetSeqNum() == 8);
    std::cout << "Codec_ResetDropsPartial() complete!" << std::endl;
}

// --------------------------------------------------------------------------
// FramedStreamTransport
// --------------------------------------------------------------------------

void Transport_SendReceive_BothDirections()
{
    Link link;
    link.a.SetSendTransport(nullptr);   // no ACKs in this test
    link.b.SetSendTransport(nullptr);
    DmqHeader h;
    uint32_t v = 0;
    for (uint16_t i = 0; i < 20; i++) {
        DMQ_ASSERT_TRUE(SendU32(link.a, 10, i, 1000u + i) == 0);
        DMQ_ASSERT_TRUE(SendU32(link.b, 11, i, 2000u + i) == 0);
    }
    for (uint16_t i = 0; i < 20; i++) {
        DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && h.GetId() == 10 && h.GetSeqNum() == i && v == 1000u + i);
        DMQ_ASSERT_TRUE(RecvU32(link.a, h, v) == 0 && h.GetId() == 11 && h.GetSeqNum() == i && v == 2000u + i);
    }
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);   // idle line
    DMQ_ASSERT_TRUE(is.remaining() == 0);
    DMQ_ASSERT_TRUE(link.a.GetRecvErrorCount() == 0 && link.b.GetRecvErrorCount() == 0);
    std::cout << "Transport_SendReceive_BothDirections() complete!" << std::endl;
}

/// Over MaxPayload fails Send() and is counted; exactly MaxPayload goes through
void Transport_PayloadLimit()
{
    Link link;
    link.b.SetSendTransport(nullptr);
    DmqHeader h;
    h.SetId(20);
    ByteWriter big;
    std::vector<char> bytes(MAX_PAYLOAD + 1, 'x');
    big.write(bytes.data(), bytes.size());
    DMQ_ASSERT_TRUE(link.a.Send(big, h) == -1);
    DMQ_ASSERT_TRUE(link.a.GetSendErrorCount() == 1);
    DMQ_ASSERT_TRUE(link.aToB.Written() == 0);   // nothing reached the line

    ByteWriter full;
    full.write(bytes.data(), MAX_PAYLOAD);
    DMQ_ASSERT_TRUE(link.a.Send(full, h) == 0);
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == 0 && is.remaining() == MAX_PAYLOAD);
    std::cout << "Transport_PayloadLimit() complete!" << std::endl;
}

/// Line noise and a corrupted frame are skipped or reported; a frame cut short
/// is dropped as TRUNCATED once the frame timeout passes; the next good frame
/// comes through each time
void Transport_ErrorsReported()
{
    Link link;
    link.b.SetSendTransport(nullptr);
    std::vector<StreamError> errors;
    std::mutex errorsMutex;
    link.b.SetRecvErrorHandler(dmq::MakeDelegate([&](StreamError e) {
        std::lock_guard<std::mutex> lock(errorsMutex);
        errors.push_back(e);
    }));

    // Noise, then a frame with a flipped payload bit, then a good frame
    const uint8_t noise[] = { 0x01, 0xAA, 0x02, 0x55 };
    link.aToB.Push(noise, sizeof(noise));
    auto bad = HandFrame(30, 1, { 1, 2, 3, 4 });
    bad[DmqHeader::HEADER_SIZE] ^= 0x01;
    link.aToB.Push(bad.data(), bad.size());
    DMQ_ASSERT_TRUE(SendU32(link.a, 30, 2, 42) == 0);

    DmqHeader h;
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == -1);                       // the bad CRC
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && h.GetSeqNum() == 2 && v == 42);

    // Half a frame, then silence: TRUNCATED after the frame timeout
    auto cut = HandFrame(30, 3, { 7, 7, 7, 7 });
    link.aToB.Push(cut.data(), 8);
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == -1);
    DMQ_ASSERT_TRUE(SendU32(link.a, 30, 4, 43) == 0);
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && h.GetSeqNum() == 4 && v == 43);

    {
        std::lock_guard<std::mutex> lock(errorsMutex);
        DMQ_ASSERT_TRUE(errors.size() == 2);
        DMQ_ASSERT_TRUE(errors[0] == StreamError::BAD_CRC);
        DMQ_ASSERT_TRUE(errors[1] == StreamError::TRUNCATED);
    }
    DMQ_ASSERT_TRUE(link.b.GetRecvErrorCount() == 2);

    // A failed write fails Send(), counted and reported
    std::atomic<int> writeErrors{ 0 };
    link.a.SetRecvErrorHandler(dmq::MakeDelegate([&](StreamError e) {
        if (e == StreamError::WRITE_FAILED) writeErrors++;
    }));
    link.aEnd.failWrites = true;
    DMQ_ASSERT_TRUE(SendU32(link.a, 30, 5, 44) == -1);
    DMQ_ASSERT_TRUE(writeErrors == 1 && link.a.GetSendErrorCount() == 1);
    link.aEnd.failWrites = false;

    // A bad header, an oversized length and a failed read, each reported with its kind
    auto badHeader = HandFrame(30, 7, {});
    badHeader[3] = 99;   // version byte
    link.aToB.Push(badHeader.data(), badHeader.size());
    auto tooLarge = HandFrame(30, 8, std::vector<uint8_t>(MAX_PAYLOAD + 1, 0x11));
    link.aToB.Push(tooLarge.data(), DmqHeader::HEADER_SIZE);   // the header alone is enough
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == -1);
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == -1);
    link.bEnd.failReads = true;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == -1);
    link.bEnd.failReads = false;
    {
        std::lock_guard<std::mutex> lock(errorsMutex);
        DMQ_ASSERT_TRUE(errors.size() == 5);
        DMQ_ASSERT_TRUE(errors[2] == StreamError::BAD_HEADER);
        DMQ_ASSERT_TRUE(errors[3] == StreamError::TOO_LARGE);
        DMQ_ASSERT_TRUE(errors[4] == StreamError::READ_FAILED);
    }

    // A detached stream: both fail
    link.b.Detach();
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == -1);
    DMQ_ASSERT_TRUE(SendU32(link.b, 30, 6, 45) == -1);
    std::cout << "Transport_ErrorsReported() complete!" << std::endl;
}

/// B ACKs what it receives; A's ACK clears its TransportMonitor entry
void Transport_Ack_TransportMonitor()
{
    Link link;
    dmq::util::TransportMonitor monitor(std::chrono::duration_cast<dmq::Duration>(std::chrono::milliseconds(50)));
    link.a.SetTransportMonitor(&monitor);
    std::atomic<int> success{ 0 };
    auto conn = monitor.OnSendStatus.Connect(dmq::MakeDelegate(
        [&](DelegateRemoteId, uint16_t seq, dmq::util::TransportMonitor::Status status) {
            if (status == dmq::util::TransportMonitor::Status::SUCCESS && seq == 9) success++;
        }));

    DMQ_ASSERT_TRUE(monitor.Add(9, 40));
    DMQ_ASSERT_TRUE(SendU32(link.a, 40, 9, 900) == 0);
    DmqHeader h;
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 900);   // B sends the ACK
    ByteReader is;
    DMQ_ASSERT_TRUE(link.a.Receive(is, h) == 0);               // A takes it
    DMQ_ASSERT_TRUE(h.GetId() == dmq::ACK_REMOTE_ID && h.GetSeqNum() == 9);
    DMQ_ASSERT_TRUE(success == 1);

    // The ACK's status callback runs outside the receive lock: it may call
    // back into the transport (here a setter that takes that lock)
    std::atomic<int> reentered{ 0 };
    auto conn2 = monitor.OnSendStatus.Connect(dmq::MakeDelegate(
        [&](DelegateRemoteId, uint16_t, dmq::util::TransportMonitor::Status) {
            link.a.SetTransportMonitor(&monitor);
            reentered++;
        }));
    DMQ_ASSERT_TRUE(monitor.Add(10, 40));
    DMQ_ASSERT_TRUE(SendU32(link.a, 40, 10, 1000) == 0);
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 1000);
    DMQ_ASSERT_TRUE(link.a.Receive(is, h) == 0 && h.GetSeqNum() == 10);
    DMQ_ASSERT_TRUE(reentered == 1);
    std::cout << "Transport_Ack_TransportMonitor() complete!" << std::endl;
}

/// A line that never stops sending noise (a floating RX pin, another
/// protocol): Receive() still returns RECEIVE_NO_DATA within about its
/// receive timeout, and a frame that then arrives is read
void Transport_EndlessNoise_ReturnsNoData()
{
    Link link;
    link.b.SetSendTransport(nullptr);
    link.b.SetRecvTimeout(50ms);
    std::atomic<bool> stop{ false };
    std::thread noise([&] {
        const uint8_t junk[] = { 0x13, 0x37, 0x00, 0xFF };
        while (!stop) {
            link.aToB.Push(junk, sizeof(junk));
            std::this_thread::sleep_for(1ms);
        }
    });
    DmqHeader h;
    uint32_t v = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int rc = RecvU32(link.b, h, v);
    const auto took = std::chrono::steady_clock::now() - t0;
    stop = true;
    noise.join();
    DMQ_ASSERT_TRUE(rc == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(took < 1s);   // not stuck on the noise
    DMQ_ASSERT_TRUE(link.b.GetRecvErrorCount() == 0);   // noise isn't an error

    DMQ_ASSERT_TRUE(SendU32(link.a, 80, 1, 81) == 0);
    int got;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do { got = RecvU32(link.b, h, v); } while (got == ITransport::RECEIVE_NO_DATA && std::chrono::steady_clock::now() < deadline);
    DMQ_ASSERT_TRUE(got == 0 && v == 81);
    std::cout << "Transport_EndlessNoise_ReturnsNoData() complete!" << std::endl;
}

/// Two threads sending at once: every frame arrives whole (Send() holds the
/// line for a whole frame), each thread's in its own order
void Transport_ConcurrentSenders()
{
    Link link;
    link.b.SetSendTransport(nullptr);
    constexpr uint16_t COUNT = 300;
    auto sender = [&](DelegateRemoteId id) {
        for (uint16_t i = 0; i < COUNT; i++)
            DMQ_ASSERT_TRUE(SendU32(link.a, id, i, (static_cast<uint32_t>(id) << 16) | i) == 0);
    };
    std::thread t1(sender, 50), t2(sender, 51);

    uint16_t next[2] = { 0, 0 };
    int received = 0;
    DmqHeader h;
    uint32_t v = 0;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (received < 2 * COUNT && std::chrono::steady_clock::now() < deadline) {
        const int rc = RecvU32(link.b, h, v);
        if (rc == ITransport::RECEIVE_NO_DATA)
            continue;
        DMQ_ASSERT_TRUE(rc == 0);
        const int k = h.GetId() - 50;
        DMQ_ASSERT_TRUE(k == 0 || k == 1);
        DMQ_ASSERT_TRUE(h.GetSeqNum() == next[k] && v == ((static_cast<uint32_t>(h.GetId()) << 16) | next[k]));
        next[k]++;
        received++;
    }
    t1.join();
    t2.join();
    DMQ_ASSERT_TRUE(received == 2 * COUNT && link.b.GetRecvErrorCount() == 0);
    std::cout << "Transport_ConcurrentSenders() complete!" << std::endl;
}

// A remote delegate call across the link
std::atomic<int> s_remoteSum{ 0 };
void RemoteTarget(int a, int b) { s_remoteSum += a + b; }

void Transport_RemoteDelegate_EndToEnd()
{
    Link link;
    link.a.SetSendTransport(nullptr);   // A ignores B's ACKs here
    link.b.SetSendTransport(nullptr);
    constexpr DelegateRemoteId ID = 60;
    dmq::serialization::raw::Serializer<void(int, int)> ser;
    RemoteChannel<void(int, int)> sender(link.a, ser, ID);
    DelegateFreeRemote<void(int, int)> receiver(&RemoteTarget, ID);
    receiver.SetSerializer(&ser);

    s_remoteSum = 0;
    sender(2, 3);
    sender(10, 20);
    for (int i = 0; i < 2; i++) {
        ByteReader is;
        DmqHeader h;
        DMQ_ASSERT_TRUE(link.b.Receive(is, h) == 0 && h.GetId() == ID);
        receiver.Invoke(is);
    }
    DMQ_ASSERT_TRUE(s_remoteSum == 35);
    std::cout << "Transport_RemoteDelegate_EndToEnd() complete!" << std::endl;
}

// --------------------------------------------------------------------------
// IsrByteStream
// --------------------------------------------------------------------------

void IsrStream_WakeTimeoutOverflow()
{
    IsrByteStream<16> stream;
    uint8_t buf[32] = {};

    // Nothing there: times out with 0
    const auto t0 = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(stream.Read(buf, sizeof(buf), std::chrono::milliseconds(30)) == 0);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t0 >= 25ms);

    // A "driver" thread puts bytes while the reader waits: woken, not timed out
    std::thread producer([&] {
        std::this_thread::sleep_for(20ms);
        const uint8_t data[] = { 1, 2, 3 };
        stream.PutFromIsr(data, sizeof(data));
    });
    const auto t1 = std::chrono::steady_clock::now();
    int n = stream.Read(buf, sizeof(buf), std::chrono::seconds(5));
    producer.join();
    DMQ_ASSERT_TRUE(n >= 1 && buf[0] == 1);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t1 < 2s);
    while (n < 3)
        n += stream.Read(buf + n, sizeof(buf) - n, std::chrono::milliseconds(100));
    DMQ_ASSERT_TRUE(buf[1] == 2 && buf[2] == 3);

    // Full ring: the extra bytes are dropped and counted, the rest kept in order
    for (int i = 0; i < 20; i++)
        stream.PutFromIsr(static_cast<uint8_t>(i));
    DMQ_ASSERT_TRUE(stream.GetOverflowCount() == 4);
    n = stream.Read(buf, sizeof(buf), std::chrono::milliseconds(10));
    DMQ_ASSERT_TRUE(n == 16 && buf[0] == 0 && buf[15] == 15);
    std::cout << "IsrStream_WakeTimeoutOverflow() complete!" << std::endl;
}

/// A transport over an IsrByteStream, bytes fed from another thread in small
/// irregular blocks, as a UART interrupt would
void IsrStream_UnderTransport()
{
    struct IsrEnd : IByteStream
    {
        IsrByteStream<256> rx;
        bool Write(const uint8_t*, size_t) override { return true; }
        int Read(uint8_t* buf, size_t len, dmq::Duration t) override { return rx.Read(buf, len, t); }
    } end;
    TestTransport t(end);
    t.SetSendTransport(nullptr);
    t.SetRecvTimeout(50ms);

    std::vector<uint8_t> bytes;
    for (uint16_t i = 0; i < 50; i++) {
        uint32_t v = 7000u + i;
        std::vector<uint8_t> p(4);
        std::memcpy(p.data(), &v, 4);
        auto f = HandFrame(70, i, p);
        bytes.insert(bytes.end(), f.begin(), f.end());
    }
    std::thread isr([&] {
        size_t pos = 0, step = 1, sincePause = 0;
        while (pos < bytes.size()) {
            const size_t n = (std::min)(step, bytes.size() - pos);
            end.rx.PutFromIsr(bytes.data() + pos, n);
            pos += n;
            sincePause += n;
            step = step % 7 + 1;
            if (sincePause >= 64) {
                // Like a UART at a finite baud rate: the reader keeps up, the
                // ring never fills (an overflow would fail a frame's CRC)
                std::this_thread::sleep_for(2ms);
                sincePause = 0;
            }
        }
    });
    DmqHeader h;
    uint32_t v = 0;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (uint16_t i = 0; i < 50; i++) {
        int rc;
        do { rc = RecvU32(t, h, v); }
        while (rc == ITransport::RECEIVE_NO_DATA && std::chrono::steady_clock::now() < deadline);
        DMQ_ASSERT_TRUE(rc == 0 && h.GetSeqNum() == i && v == 7000u + i);
    }
    isr.join();
    DMQ_ASSERT_TRUE(end.rx.GetOverflowCount() == 0);
    DMQ_ASSERT_TRUE(t.GetRecvErrorCount() == 0);
    std::cout << "IsrStream_UnderTransport() complete!" << std::endl;
}

// --------------------------------------------------------------------------
// TransportAck.h (the ACK step every transport shares)
// --------------------------------------------------------------------------

void TransportAck_Helpers()
{
    struct RecordingMonitor : ITransportMonitor
    {
        std::vector<uint16_t> removed;
        bool Add(uint16_t, DelegateRemoteId) override { return true; }
        void Remove(uint16_t seq) override { removed.push_back(seq); }
    } monitor;
    struct RecordingTransport : ITransport
    {
        std::vector<DmqHeader> sent;
        size_t payloadBytes = 0;
        int Send(const ByteWriter& os, const DmqHeader& h) override
        {
            sent.push_back(h);
            payloadBytes += os.size();
            return 0;
        }
        int Receive(ByteReader&, DmqHeader&) override { return RECEIVE_NO_DATA; }
    } transport;

    // An ACK clears its entry and sends nothing
    DmqHeader ack(dmq::ACK_REMOTE_ID, 21);
    AckReceived(ack, &monitor, &transport);
    DMQ_ASSERT_TRUE(monitor.removed.size() == 1 && monitor.removed[0] == 21);
    DMQ_ASSERT_TRUE(transport.sent.empty());

    // A message is ACKed back: an empty message with the same sequence number
    DmqHeader msg(90, 22);
    AckReceived(msg, &monitor, &transport);
    DMQ_ASSERT_TRUE(transport.sent.size() == 1);
    DMQ_ASSERT_TRUE(transport.sent[0].GetId() == dmq::ACK_REMOTE_ID && transport.sent[0].GetSeqNum() == 22);
    DMQ_ASSERT_TRUE(transport.payloadBytes == 0);
    DMQ_ASSERT_TRUE(monitor.removed.size() == 1);   // not an ACK: nothing removed

    // No monitor, no send transport: an ACK is still recognized; nothing is sent
    DMQ_ASSERT_TRUE(HandleAck(ack, nullptr));
    DMQ_ASSERT_TRUE(!HandleAck(msg, nullptr));
    AckReceived(msg, nullptr, nullptr);
    DMQ_ASSERT_TRUE(transport.sent.size() == 1);

    // EncodeAck(): a header-only frame that decodes back to the ACK (both buffer types)
    char chars[DmqHeader::HEADER_SIZE];
    uint8_t bytes[DmqHeader::HEADER_SIZE];
    EncodeAck(23, chars);
    EncodeAck(23, bytes);
    DMQ_ASSERT_TRUE(std::memcmp(chars, bytes, DmqHeader::HEADER_SIZE) == 0);
    DmqHeader decoded;
    decoded.Decode(bytes);
    DMQ_ASSERT_TRUE(decoded.IsValid() && decoded.GetId() == dmq::ACK_REMOTE_ID);
    DMQ_ASSERT_TRUE(decoded.GetSeqNum() == 23 && decoded.GetLength() == 0);
    std::cout << "TransportAck_Helpers() complete!" << std::endl;
}

} // namespace

void FramedStreamTransportTests()
{
    TransportAck_Helpers();
    Codec_WireFormat();
    Codec_ResyncAndErrors();
    Codec_ResetDropsPartial();
    Transport_SendReceive_BothDirections();
    Transport_PayloadLimit();
    Transport_ErrorsReported();
    Transport_Ack_TransportMonitor();
    Transport_EndlessNoise_ReturnsNoData();
    Transport_ConcurrentSenders();
    Transport_RemoteDelegate_EndToEnd();
    IsrStream_WakeTimeoutOverflow();
    IsrStream_UnderTransport();
    std::cout << "FramedStreamTransportTests() complete!" << std::endl;
}
