#ifndef DMQ_FRAME_CODEC_H
#define DMQ_FRAME_CODEC_H

/// @file FrameCodec.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief The DelegateMQ frame on a byte stream (UART, USB-CDC, RS-485, any
/// serial link): encoding, and an incremental decoder that takes one byte at a
/// time. No OS, hardware or timing code, so it is unit-tested on the PC and
/// shared by every stream transport (FramedStreamTransport and its adapters).
///
/// Frame, all multi-byte fields big-endian:
///
/// | Bytes | Field |
/// | :--- | :--- |
/// | 10 | DmqHeader: marker 0xAA 0x55, id, sequence number, version, payload length |
/// | length | payload |
/// | 2 | CRC-16 (dmq::util::Crc16CalcBlock, seed 0xFFFF) over header and payload |
///
/// The decoder finds a frame start by the two marker bytes, tracking the
/// previous byte, so a stray 0xAA right before a real marker (0xAA 0xAA 0x55)
/// still syncs on the real one. A frame with a bad header, a payload too large
/// for the receive buffer or a bad CRC is dropped and the decoder looks for the
/// next marker. Time is the caller's: a frame cut short by lost bytes is
/// dropped by calling Reset() when the next byte doesn't come in time.

#include "delegate/DelegateOpt.h"
#include "port/transport/common/DmqHeader.h"
#include "extras/util/crc16.h"
#include <cstddef>
#include <cstdint>

namespace dmq::transport {

/// What a received byte completed. Every value but NEED_MORE ends the frame.
enum class FrameStatus : uint8_t
{
    NEED_MORE,      ///< Nothing complete yet
    FRAME,          ///< A whole frame with a good CRC: Header() and Payload()
    BAD_HEADER,     ///< Marker found but the header is invalid (wrong version)
    TOO_LARGE,      ///< The header's payload length exceeds the receive buffer
    BAD_CRC         ///< The frame arrived whole but its CRC doesn't match
};

/// Bytes a frame adds to its payload: header and CRC.
inline constexpr size_t FRAME_OVERHEAD = DmqHeader::HEADER_SIZE + 2;

/// The header and CRC around a payload, for a sender that writes the three
/// parts in turn (no copy of the payload).
struct FrameEnvelope
{
    uint8_t header[DmqHeader::HEADER_SIZE];
    uint8_t crc[2];
};

/// Build the header (its length set to payloadSize) and the CRC for a frame.
inline void EncodeFrame(const DmqHeader& header, const void* payload, uint16_t payloadSize,
                        FrameEnvelope& out)
{
    DmqHeader h = header;
    h.SetLength(payloadSize);
    h.Encode(out.header);
    uint16_t crc = dmq::util::Crc16CalcBlock(out.header, static_cast<int>(DmqHeader::HEADER_SIZE), 0xFFFF);
    if (payloadSize > 0)
        crc = dmq::util::Crc16CalcBlock(static_cast<const unsigned char*>(payload),
                                        static_cast<int>(payloadSize), crc);
    out.crc[0] = static_cast<uint8_t>(crc >> 8);
    out.crc[1] = static_cast<uint8_t>(crc & 0xFF);
}

/// Incremental frame decoder.
/// @tparam MaxPayload the largest payload accepted (the receive buffer size)
template <size_t MaxPayload>
class FrameDecoder
{
    static_assert(MaxPayload <= 0xFFFF, "a frame's payload length is 16 bits");

public:
    /// Take the next received byte.
    FrameStatus Put(uint8_t b)
    {
        switch (m_state) {
        case State::SYNC:
            if (m_prev == MARKER_HIGH && b == MARKER_LOW) {
                m_header[0] = MARKER_HIGH;
                m_header[1] = MARKER_LOW;
                m_pos = 2;
                m_state = State::HEADER;
                m_prev = 0;
            } else {
                m_prev = b;
            }
            return FrameStatus::NEED_MORE;

        case State::HEADER:
            m_header[m_pos++] = b;
            if (m_pos < DmqHeader::HEADER_SIZE)
                return FrameStatus::NEED_MORE;
            m_decoded.Decode(m_header);
            if (!m_decoded.IsValid())
                return Fail(FrameStatus::BAD_HEADER);
            m_length = m_decoded.GetLength();
            if (m_length > MaxPayload)
                return Fail(FrameStatus::TOO_LARGE);
            m_pos = 0;
            m_state = (m_length > 0) ? State::PAYLOAD : State::CRC_HIGH;
            return FrameStatus::NEED_MORE;

        case State::PAYLOAD:
            m_payload[m_pos++] = b;
            if (m_pos == m_length)
                m_state = State::CRC_HIGH;
            return FrameStatus::NEED_MORE;

        case State::CRC_HIGH:
            m_crc = static_cast<uint16_t>(static_cast<uint16_t>(b) << 8);
            m_state = State::CRC_LOW;
            return FrameStatus::NEED_MORE;

        case State::CRC_LOW: {
            m_crc = static_cast<uint16_t>(m_crc | b);
            uint16_t calc = dmq::util::Crc16CalcBlock(m_header, static_cast<int>(DmqHeader::HEADER_SIZE), 0xFFFF);
            if (m_length > 0)
                calc = dmq::util::Crc16CalcBlock(m_payload, static_cast<int>(m_length), calc);
            m_state = State::SYNC;
            return (calc == m_crc) ? FrameStatus::FRAME : FrameStatus::BAD_CRC;
        }
        }
        return FrameStatus::NEED_MORE;
    }

    /// Drop a partial frame and look for the next marker: the caller's
    /// inter-byte timeout ran out, or the link was reset.
    void Reset()
    {
        m_state = State::SYNC;
        m_prev = 0;
        m_pos = 0;
    }

    /// Past the marker, inside a frame: its remaining bytes should follow
    /// within the frame timeout.
    bool InFrame() const { return m_state != State::SYNC; }

    /// Waiting for a marker, and the last byte was its first half (0xAA).
    bool AfterMarkerStart() const { return m_state == State::SYNC && m_prev == MARKER_HIGH; }

    /// The last frame's header and payload. Valid after Put() returned FRAME,
    /// until the next Put().
    const DmqHeader& Header() const { return m_decoded; }
    const uint8_t* Payload() const { return m_payload; }
    uint16_t PayloadSize() const { return m_length; }

private:
    enum class State : uint8_t { SYNC, HEADER, PAYLOAD, CRC_HIGH, CRC_LOW };

    static constexpr uint8_t MARKER_HIGH = static_cast<uint8_t>(DmqHeader::MARKER >> 8);
    static constexpr uint8_t MARKER_LOW = static_cast<uint8_t>(DmqHeader::MARKER & 0xFF);

    FrameStatus Fail(FrameStatus status)
    {
        Reset();
        return status;
    }

    State m_state = State::SYNC;
    uint8_t m_prev = 0;
    size_t m_pos = 0;
    uint16_t m_length = 0;
    uint16_t m_crc = 0;
    uint8_t m_header[DmqHeader::HEADER_SIZE] = {};
    DmqHeader m_decoded;
    uint8_t m_payload[MaxPayload > 0 ? MaxPayload : 1] = {};
};

} // namespace dmq::transport

#endif // DMQ_FRAME_CODEC_H
