#ifndef STM32_UART_TRANSPORT_H
#define STM32_UART_TRANSPORT_H

/// @file Stm32UartTransport.h
/// @brief STM32 HAL UART transport for DelegateMQ, on any RTOS port.
///
/// @details
/// An adapter: the frame, the locking, the ACKs and the timeouts are
/// FramedStreamTransport's (port/transport/framed-stream), shared with every
/// other stream link. This file only moves bytes through the STM32 HAL:
/// - **Receive:** the UART receive interrupt hands each byte to an
///   IsrByteStream ring (HAL_UART_RxCpltCallback() calls OnRxCplt()), and
///   Receive() sleeps on a semaphore until bytes arrive, so an idle link costs
///   nothing. A full ring drops the byte and counts it (GetRxOverflowCount()).
/// - **Send:** HAL_UART_Transmit(), blocking, at most DMQ_UART_TX_TIMEOUT_MS
///   per call.
///
/// Receive() returns RECEIVE_NO_DATA after DMQ_UART_RECV_TIMEOUT_MS with no
/// frame (SetRecvTimeout()). Sizes and timeouts: DMQ_UART_* in
/// DelegateMQConfig.h. Frame errors are counted and reported
/// (SetRecvErrorHandler()).
///
/// Wiring, in the application:
/// @code
/// dmq::transport::Stm32UartTransport* g_uartTransportInstance = nullptr;
/// extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* h)
///     { if (g_uartTransportInstance) g_uartTransportInstance->OnRxCplt(); }
/// extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* h)
///     { if (g_uartTransportInstance) g_uartTransportInstance->OnRxError(); }
/// @endcode
/// (check `h` against the transport's UART if other UARTs use these callbacks).

// STM32 HAL for the chip family: the first family HAL on the include path, or
// the header named by DMQ_STM32_HAL_HEADER (e.g. -DDMQ_STM32_HAL_HEADER=\"stm32g4xx_hal.h\").
// The UART calls used here are the same across the families' HALs.
#if defined(DMQ_STM32_HAL_HEADER)
    #include DMQ_STM32_HAL_HEADER
#elif __has_include("stm32h7xx_hal.h")
    #include "stm32h7xx_hal.h"
#elif __has_include("stm32f4xx_hal.h")
    #include "stm32f4xx_hal.h"
#elif __has_include("stm32f7xx_hal.h")
    #include "stm32f7xx_hal.h"
#elif __has_include("stm32g4xx_hal.h")
    #include "stm32g4xx_hal.h"
#elif __has_include("stm32l4xx_hal.h")
    #include "stm32l4xx_hal.h"
#else
    #error "Stm32UartTransport: no STM32 HAL found; define DMQ_STM32_HAL_HEADER"
#endif
#include "delegate/DelegateOpt.h"
#include "port/transport/framed-stream/FramedStreamTransport.h"
#include "port/transport/framed-stream/IsrByteStream.h"
#include <cstddef>
#include <cstdint>

namespace dmq::transport {
class Stm32UartTransport;
}

// The active transport, for the C HAL callbacks to reach it (see above).
// Defined by the application.
extern dmq::transport::Stm32UartTransport* g_uartTransportInstance;

namespace dmq::transport {

class Stm32UartTransport : public FramedStreamTransport<dmq::UART_MAX_PAYLOAD>, private IByteStream
{
public:
    Stm32UartTransport() = default;
    explicit Stm32UartTransport(UART_HandleTypeDef* huart) : m_huart(huart) {}

    ~Stm32UartTransport() { Close(); }

