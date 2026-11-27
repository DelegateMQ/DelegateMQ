#ifndef DMQ_FRAMED_STREAM_TRANSPORT_H
#define DMQ_FRAMED_STREAM_TRANSPORT_H

/// @file FramedStreamTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief DelegateMQ messages over any byte stream: a UART, USB-CDC, RS-485,
/// a BLE serial service, a PC serial port. The frame (FrameCodec.h), the
/// locking, the ACKs and the timeouts are here; a link supplies only an
/// IByteStream: write bytes, and read what has arrived.
///
/// **Porting to a new link.** Implement IByteStream:
/// - Write(): send all the bytes (blocking is fine; Send() holds a lock).
/// - Read(): return what has arrived, up to len bytes, waiting at most
///   timeout for the first. An interrupt-driven UART can use IsrByteStream
///   (IsrByteStream.h) for this: the receive interrupt calls PutFromIsr().
/// Stm32UartTransport (STM32 HAL) and SerialTransport (libserialport) are
/// such adapters, about 50 lines each.
///
/// **Receive().** Waits up to the receive timeout (SetRecvTimeout(), default
/// DMQ_UART_RECV_TIMEOUT_MS) for a frame to start, then returns
/// RECEIVE_NO_DATA, so the calling thread can check for exit. Once a frame has
/// started, each next byte must arrive within the frame timeout
/// (SetFrameTimeout(), default DMQ_UART_FRAME_TIMEOUT_MS), or the frame is
/// dropped as truncated, so a frame cut short by lost bytes doesn't swallow
/// the next one. A bad CRC, an invalid header, a payload over MaxPayload, a
/// truncated frame or a failed read returns -1, is counted
/// (GetRecvErrorCount()) and is reported to SetRecvErrorHandler(), with the
/// kind. Bytes between frames (line noise, a peer starting up) are skipped,
/// within the same receive timeout: endless noise still returns
/// RECEIVE_NO_DATA in time.
///
/// **ACKs.** As on the other transports: a received message other than an ACK
/// is ACKed through the send transport (this one by default), and a received
/// ACK clears its entry in the TransportMonitor, if one is set. A link that
/// uses no TransportMonitor can SetSendTransport(nullptr) to send none.

#include "delegate/DelegateOpt.h"
#include "delegate/UnicastDelegate.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/ITransportMonitor.h"
#include "port/transport/common/DmqHeader.h"
#include "FrameCodec.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dmq::transport {

/// The byte stream under a FramedStreamTransport.
class IByteStream
{
public:
    virtual ~IByteStream() = default;

    /// Write all len bytes.
    /// @return true if all were written
    virtual bool Write(const uint8_t* data, size_t len) = 0;

    /// Read the bytes that have arrived, up to len, waiting at most timeout
    /// for the first.
    /// @return the number of bytes read; 0 if none arrived in time; negative
    /// if the stream failed or is closed
    virtual int Read(uint8_t* buf, size_t len, dmq::Duration timeout) = 0;
};

/// Why Receive() dropped a frame or failed, or Send() failed (SetRecvErrorHandler()).
enum class StreamError : uint8_t
{
    BAD_CRC,        ///< A whole frame whose CRC doesn't match: corrupted on the line
    BAD_HEADER,     ///< A marker, but an invalid header (another protocol version)
    TOO_LARGE,      ///< A received payload over MaxPayload
    TRUNCATED,      ///< A frame stopped arriving (bytes lost) within the frame timeout
    READ_FAILED,    ///< IByteStream::Read() failed
    WRITE_FAILED    ///< IByteStream::Write() failed (Send())
};

/// @tparam MaxPayload the largest payload sent or received (the receive buffer)
template <size_t MaxPayload = dmq::UART_MAX_PAYLOAD>
class FramedStreamTransport : public ITransport
{
public:
    explicit FramedStreamTransport(IByteStream* stream = nullptr)
        : m_stream(stream), m_sendTransport(this), m_recvTransport(this) {}

    FramedStreamTransport(const FramedStreamTransport&) = delete;
    FramedStreamTransport& operator=(const FramedStreamTransport&) = delete;

