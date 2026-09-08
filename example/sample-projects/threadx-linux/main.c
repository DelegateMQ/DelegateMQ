/// @file main.c
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Entry point for the ThreadX Linux simulation build.
///
/// ThreadX's `tx_kernel_enter()` performs low-level initialization and then
/// calls `tx_application_define()` once, before starting the scheduler. All
/// initial ThreadX object creation (threads, timers, ...) happens there --
/// the ThreadX equivalent of FreeRTOS's "create tasks, then start scheduler"
/// idiom used by the freertos-bare-metal sample.

#include "tx_api.h"
#include <stdio.h>

/// Defined in main_delegate.cpp
extern void main_delegate(void* first_unused_memory);

void tx_application_define(void* first_unused_memory)
{
    main_delegate(first_unused_memory);
}

int main(void)
{
    printf("--- Starting ThreadX Kernel (Linux simulation) ---\n");

    /* Never returns. */
    tx_kernel_enter();

    return 0;
}
