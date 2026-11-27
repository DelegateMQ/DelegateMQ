/// @file transport_check.cpp
/// Builds Stm32UartTransport against the STM32H7 HAL, so a change that breaks
/// it on this family fails this build. Nothing here runs yet: the console
/// owns USART3 for now, and the PC link over UART comes later.

#include "DelegateMQ.h"
#include "port/transport/stm32-uart/Stm32UartTransport.h"

// The transport's HAL callback hook (see Stm32UartTransport.h)
dmq::transport::Stm32UartTransport* g_uartTransportInstance = nullptr;
