#ifndef SERIAL_TRANSPORT_H
#define SERIAL_TRANSPORT_H

/// @file SerialTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief A PC serial port (RS-232, USB-UART, USB-CDC) as a DelegateMQ
/// transport, through the cross-platform libserialport library.
///
/// @details
/// An adapter: the frame, the locking, the ACKs and the timeouts are
/// FramedStreamTransport's (port/transport/framed-stream), shared with every
/// other stream link, so this end and an MCU's (Stm32UartTransport, or any
/// IByteStream adapter) speak the same frames by construction. This file only
/// opens the port and moves bytes through libserialport.
///
/// Timing suited to a PC: Receive() waits 10 ms for a frame to start, so a
/// thread can poll it between other work, and gives each byte of a started
/// frame up to a second (OS scheduling latency). SetRecvTimeout() and
/// SetFrameTimeout() change them. Frame errors are counted and reported
/// (SetRecvErrorHandler()).
///
/// @note This class requires `libserialport` to be linked.

#include "libserialport.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/framed-stream/FramedStreamTransport.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>

namespace dmq::transport {

/// Largest payload a SerialTransport sends or receives
inline constexpr size_t SERIAL_MAX_PAYLOAD = 4096;

class SerialTransport : public FramedStreamTransport<SERIAL_MAX_PAYLOAD>, private IByteStream
{
public:
    SerialTransport()
    {
        SetRecvTimeout(std::chrono::milliseconds(10));
        SetFrameTimeout(std::chrono::milliseconds(1000));
    }

    ~SerialTransport() { Close(); }

    /// Open a serial port, 8N1, no flow control.
    /// @param portName e.g. "COM5" or "/dev/ttyUSB0"
    /// @return 0, or -1 if the port isn't there or can't be opened
    int Create(const char* portName, int baudRate)
    {
        // One hold of m_portMutex: a Close() on another thread runs wholly
        // before or after this, never in between. (Lock order: m_portMutex,
        // then the base's receive and send locks in SetStream(); Send() and
        // Receive() never take m_portMutex.)
        std::lock_guard<std::mutex> lock(m_portMutex);
        CloseLocked();    // a second Create() replaces the port instead of leaking it
        if (portName == nullptr || *portName == '\0') {
            std::cerr << "SerialTransport: no port name" << std::endl;
            return -1;
        }
        sp_port* port = nullptr;
        if (sp_get_port_by_name(portName, &port) != SP_OK) {
            std::cerr << "SerialTransport: Could not find port " << portName << std::endl;
            return -1;
        }
        if (sp_open(port, SP_MODE_READ_WRITE) != SP_OK) {
            std::cerr << "SerialTransport: Could not open port " << portName << std::endl;
            sp_free_port(port);
            return -1;
        }
        if (sp_set_baudrate(port, baudRate) != SP_OK || sp_set_bits(port, 8) != SP_OK ||
            sp_set_parity(port, SP_PARITY_NONE) != SP_OK || sp_set_stopbits(port, 1) != SP_OK ||
            sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE) != SP_OK) {
            std::cerr << "SerialTransport: Could not set " << portName << " to " << baudRate
                      << " 8N1 without flow control" << std::endl;
            sp_close(port);
            sp_free_port(port);
            return -1;
        }
        m_port = port;
        SetStream(this);
        return 0;
    }

    /// Close the port. Waits for a Send() or Receive() in progress on another
    /// thread (at most about a second: reads time out), so the port is never
    /// freed under it.
    void Close()
    {
        std::lock_guard<std::mutex> lock(m_portMutex);
        CloseLocked();
    }

private:
    // Under m_portMutex: detach (waits for Send()/Receive()), then free the port
    void CloseLocked()
    {
        SetStream(nullptr);
        if (m_port) {
            sp_close(m_port);
            sp_free_port(m_port);
            m_port = nullptr;
        }
    }

    // IByteStream. Only called between SetStream(this) and SetStream(nullptr),
    // so m_port is open.
    bool Write(const uint8_t* data, size_t len) override
    {
        const int written = sp_blocking_write(m_port, data, len, 1000);
        return written == static_cast<int>(len);
    }

    int Read(uint8_t* buf, size_t len, dmq::Duration timeout) override
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        // sp_blocking_read_next(): returns as soon as any bytes arrive. Its
        // timeout 0 means "forever", so no wait at all takes the non-blocking read.
        const int n = (ms <= 0) ? static_cast<int>(sp_nonblocking_read(m_port, buf, len))
                                : static_cast<int>(sp_blocking_read_next(m_port, buf, len,
                                      static_cast<unsigned int>(ms > 0x7FFFFFFF ? 0x7FFFFFFF : ms)));
        return n < 0 ? -1 : n;
    }

    sp_port* m_port = nullptr;
    std::mutex m_portMutex;     ///< Create() and Close(): m_port's lifetime
};

} // namespace dmq::transport

#endif