    /// Frame and write a message. Blocks while another Send() writes.
    /// @return 0, or -1: no stream, `os` failed, a payload over MaxPayload, or
    /// the write failed (reported as WRITE_FAILED)
    int Send(const dmq::ByteWriter& os, const DmqHeader& header) override
    {
        if (os.fail())
            return -1;
        if (os.size() > MaxPayload) {
            m_sendErrors.fetch_add(1);
            return -1;
        }
        const uint16_t size = static_cast<uint16_t>(os.size());
        FrameEnvelope envelope;
        EncodeFrame(header, os.data(), size, envelope);

        bool ok;
        {
            const dmq::LockGuard<dmq::Mutex> lock(m_sendLock);
            if (m_stream == nullptr)
                return -1;
            ok = m_stream->Write(envelope.header, sizeof(envelope.header)) &&
                 (size == 0 || m_stream->Write(reinterpret_cast<const uint8_t*>(os.data()), size)) &&
                 m_stream->Write(envelope.crc, sizeof(envelope.crc));
        }
        if (!ok) {
            m_sendErrors.fetch_add(1);
            ReportError(StreamError::WRITE_FAILED);
            return -1;
        }
        return 0;
    }

    /// Wait for the next frame (see the file comment for the timeouts).
    /// @return 0 with the message in `is` (valid until the next Receive()),
    /// RECEIVE_NO_DATA, or -1 (a dropped frame or a failed read, reported)
    int Receive(dmq::ByteReader& is, DmqHeader& header) override
    {
        is.assign(nullptr, 0);   // nothing until a message arrives
        StreamError error = StreamError::READ_FAILED;
        ITransportMonitor* monitor = nullptr;
        ITransport* ackTransport = nullptr;
        int result;
        {
            const dmq::LockGuard<dmq::Mutex> lock(m_recvLock);
            if (m_stream == nullptr || m_recvTransport != this)
                return -1;
            result = ReceiveFrame(is, header, error);
            monitor = m_transportMonitor;
            ackTransport = m_sendTransport;
        }

        // Everything below runs outside the receive lock: the error handler,
        // TransportMonitor::Remove() (it fires OnSendStatus) and the ACK's
        // Send() (which may report an error) can all call back into this
        // transport. `is` views the decoder's buffer, kept until the next Receive().
        if (result == -1) {
            m_recvErrors.fetch_add(1);
            ReportError(error);
            return -1;
        }
        if (result != 0)
            return result;   // RECEIVE_NO_DATA

        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (monitor)
                monitor->Remove(header.GetSeqNum());
        }
        // ACK whenever a send path exists, whether or not this transport also
        // tracks its own sends: a receive-only transport (e.g. NetworkNode's
        // SUB) has no monitor but must still ACK the sender's RELIABLE messages
        else if (ackTransport) {
            dmq::ByteWriter empty;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            ackTransport->Send(empty, ack);
        }
        return 0;
    }

    /// How long Receive() waits for a frame to start before returning
    /// RECEIVE_NO_DATA. Default DMQ_UART_RECV_TIMEOUT_MS.
    void SetRecvTimeout(dmq::Duration timeout) { m_recvTimeout.store(ToMs(timeout)); }

    /// How long each byte of a started frame may take. Default
    /// DMQ_UART_FRAME_TIMEOUT_MS; a PC serial port may need more (OS latency).
    void SetFrameTimeout(dmq::Duration timeout) { m_frameTimeout.store(ToMs(timeout)); }

    /// Waits for a Receive() in progress (they read these under its lock).
    void SetTransportMonitor(ITransportMonitor* tm)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_recvLock);
        m_transportMonitor = tm;
    }
    void SetSendTransport(ITransport* st)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_recvLock);
        m_sendTransport = st;
    }
    void SetRecvTransport(ITransport* rt)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_recvLock);
        m_recvTransport = rt;
    }

    /// Called with each receive or send failure's kind, on the thread that hit
    /// it, outside the send and receive locks (so it may Send()). It must not
    /// call SetRecvErrorHandler(): it runs under the handler's own lock.
    void SetRecvErrorHandler(dmq::UnicastDelegate<void(StreamError)> handler)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_handlerLock);
        m_errorHandler = std::move(handler);
    }

    /// Frames Receive() dropped or reads that failed.
    uint32_t GetRecvErrorCount() const { return m_recvErrors.load(); }

    /// Sends that failed: a payload over MaxPayload or a failed write.
    uint32_t GetSendErrorCount() const { return m_sendErrors.load(); }

protected:
    /// Attach the stream (an adapter's Create()) or detach it (nullptr, its
    /// Close()). Waits for a Send() or Receive() in progress, so the old
    /// stream can be closed once this returns.
    void SetStream(IByteStream* stream)
    {
        const dmq::LockGuard<dmq::Mutex> recvLock(m_recvLock);
        const dmq::LockGuard<dmq::Mutex> sendLock(m_sendLock);
        m_stream = stream;
        m_decoder.Reset();
        m_rxPos = m_rxLen = 0;
    }

