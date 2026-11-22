/// @file main.c
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Starts the ThreadX kernel (Linux simulation); the application is
/// set up in tx_application_define(), see main_delegate.cpp.

#include "tx_api.h"
#include <stdio.h>

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
