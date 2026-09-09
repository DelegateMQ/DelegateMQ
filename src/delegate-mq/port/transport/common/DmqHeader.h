#ifndef DMQ_HEADER_H
#define DMQ_HEADER_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include <cstdint>

namespace dmq::transport {

/// @brief Header for remote delegate messages. 
/// @details This class is a Plain Old Data (POD) container. 
/// It stores values in Host Byte Order. The Transport layer is responsible 
/// for converting to/from Network Byte Order (Big Endian) during transmission.
class DmqHeader
{
public:
    // Standard Marker (0xAA55 is often preferred as it looks like 10101010 01010101 binary)
    static const uint16_t MARKER = 0xAA55;

    // 4 fields * 2 bytes = 8 bytes total. Perfectly aligned.
    static const size_t HEADER_SIZE = 8;

    // Constructor
    DmqHeader() = default;
    DmqHeader(uint16_t id, uint16_t seqNum, uint16_t length = 0)
        : m_id(id), m_seqNum(seqNum), m_length(length) {
    }

    // --- Getters (Return Host Native Values) ---

    uint16_t GetMarker() const { return m_marker; }
    uint16_t GetId()     const { return m_id; }
    uint16_t GetSeqNum() const { return m_seqNum; }
    uint16_t GetLength() const { return m_length; }

    // --- Setters (Store Host Native Values) ---

    void SetId(uint16_t id) { m_id = id; }
    void SetSeqNum(uint16_t seqNum) { m_seqNum = seqNum; }
    void SetMarker(uint16_t marker) { m_marker = marker; }
    void SetLength(uint16_t length) { m_length = length; }

private:
    uint16_t m_marker = MARKER;          // Static marker value
    uint16_t m_id = 0;                   // DelegateRemoteId
    uint16_t m_seqNum = 0;               // Sequence number
    uint16_t m_length = 0;               // Payload length
};

} // namespace dmq::transport


#endif