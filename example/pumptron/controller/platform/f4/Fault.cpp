/**
 * @file Fault.cpp
 * @brief Pumptron F4 replacement for DelegateMQ's port/fault/Fault.cpp.
 *
 * The library's bare-metal FaultHandler prints and then spins silently, so a
 * board that faults without a debugger attached simply freezes. This version
 * makes faults visible on the board itself:
 *
 *   - Red LED fast blink (~5 Hz) : DelegateMQ fault (DMQ_ASSERT / BAD_ALLOC)
 *   - Red LED slow blink (~1 Hz) : thread watchdog expired
 *   - Red LED solid              : Error_Handler / FreeRTOS hook (main.cpp)
 *
 * The message goes straight to SWV ITM, bypassing printf and its FreeRTOS
 * mutex (the fault may have happened while that mutex was held). Interrupts
 * are disabled, so the blink uses a busy-wait rather than any OS delay.
 *
 * Selected by filtering port/fault/Fault.cpp out of the DelegateMQ sources in
 * platform/f4/CMakeLists.txt (the library's documented override mechanism).
 */

#include "extras/util/Fault.h"
#include "stm32f4xx_hal.h"
#include "stm32f4_discovery.h"

namespace {

void ItmWrite(const char* s)
{
    while (s && *s)
        ITM_SendChar(static_cast<uint32_t>(*s++));
}

void ItmWriteUnsigned(unsigned value)
{
    char buf[12];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    do { buf[--i] = static_cast<char>('0' + value % 10); value /= 10; } while (value && i > 0);
    ItmWrite(&buf[i]);
}

void BusyWaitMs(uint32_t ms)
{
    // Roughly calibrated for 168 MHz; interrupts are off, so no SysTick/HAL_Delay.
    for (volatile uint32_t i = 0; i < ms * 24000u; ++i) {}
}

[[noreturn]] void HaltBlinking(uint32_t halfPeriodMs)
{
    BSP_LED_Init(LED3); BSP_LED_Init(LED4); BSP_LED_Init(LED5); BSP_LED_Init(LED6);
    BSP_LED_Off(LED3);  BSP_LED_Off(LED4);  BSP_LED_Off(LED6);
    for (;;) {
        BSP_LED_Toggle(LED5);
        BusyWaitMs(halfPeriodMs);
    }
}

} // namespace

namespace dmq::util {

DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    __disable_irq();
    ItmWrite("\r\n*** DelegateMQ FAULT: ");
    ItmWrite(file);
    ItmWrite(" line ");
    ItmWriteUnsigned(line);
    ItmWrite(" ***\r\n");
    HaltBlinking(100);
}

void InstallCrashHandlers() {}

} // namespace dmq::util

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    dmq::util::FaultHandler(file, line);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    __disable_irq();
    ItmWrite("\r\n*** WATCHDOG EXPIRED: ");
    ItmWrite(threadName);
    ItmWrite(" ***\r\n");
    HaltBlinking(500);
}

extern "C" void InstallCrashHandlers()
{
    dmq::util::InstallCrashHandlers();
}
