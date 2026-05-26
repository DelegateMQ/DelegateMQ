#ifndef SERIAL_TRANSPORT_H
#define SERIAL_TRANSPORT_H

/// @file SerialTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Libserialport-based transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the cross-platform `libserialport` 
/// library. It provides a reliable, packet-based communication layer over RS-232/UART 
/// serial links.
/// 
/// Key Features:
/// 1. **Thread-Safe Access**: Uses a recursive mutex to serialize access to the 
///    underlying serial port, allowing concurrent Send/Receive calls from different threads.
/// 2. **Data Framing**: Encapsulates delegate arguments in a binary-safe frame structure:
///    `[Header (8 bytes)] + [Payload (N bytes)] + [CRC16 (2 bytes)]`.
/// 3. **Data Integrity**: Automatically calculates and verifies a 16-bit CRC for every 
///    packet to detect transmission errors common in serial communication.
/// 4. **Reliability**: Integrates with `TransportMonitor` to track sequence numbers and 
///    support ACK-based reliability when paired with the `RetryMonitor`.
/// 
/// @note This class requires `libserialport` to be linked.

#include "libserialport.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"
#include "extras/util/crc16.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace dmq::transport {

class SerialTransport : public ITransport
{
public:
    SerialTransport() : m_sendTransport(this), m_recvTransport(this) {}

    ~SerialTransport() { Close(); }

    int Create(const char* portName, int baudRate)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        sp_return ret = sp_get_port_by_name(portName, &m_port);
        if (ret != SP_OK) {
            std::cerr << "SerialTransport: Could not find port " << portName << std::endl;
            return -1;
        }

        ret = sp_open(m_port, SP_MODE_READ_WRITE);
        if (ret != SP_OK) {
            std::cerr << "SerialTransport: Could not open port " << portName << std::endl;
            sp_free_port(m_port);
            m_port = nullptr;
            return -1;
        }

        sp_set_baudrate(m_port, baudRate);
        sp_set_bits(m_port, 8);
        sp_set_parity(m_port, SP_PARITY_NONE);
        sp_set_stopbits(m_port, 1);
        sp_set_flowcontrol(m_port, SP_FLOWCONTROL_NONE);
        return 0;
    }

    void Close()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_port) {
            sp_close(m_port);
            sp_free_port(m_port);
            m_port = nullptr;
        }
    }

    // Helper: Swap Little Endian <-> Big Endian
    static uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_port) return -1;

        DmqHeader headerCopy = header;
        dmq::xstring payload = os.str();
        if (payload.length() > UINT16_MAX) return -1;
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        dmq::xostringstream ss(std::ios::in | std::ios::out | std::ios::binary);

        // SERIALIZE HEADER (Big Endian)
        uint16_t val;

        val = swap16(headerCopy.GetMarker());
        ss.write((char*)&val, 2);

        val = swap16(headerCopy.GetId());
        ss.write((char*)&val, 2);

        val = swap16(headerCopy.GetSeqNum());
        ss.write((char*)&val, 2);

        val = swap16(headerCopy.GetLength());
        ss.write((char*)&val, 2);

        // Payload
        ss.write(payload.data(), payload.size());

        // CRC
        dmq::xstring packetWithoutCrc = ss.str();
        uint16_t crc = dmq::util::Crc16CalcBlock((unsigned char*)packetWithoutCrc.c_str(), (int)packetWithoutCrc.length(), 0xFFFF);
        ss.write(reinterpret_cast<const char*>(&crc), sizeof(crc));

        dmq::xstring packetData = ss.str();

        int result = sp_blocking_write(m_port, packetData.c_str(), packetData.length(), 1000);
        if (result != (int)packetData.length()) return -1;

        if (headerCopy.GetId() != dmq::ACK_REMOTE_ID && m_transportMonitor) {
            if (m_transportMonitor->Add(headerCopy.GetSeqNum(), headerCopy.GetId()) == false) {
                return -1;
            }
        }

        return 0;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        if (!m_port) return -1;
        if (m_recvTransport != this) return -1;

        char headerBuf[DmqHeader::HEADER_SIZE];

        // 1. Strict Sync Loop
        char b = 0;
        while (true) {
            // Short timeout for Sync, but non-blocking check
            if (sp_blocking_read(m_port, &b, 1, 10) <= 0) return -1;

            if ((uint8_t)b == 0xAA) {
                char next_b = 0;
                if (sp_blocking_read(m_port, &next_b, 1, 100) > 0) {
                    if ((uint8_t)next_b == 0x55) {
                        headerBuf[0] = b;
                        headerBuf[1] = next_b;
                        break;
                    }
                }
            }
        }

        // 2. Read Rest of Header
        // INCREASED TIMEOUT: 1000ms (was 100ms) to allow OS latency
        if (!ReadExact(headerBuf + 2, DmqHeader::HEADER_SIZE - 2, 1000))
            return -1;

        // 3. Deserialize
        uint16_t val;
        memcpy(&val, &headerBuf[0], 2); header.SetMarker(swap16(val));
        memcpy(&val, &headerBuf[2], 2); header.SetId(swap16(val));
        memcpy(&val, &headerBuf[4], 2); header.SetSeqNum(swap16(val));
        memcpy(&val, &headerBuf[6], 2); header.SetLength(swap16(val));

        if (header.GetMarker() != DmqHeader::MARKER) return -1;

        // 4. Payload
        uint16_t len = header.GetLength();
        if (len > 0) {
            if (len > BUFFER_SIZE) return -1;
            // INCREASED TIMEOUT: 1000ms
            if (!ReadExact(m_buffer, len, 1000)) return -1;
            is.write(m_buffer, len);
        }

        // 5. CRC Check
        uint16_t receivedCrc = 0;
        // INCREASED TIMEOUT: 500ms
        if (!ReadExact(reinterpret_cast<char*>(&receivedCrc), sizeof(receivedCrc), 500)) return -1;

        uint16_t calcCrc = dmq::util::Crc16CalcBlock((unsigned char*)headerBuf, DmqHeader::HEADER_SIZE, 0xFFFF);
        if (len > 0) calcCrc = dmq::util::Crc16CalcBlock((unsigned char*)m_buffer, len, calcCrc);

        if (receivedCrc != calcCrc) {
            std::cerr << "CRC Mismatch" << std::endl;
            return -1;
        }

        // 6. ACKs
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_sendTransport) {
            dmq::xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            m_sendTransport->Send(ss_ack, ack);
        }
        return 0;
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        m_recvTimeout = timeout;
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

private:
    bool ReadExact(char* dest, size_t size, unsigned int timeoutMs)
    {
        size_t totalRead = 0;
        // Keep attempting to read until we have 'size' bytes
        while (totalRead < size)
        {
            if (!m_port) return false;
            int ret = sp_blocking_read(m_port, dest + totalRead, size - totalRead, timeoutMs);
            if (ret < 0) return false; // Error
            if (ret == 0) return false; // Timeout
            totalRead += ret;
        }
        return true;
    }

    sp_port* m_port = nullptr;
    std::recursive_mutex m_mutex;
    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;
    std::chrono::milliseconds m_recvTimeout = std::chrono::milliseconds(2000);
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
};

} // namespace dmq::transport

#endif