    /// @brief Start interrupt-driven reception on a UART the HAL has
    /// initialized (HAL_UART_Init()), and hook the HAL callbacks to this.
    /// @param huart the UART handle (e.g. &huart6)
    /// @return 0, or -1 (no handle, or reception didn't start)
    int Create(UART_HandleTypeDef* huart)
    {
        Close();    // a second Create() stops the first session before starting another
        if (huart == nullptr)
            return -1;
        m_huart = huart;

        // Startup noise may have left error flags set
        __HAL_UART_CLEAR_OREFLAG(m_huart);
        __HAL_UART_CLEAR_NEFLAG(m_huart);
        __HAL_UART_CLEAR_FEFLAG(m_huart);

        m_rx.Clear();   // reception is off: no producer running
        g_uartTransportInstance = this;
        // One byte at a time: OnRxCplt() re-arms for the next
        if (HAL_UART_Receive_IT(m_huart, &m_rxByte, 1) != HAL_OK) {
            g_uartTransportInstance = nullptr;   // nothing hooked on failure
            m_huart = nullptr;
            return -1;
        }
        m_receiving = true;
        SetStream(this);
        return 0;
    }

    /// @brief Stop: Send() and Receive() fail from here on, and reception
    /// stops. Waits for a Send() or Receive() in progress. Does nothing to a
    /// UART this transport never started (a handle given only to the constructor).
    void Close()
    {
        SetStream(nullptr);
        if (m_receiving && m_huart)
            HAL_UART_AbortReceive_IT(m_huart);
        m_receiving = false;
        if (g_uartTransportInstance == this)
            g_uartTransportInstance = nullptr;
    }

    /// @brief From HAL_UART_RxCpltCallback(): a byte arrived. Interrupt context.
    void OnRxCplt()
    {
        if (!m_huart)
            return;
        m_rx.PutFromIsr(m_rxByte);   // wakes Receive()
        HAL_UART_Receive_IT(m_huart, &m_rxByte, 1);
    }

    /// @brief From HAL_UART_ErrorCallback(). Interrupt context.
    /// @details A blocking UART error (e.g. overrun) makes the HAL abort the
    /// interrupt-driven receive, so no further bytes would ever arrive. Clear
    /// the error flags and re-arm single-byte reception. For non-blocking
    /// errors (noise/framing/parity) the HAL keeps receiving, RxState stays
    /// busy, and re-arming is skipped. A frame the error corrupted fails its
    /// CRC in Receive().
    void OnRxError()
    {
        if (!m_huart)
            return;
        __HAL_UART_CLEAR_OREFLAG(m_huart);
        __HAL_UART_CLEAR_NEFLAG(m_huart);
        __HAL_UART_CLEAR_FEFLAG(m_huart);
        if (m_huart->RxState == HAL_UART_STATE_READY)
            HAL_UART_Receive_IT(m_huart, &m_rxByte, 1);
    }

    /// @brief Received bytes dropped because the receive ring was full.
    /// Non-zero means DMQ_UART_RX_RING_SIZE is too small for the traffic, or
    /// Receive() isn't called often enough.
    uint32_t GetRxOverflowCount() const { return m_rx.GetOverflowCount(); }

private:
    // IByteStream
    bool Write(const uint8_t* data, size_t len) override
    {
        const uint32_t timeout = static_cast<uint32_t>(dmq::UART_TX_TIMEOUT.count());
        while (len > 0) {
            const uint16_t chunk = static_cast<uint16_t>(len > 0xFFFF ? 0xFFFF : len);
            if (HAL_UART_Transmit(m_huart, const_cast<uint8_t*>(data), chunk, timeout) != HAL_OK)
                return false;
            data += chunk;
            len -= chunk;
        }
        return true;
    }

    int Read(uint8_t* buf, size_t len, dmq::Duration timeout) override
    {
        return m_rx.Read(buf, len, timeout);
    }

    UART_HandleTypeDef* m_huart = nullptr;
    bool m_receiving = false;  ///< Create() started reception; Close() stops it
    uint8_t m_rxByte = 0;      ///< The HAL's one-byte receive target
    IsrByteStream<> m_rx;      ///< Interrupt -> Receive()
};

} // namespace dmq::transport

#endif // STM32_UART_TRANSPORT_H
