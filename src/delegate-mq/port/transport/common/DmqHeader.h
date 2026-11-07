#ifndef DMQ_HEADER_H
#define DMQ_HEADER_H

/// @file
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "delegate/DelegateOpt.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dmq::transport {

/// @brief Header for remote delegate messages.
/// @details Stores values in host byte order. On the wire it is 10 bytes, every
/// multi-byte field big-endian (network byte order):
///
/// | Offset | Size | Field    | Notes                                              |
/// |--------|------|----------|----------------------------------------------------|
/// | 0      | 2    | marker   | Always `MARKER` (0xAA55). Frame check.             |
/// | 2      | 1    | nodeId   | Sender's node ID. 0 = unset.                       |
/// | 3      | 1    | version  | Wire protocol version, `VERSION`.                  |
/// | 4      | 2    | id       | DelegateRemoteId. `ACK_REMOTE_ID` (0) for an ACK.  |
/// | 6      | 2    | seqNum   | Sender's sequence number.                          |
/// | 8      | 2    | length   | Payload bytes following the header.                |
///
/// Transports write and read the header with `Encode()`/`Decode()` and check it
/// with `IsValid()`, so the layout lives only here.
class DmqHeader
{
public:
    /// Frame marker (10101010 01010101).
    static constexpr uint16_t MARKER = 0xAA55;

    /// Wire protocol version this build sends and accepts.
    static constexpr uint8_t VERSION = 1;

    /// Encoded header size in bytes.
    static constexpr size_t HEADER_SIZE = 10;

    DmqHeader() = default;
    DmqHeader(uint16_t id, uint16_t seqNum, uint16_t length = 0)
        : m_id(id), m_seqNum(seqNum), m_length(length) {
    }

    // --- Getters (host byte order) ---

    uint16_t GetMarker()  const { return m_marker; }
    uint8_t  GetNodeId()  const { return m_nodeId; }
    uint8_t  GetVersion() const { return m_version; }
    uint16_t GetId()      const { return m_id; }
    uint16_t GetSeqNum()  const { return m_seqNum; }
    uint16_t GetLength()  const { return m_length; }

    // --- Setters (host byte order) ---

    void SetMarker(uint16_t marker)   { m_marker = marker; }
    void SetNodeId(uint8_t nodeId)    { m_nodeId = nodeId; }
    void SetVersion(uint8_t version)  { m_version = version; }
    void SetId(uint16_t id)           { m_id = id; }
    void SetSeqNum(uint16_t seqNum)   { m_seqNum = seqNum; }
    void SetLength(uint16_t length)   { m_length = length; }

    /// @brief True if the marker matches. False means garbage or lost framing.
    bool HasValidMarker() const { return m_marker == MARKER; }

    /// @brief True if the marker and protocol version both match. A valid marker
    /// with another version is a well-formed frame from an incompatible peer.
    bool IsValid() const { return m_marker == MARKER && m_version == VERSION; }

    /// @brief Write the header to `buf` in wire format.
    /// @param[out] buf At least HEADER_SIZE bytes.
    void Encode(void* buf) const {
        uint8_t* p = static_cast<uint8_t*>(buf);
        Put16(p + 0, m_marker);
        p[2] = m_nodeId;
        p[3] = m_version;
        Put16(p + 4, m_id);
        Put16(p + 6, m_seqNum);
        Put16(p + 8, m_length);
    }

    /// @brief Read a header from `buf` in wire format. Doesn't validate it;
    /// call IsValid() afterwards.
    /// @param[in] buf At least HEADER_SIZE bytes.
    void Decode(const void* buf) {
        const uint8_t* p = static_cast<const uint8_t*>(buf);
        m_marker  = Get16(p + 0);
        m_nodeId  = p[2];
        m_version = p[3];
        m_id      = Get16(p + 4);
        m_seqNum  = Get16(p + 6);
        m_length  = Get16(p + 8);
    }

    /// @brief This process's node ID, stamped into every header it creates.
    /// Defaults to dmq::NODE_ID (DMQ_NODE_ID).
    static uint8_t GetLocalNodeId() { return LocalNodeId().load(std::memory_order_relaxed); }

    /// @brief Set this process's node ID (1-255, 0 = unset). Call once at
    /// startup, before any messages are sent.
    static void SetLocalNodeId(uint8_t nodeId) { LocalNodeId().store(nodeId, std::memory_order_relaxed); }

private:
    static std::atomic<uint8_t>& LocalNodeId() {
        static std::atomic<uint8_t> nodeId{ dmq::NODE_ID };
        return nodeId;
    }

    static void Put16(uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    }
    static uint16_t Get16(const uint8_t* p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    }

    uint16_t m_marker = MARKER;
    uint8_t  m_nodeId = GetLocalNodeId();
    uint8_t  m_version = VERSION;
    uint16_t m_id = 0;              // DelegateRemoteId
    uint16_t m_seqNum = 0;
    uint16_t m_length = 0;          // Payload length
};

} // namespace dmq::transport


#endif