private:
    static uint32_t ToMs(dmq::Duration timeout)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        return ms < 0 ? 0u : (ms > 0x7FFFFFFF ? 0x7FFFFFFFu : static_cast<uint32_t>(ms));
    }

    void ReportError(StreamError error)
    {
        // Called under m_handlerLock, not a copy: copying a delegate may allocate
        const dmq::LockGuard<dmq::Mutex> lock(m_handlerLock);
        if (m_errorHandler)
            m_errorHandler(error);
    }

    // Under m_recvLock. Returns 0, RECEIVE_NO_DATA, or -1 with error set.
    int ReceiveFrame(dmq::ByteReader& is, DmqHeader& header, StreamError& error)
    {
        // One receive timeout for the whole wait for a frame to start, not per
        // byte: a line that never stops sending noise (a floating RX pin,
        // another protocol) must still return RECEIVE_NO_DATA in time, so the
        // calling thread can check for exit and Close() isn't blocked
        const auto syncDeadline = dmq::Clock::now() + std::chrono::milliseconds(m_recvTimeout.load());
        bool readOnce = false;
        for (;;) {
            if (m_rxPos == m_rxLen) {
                // Inside a frame (or after its first marker byte) the rest must
                // follow within the frame timeout; otherwise wait for one to
                // start, until the receive timeout runs out
                const bool started = m_decoder.InFrame() || m_decoder.AfterMarkerStart();
                dmq::Duration wait = std::chrono::milliseconds(m_frameTimeout.load());
                if (!started) {
                    const auto now = dmq::Clock::now();
                    if (readOnce && now >= syncDeadline) {
                        m_decoder.Reset();
                        return RECEIVE_NO_DATA;
                    }
                    wait = (syncDeadline > now) ? std::chrono::duration_cast<dmq::Duration>(syncDeadline - now)
                                                : dmq::Duration::zero();
                }
                const int n = m_stream->Read(m_rx, sizeof(m_rx), wait);
                readOnce = true;
                if (n < 0) {
                    m_decoder.Reset();
                    error = StreamError::READ_FAILED;
                    return -1;
                }
                if (n == 0) {
                    const bool truncated = m_decoder.InFrame();
                    m_decoder.Reset();   // a lone 0xAA is just a stray byte
                    if (truncated) {
                        error = StreamError::TRUNCATED;
                        return -1;
                    }
                    return RECEIVE_NO_DATA;
                }
                m_rxPos = 0;
                m_rxLen = static_cast<size_t>(n);
            }

            const FrameStatus status = m_decoder.Put(m_rx[m_rxPos++]);
            switch (status) {
            case FrameStatus::NEED_MORE:
                continue;
            case FrameStatus::BAD_CRC:    error = StreamError::BAD_CRC;    return -1;
            case FrameStatus::BAD_HEADER: error = StreamError::BAD_HEADER; return -1;
            case FrameStatus::TOO_LARGE:  error = StreamError::TOO_LARGE;  return -1;
            case FrameStatus::FRAME:
                break;
            }

            header = m_decoder.Header();
            // The payload, viewed in place: the decoder keeps it until the next Receive()
            is.assign(reinterpret_cast<const char*>(m_decoder.Payload()), m_decoder.PayloadSize());
            return 0;   // Receive() handles ACKs, outside the lock
        }
    }

    IByteStream* m_stream;
    ITransport* m_sendTransport;
    ITransport* m_recvTransport;
    ITransportMonitor* m_transportMonitor = nullptr;

    dmq::Mutex m_sendLock;      ///< Send(): one frame on the line at a time
    dmq::Mutex m_recvLock;      ///< Receive(): the decoder and m_rx
    dmq::Mutex m_handlerLock;   ///< m_errorHandler
    dmq::UnicastDelegate<void(StreamError)> m_errorHandler;

    FrameDecoder<MaxPayload> m_decoder;
    uint8_t m_rx[64] = {};      ///< Bytes read but not yet decoded (a fixed scratch buffer)
    size_t m_rxPos = 0;
    size_t m_rxLen = 0;

    std::atomic<uint32_t> m_recvTimeout{ static_cast<uint32_t>(dmq::UART_RECV_TIMEOUT.count()) };
    std::atomic<uint32_t> m_frameTimeout{ static_cast<uint32_t>(dmq::UART_FRAME_TIMEOUT.count()) };
    std::atomic<uint32_t> m_recvErrors{ 0 };
    std::atomic<uint32_t> m_sendErrors{ 0 };
};

} // namespace dmq::transport

#endif // DMQ_FRAMED_STREAM_TRANSPORT_H
