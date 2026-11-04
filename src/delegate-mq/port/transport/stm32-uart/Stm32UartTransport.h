#ifndef STM32_UART_TRANSPORT_H
#define STM32_UART_TRANSPORT_H

/// @file Stm32UartTransport.h
/// @brief STM32 HAL UART transport implementation for DelegateMQ (FreeRTOS).
///
/// @details
/// This class provides a robust, thread-safe transport layer for serial communication
/// on STM32 microcontrollers running FreeRTOS.
///
/// **Key Architecture: Interrupt-Driven Ring Buffer**
/// Instead of polling the UART (which wastes CPU) or using blocking HAL calls
/// (which starve other FreeRTOS tasks), this implementation uses:
/// 1. **UART RX Interrupt:** Fires immediately when a byte arrives, pushing it into a `UartRingBuffer`.
/// 2. **Binary Semaphore:** The `Receive()` task sleeps on this semaphore. It is only woken up
///    by the ISR when data is actually available.
/// 3. **Recursive Mutex:** Protects the `Send()` function to ensure atomic packet transmission
///    even if multiple threads try to send simultaneously.
///
/// **Data Integrity:**
/// * Implements strict `0xAA 0x55` framing synchronization.
/// * Calculates and verifies 16-bit CRC for every packet (sent big-endian, after
///   the payload).
/// * Sizes and timeouts come from DMQ_UART_* in DelegateMQConfig.h.
/// * Handles reliable delivery via `TransportMonitor` (ACKs/Retries).

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_uart.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/ITransportMonitor.h"
#include "extras/util/crc16.h"

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <cstring>
#include <atomic>

namespace dmq::transport {

// ----------------------------------------------------------------------------
// Forward Declarations & Global Hook
// ----------------------------------------------------------------------------
class Stm32UartTransport;

} // namespace dmq::transport

// Global pointer to the active transport instance.
// This is defined in main.cpp and used by the C-style HAL Interrupt Callback
// to route the hardware interrupt back into the C++ class.
extern dmq::transport::Stm32UartTransport* g_uartTransportInstance;

namespace dmq::transport {

// ============================================================================
// UartRingBuffer
// ============================================================================
/// @brief A lightweight, thread-safe (Single Producer / Single Consumer) Ring Buffer.
/// @tparam Size The size of the buffer in bytes. Must be large enough to hold
///              bursts of data (e.g., 1024 bytes) to prevent overrun.
template <size_t Size>
class UartRingBuffer {
public:
    /// Push a byte into the buffer (Called from ISR).
    /// @return false if the buffer was full and the byte was dropped.
    bool Put(uint8_t data) {
        size_t next_head = (m_head + 1) % Size;
        if (next_head == m_tail) return false; // Buffer Full
        m_buffer[m_head] = data;
        m_head = next_head;
        return true;
    }

    /// Pop a byte from the buffer (Called from Consumer Task).
    bool Get(uint8_t& data) {
        if (m_head == m_tail) return false; // Buffer Empty
        data = m_buffer[m_tail];
        m_tail = (m_tail + 1) % Size;
        return true;
    }

    void Clear() { m_head = m_tail = 0; }

private:
    volatile uint8_t m_buffer[Size];
    volatile size_t m_head = 0;
    volatile size_t m_tail = 0;
};

// ============================================================================
// Stm32UartTransport Class
// ============================================================================
class Stm32UartTransport : public ITransport
{
public:
    Stm32UartTransport() : m_huart(nullptr), m_sendTransport(this), m_recvTransport(this) {}
    Stm32UartTransport(UART_HandleTypeDef* huart) : m_huart(huart), m_sendTransport(this), m_recvTransport(this) {}

    ~Stm32UartTransport() {
        if (m_mutex) vSemaphoreDelete(m_mutex);
        if (m_rxSemaphore) vSemaphoreDelete(m_rxSemaphore);
        g_uartTransportInstance = nullptr;
    }

    /// @brief Initialize the Transport and enable UART Interrupts.
    /// @param huart Pointer to the HAL UART handle (e.g., &huart6).
    /// @return 0 on success, -1 on failure.
    int Create(UART_HandleTypeDef* huart) {
        m_huart = huart;
        g_uartTransportInstance = this; // Hook global pointer for ISR

        // Create OS primitives
        if (!m_mutex) m_mutex = xSemaphoreCreateRecursiveMutex();
        if (!m_rxSemaphore) m_rxSemaphore = xSemaphoreCreateBinary();

        if (!m_huart || !m_mutex || !m_rxSemaphore) return -1;

        // Clear potential hardware error flags from startup noise
        __HAL_UART_CLEAR_OREFLAG(m_huart);
        __HAL_UART_CLEAR_NEFLAG(m_huart);
        __HAL_UART_CLEAR_FEFLAG(m_huart);

        // START RECEIVING: Enable RX Interrupt for the first byte.
        // This kicks off the chain; subsequent bytes are re-armed in OnRxCplt.
        if (HAL_UART_Receive_IT(m_huart, &m_rxByte, 1) != HAL_OK) return -1;

        return 0;
    }

