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
/// * Calculates and verifies 16-bit CRC for every packet.
/// * Handles reliable delivery via `TransportMonitor` (ACKs/Retries).

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_uart.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransport.h"
#include "port/transport/ITransportMonitor.h"
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

        // 1. Prepare Packet Structure
        dmq::xstring payload = os.str();
        DmqHeader headerCopy = header;
        headerCopy.SetLength((uint16_t)payload.length());

        uint8_t packet[DmqHeader::HEADER_SIZE];
        auto to_net = [](uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); };
        
        // Serialize Header (Network Byte Order)
        uint16_t val;
        val = to_net(headerCopy.GetMarker()); memcpy(&packet[0], &val, 2);
        val = to_net(headerCopy.GetId());     memcpy(&packet[2], &val, 2);
        val = to_net(headerCopy.GetSeqNum()); memcpy(&packet[4], &val, 2);
        val = to_net(headerCopy.GetLength()); memcpy(&packet[6], &val, 2);

        // 2. Send Header
        if (HAL_UART_Transmit(m_huart, packet, DmqHeader::HEADER_SIZE, 100) != HAL_OK) {
            xSemaphoreGiveRecursive(m_mutex); return -1;
        }

        // 3. Send Payload
        if (payload.length() > 0) {
            if (HAL_UART_Transmit(m_huart, (uint8_t*)payload.data(), (uint16_t)payload.length(), 500) != HAL_OK) {
                xSemaphoreGiveRecursive(m_mutex); return -1;
            }
        }

        // 4. Calculate and Send CRC (Required for PC communication)
        uint16_t crc = dmq::util::Crc16CalcBlock(packet, DmqHeader::HEADER_SIZE, 0xFFFF);
        if (payload.length() > 0) crc = dmq::util::Crc16CalcBlock((uint8_t*)payload.data(), payload.length(), crc);

        if (HAL_UART_Transmit(m_huart, (uint8_t*)&crc, 2, 100) != HAL_OK) {
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
        uint8_t b = 0;
        uint8_t markerHigh = (uint8_t)(DmqHeader::MARKER >> 8); 

        // 1. Sync Loop
        // Reads from Ring Buffer. If empty, sleeps efficiently on Semaphore.
        while (true) {
            if (!ReadByteBlocked(b)) return -1;
            if (b == markerHigh) { headerBuf[0] = b; break; }
        }
        
        // 2. Read Rest of Header
        for (int i = 1; i < (int)DmqHeader::HEADER_SIZE; i++) {
            if (!ReadByteBlocked(headerBuf[i])) return -1;
        }

        // Deserialize Header
        auto from_net = [](uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); };
        uint16_t val;
        memcpy(&val, &headerBuf[0], 2); header.SetMarker(from_net(val));
        memcpy(&val, &headerBuf[2], 2); header.SetId(from_net(val));
        memcpy(&val, &headerBuf[4], 2); header.SetSeqNum(from_net(val));
        memcpy(&val, &headerBuf[6], 2); header.SetLength(from_net(val));

        if (header.GetMarker() != DmqHeader::MARKER) return -1;

        // 3. Read Payload
        uint16_t len = header.GetLength();
        if (len > 0) {
            if (len > BUFFER_SIZE) return -1; 
            for (int i = 0; i < len; i++) {
                if (!ReadByteBlocked(m_tempRxBuffer[i])) return -1;
            }
            is.clear(); is.str(""); is.write((char*)m_tempRxBuffer, len);
        }

        // 4. Read & Verify CRC
        uint16_t receivedCrc;
        uint8_t* pCrc = (uint8_t*)&receivedCrc;
        if (!ReadByteBlocked(pCrc[0])) return -1;
        if (!ReadByteBlocked(pCrc[1])) return -1;

        uint16_t calcCrc = dmq::util::Crc16CalcBlock(headerBuf, DmqHeader::HEADER_SIZE, 0xFFFF);
        if (len > 0) calcCrc = dmq::util::Crc16CalcBlock(m_tempRxBuffer, len, calcCrc);
        if (receivedCrc != calcCrc) return -1;

        // 5. Handle ACKs
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_transportMonitor && m_sendTransport) {
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

        // 1. Push data to thread-safe buffer
        m_ringBuffer.Put(m_rxByte);

        // 2. Wake up the Receive() task immediately
        xSemaphoreGiveFromISR(m_rxSemaphore, &xHigherPriorityTaskWoken);

        // 3. Re-arm interrupt for the next byte
        HAL_UART_Receive_IT(m_huart, &m_rxByte, 1);

        // 4. Context Switch if necessary
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

private:
    /// @brief Read a byte from the Ring Buffer. Blocks if empty.
    bool ReadByteBlocked(uint8_t& out) {
        while (true) {
            // Check buffer
            if (m_ringBuffer.Get(out)) return true;
            
            // If empty, Sleep until ISR wakes us up.
            // This prevents CPU starvation loop.
            if (xSemaphoreTake(m_rxSemaphore, portMAX_DELAY) != pdTRUE) return false;
        }
    }

    UART_HandleTypeDef* m_huart;
    SemaphoreHandle_t m_mutex = nullptr;       // Mutex for Send()
    SemaphoreHandle_t m_rxSemaphore = nullptr; // Semaphore for Receive() blocking
    
    uint8_t m_rxByte; // Temp byte for HAL ISR
    UartRingBuffer<1024> m_ringBuffer; // 1KB RX Buffer
    
    static const int BUFFER_SIZE = 512;
    uint8_t m_tempRxBuffer[BUFFER_SIZE]; // Payload reassembly buffer

    ITransport* m_sendTransport;
    ITransport* m_recvTransport;
    ITransportMonitor* m_transportMonitor = nullptr;
};

} // namespace dmq::transport

#endif // STM32_UART_TRANSPORT_H
