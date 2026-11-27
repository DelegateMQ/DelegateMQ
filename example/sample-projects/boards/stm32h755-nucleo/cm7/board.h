/// @file board.h
/// NUCLEO-H755ZI-Q, Cortex-M7: LEDs and the console UART. The console is
/// USART3 (PD8 TX, PD9 RX), wired to the ST-LINK's virtual COM port.

#ifndef H755_CM7_BOARD_H
#define H755_CM7_BOARD_H

#include "stm32h7xx_hal.h"
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Console baud rate on the ST-LINK virtual COM port
#ifndef BOARD_CONSOLE_BAUD
#define BOARD_CONSOLE_BAUD 115200
#endif

/// The Cortex-M7's LEDs: LD1 green (PB0) and LD3 red (PB14). LD2 (yellow,
/// PE1) belongs to the Cortex-M4.
typedef enum { LED_GREEN, LED_RED } BoardLed;

/// LEDs and the console UART. After the clocks are set.
void BoardInit(void);

void BoardLedSet(BoardLed led, int on);
void BoardLedToggle(BoardLed led);

/// Console output, used by printf (syscalls.c). Whole calls don't interleave
/// between tasks; from an interrupt, or with interrupts off, it writes directly.
int BoardWrite(const char* buf, int len);

/// Unrecoverable error before the console works: red LED, halt.
void BoardFatal(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif // H755_CM7_BOARD_H