    virtual void Close() { 
        // Optional: Disable UART interrupts here if destroying the transport.
    }

    // ------------------------------------------------------------------------
    // Send() Implementation
    // ------------------------------------------------------------------------
    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override {
        if (os.bad() || os.fail()) return -1;
        if (!m_huart || !m_mutex) return -1;

        // CRITICAL SECTION: Protect UART TX hardware
        if (xSemaphoreTakeRecursive(m_mutex, portMAX_DELAY) != pdTRUE) return -1;

        const uint32_t txTimeout = static_cast<uint32_t>(dmq::UART_TX_TIMEOUT.count());

        // 1. Prepare Packet Structure
        dmq::xstring payload = os.str();
        if (payload.length() > BUFFER_SIZE) {  // receiver's reassembly limit
            xSemaphoreGiveRecursive(m_mutex);
            return -1;
        }
        DmqHeader headerCopy = header;
        headerCopy.SetLength((uint16_t)payload.length());

        // Serialize Header (Network Byte Order)
        uint8_t packet[DmqHeader::HEADER_SIZE];
        headerCopy.Encode(packet);

        // 2. Send Header
        if (HAL_UART_Transmit(m_huart, packet, DmqHeader::HEADER_SIZE, txTimeout) != HAL_OK) {
            xSemaphoreGiveRecursive(m_mutex); return -1;
        }

        // 3. Send Payload
        if (payload.length() > 0) {
            if (HAL_UART_Transmit(m_huart, (uint8_t*)payload.data(), (uint16_t)payload.length(), txTimeout) != HAL_OK) {
                xSemaphoreGiveRecursive(m_mutex); return -1;
            }
        }

        // 4. Calculate and Send CRC (Required for PC communication)
        uint16_t crc = dmq::util::Crc16CalcBlock(packet, DmqHeader::HEADER_SIZE, 0xFFFF);
        if (payload.length() > 0) crc = dmq::util::Crc16CalcBlock((uint8_t*)payload.data(), payload.length(), crc);

        const uint8_t crcBytes[2] = { static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc) };  // big-endian
        if (HAL_UART_Transmit(m_huart, const_cast<uint8_t*>(crcBytes), 2, txTimeout) != HAL_OK) {
            xSemaphoreGiveRecursive(m_mutex); return -1;
        }

        xSemaphoreGiveRecursive(m_mutex);
        return 0;
    }

    // ------------------------------------------------------------------------
    // Receive() Implementation
    // ------------------------------------------------------------------------
    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override {
        if (m_recvTransport != this) return -1;

        uint8_t headerBuf[DmqHeader::HEADER_SIZE];
        const uint8_t markerHigh = (uint8_t)(DmqHeader::MARKER >> 8);
        const uint8_t markerLow  = (uint8_t)(DmqHeader::MARKER & 0xFF);
        const TickType_t frameTimeout = pdMS_TO_TICKS(dmq::UART_FRAME_TIMEOUT.count());

        // 1. Sync Loop: both marker bytes, so a stray 0xAA in other data doesn't
        // start a frame. Waits indefinitely (sleeping on the semaphore while the
        // ring buffer is empty) until a 0xAA arrives; only the byte after it
        // must come within the frame timeout. Stray bytes followed by an idle
        // line are discarded, not reported as an error.
        uint8_t prev = 0;
        while (true) {
            uint8_t b = 0;
            const TickType_t wait = (prev == markerHigh) ? frameTimeout : portMAX_DELAY;
            if (!ReadByte(b, wait)) {
                if (prev == markerHigh) { prev = 0; continue; }  // lone 0xAA: keep syncing
                return -1;
            }
            if (prev == markerHigh && b == markerLow) break;
            prev = b;
        }
        headerBuf[0] = markerHigh;
        headerBuf[1] = markerLow;

        // 2. Read Rest of Header. Each later byte must arrive within the frame
        // timeout: a frame cut short by lost bytes is dropped instead of
        // swallowing the start of the next one.
        for (int i = 2; i < (int)DmqHeader::HEADER_SIZE; i++) {
            if (!ReadByte(headerBuf[i], frameTimeout)) return -1;
        }

        // Deserialize Header
        header.Decode(headerBuf);

        if (!header.IsValid()) return -1;

        // 3. Read Payload
        uint16_t len = header.GetLength();
        if (len > 0) {
            if (len > BUFFER_SIZE) return -1; 
            for (int i = 0; i < len; i++) {
                if (!ReadByte(m_tempRxBuffer[i], frameTimeout)) return -1;
            }
            is.clear(); is.str(""); is.write((char*)m_tempRxBuffer, len);
        }

        // 4. Read & Verify CRC (big-endian)
        uint8_t crcBytes[2];
        if (!ReadByte(crcBytes[0], frameTimeout)) return -1;
        if (!ReadByte(crcBytes[1], frameTimeout)) return -1;
        const uint16_t receivedCrc = static_cast<uint16_t>((crcBytes[0] << 8) | crcBytes[1]);

        uint16_t calcCrc = dmq::util::Crc16CalcBlock(headerBuf, DmqHeader::HEADER_SIZE, 0xFFFF);
        if (len > 0) calcCrc = dmq::util::Crc16CalcBlock(m_tempRxBuffer, len, calcCrc);
        if (receivedCrc != calcCrc) return -1;

        // 5. Handle ACKs
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        // ACK whenever a send path exists, whether or not this transport also
        // tracks its own sends: a receive-only transport (e.g. NetworkNode's SUB)
        // has no monitor but must still ACK the sender's RELIABLE messages.
        else if (m_sendTransport) {
             // Auto-Reply with ACK
             dmq::xostringstream ss;
             DmqHeader ack;
             ack.SetId(dmq::ACK_REMOTE_ID);
             ack.SetSeqNum(header.GetSeqNum());
             m_sendTransport->Send(ss, ack);
        }
        return 0;
    }

    // ------------------------------------------------------------------------
    // Interrupt Handler
    // ------------------------------------------------------------------------
    /// @brief Called by HAL_UART_RxCpltCallback when a byte arrives.
    void OnRxCplt() {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // 1. Push data to thread-safe buffer. A full buffer drops the byte (the
        // frame then fails its CRC); count it so the app can see it happening.
        if (!m_ringBuffer.Put(m_rxByte))
            m_rxOverflows.fetch_add(1, std::memory_order_relaxed);

        // 2. Wake up the Receive() task immediately
        xSemaphoreGiveFromISR(m_rxSemaphore, &xHigherPriorityTaskWoken);

        // 3. Re-arm interrupt for the next byte
        HAL_UART_Receive_IT(m_huart, &m_rxByte, 1);

        // 4. Context Switch if necessary
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    /// @brief Called by HAL_UART_ErrorCallback. ISR-safe: no OS calls.
    /// @details A blocking UART error (e.g. overrun) makes the HAL abort the
    /// interrupt-driven receive, so no further bytes would ever arrive. Clear
    /// the error flags and re-arm single-byte reception. For non-blocking
    /// errors (noise/framing/parity) the HAL keeps receiving, RxState stays
    /// busy, and re-arming is skipped. Any frame the error corrupted is
    /// rejected by the CRC check in Receive().
    void OnRxError() {
        if (!m_huart) return;

        __HAL_UART_CLEAR_OREFLAG(m_huart);
        __HAL_UART_CLEAR_NEFLAG(m_huart);
        __HAL_UART_CLEAR_FEFLAG(m_huart);

        if (m_huart->RxState == HAL_UART_STATE_READY)
            HAL_UART_Receive_IT(m_huart, &m_rxByte, 1);
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

    /// @brief Received bytes dropped because the RX ring buffer was full.
    /// Non-zero means DMQ_UART_RX_RING_SIZE is too small for the traffic, or
    /// Receive() isn't called often enough.
    uint32_t GetRxOverflowCount() const { return m_rxOverflows.load(std::memory_order_relaxed); }

private:
    /// @brief Read a byte from the Ring Buffer, sleeping on the RX semaphore
    /// while it's empty.
    /// @param timeout Most to wait for each wake-up (portMAX_DELAY = forever).
    /// @return false if no byte arrived within the timeout.
    bool ReadByte(uint8_t& out, TickType_t timeout) {
        while (true) {
            if (m_ringBuffer.Get(out)) return true;
            if (xSemaphoreTake(m_rxSemaphore, timeout) != pdTRUE) return false;
        }
    }

    UART_HandleTypeDef* m_huart;
    SemaphoreHandle_t m_mutex = nullptr;       // Mutex for Send()
    SemaphoreHandle_t m_rxSemaphore = nullptr; // Semaphore for Receive() blocking
    
    uint8_t m_rxByte; // Temp byte for HAL ISR
    UartRingBuffer<dmq::UART_RX_RING_SIZE> m_ringBuffer;
    std::atomic<uint32_t> m_rxOverflows{ 0 };

    static constexpr size_t BUFFER_SIZE = dmq::UART_MAX_PAYLOAD;
    uint8_t m_tempRxBuffer[BUFFER_SIZE]; // Payload reassembly buffer

    ITransport* m_sendTransport;
    ITransport* m_recvTransport;
    ITransportMonitor* m_transportMonitor = nullptr;
};

} // namespace dmq::transport

#endif // STM32_UART_TRANSPORT_H
