#ifndef NX_USER_H
#define NX_USER_H

/// @file nx_user.h
/// @brief NetX Duo options for transport/netx-udp: NetX on ThreadX's Linux
/// simulation port, 64-bit.
///
/// On x86-64, ThreadX's Linux port keeps ULONG at 32 bits, so a pointer can't
/// travel in a ULONG thread or timer argument the way NetX passes its control
/// blocks by default. These macros pass them through the ThreadX thread and
/// timer control blocks instead (tx_thread_extension_ptr,
/// tx_timer_internal_extension_ptr). Without them NetX's IP thread gets a
/// truncated NX_IP pointer and crashes.
///
/// From NetX Duo's own 64-bit test configuration,
/// netxduo/test/cmake/netxduo64/nx_user.h (Eclipse ThreadX, MIT license).

#define NX_THREAD_EXTENSION_PTR_SET(a, b)                   { \
                                                                TX_THREAD *thread_ptr; \
                                                                thread_ptr = (TX_THREAD *) (a); \
                                                                (thread_ptr -> tx_thread_extension_ptr) = (VOID *)(b); \
                                                            }
#define NX_THREAD_EXTENSION_PTR_GET(a, b, c)                { \
                                                                NX_PARAMETER_NOT_USED(c); \
                                                                TX_THREAD *thread_ptr; \
                                                                thread_ptr = tx_thread_identify(); \
                                                                while(1)\
                                                                { \
                                                                    if (thread_ptr -> tx_thread_extension_ptr) \
                                                                    { \
                                                                        (a) = (b *)(thread_ptr -> tx_thread_extension_ptr); \
                                                                        break; \
                                                                    } \
                                                                    tx_thread_sleep(1); \
                                                                } \
                                                            }
#define NX_TIMER_EXTENSION_PTR_SET(a, b)                    { \
                                                                TX_TIMER *timer_ptr; \
                                                                timer_ptr = (TX_TIMER *) (a);   \
                                                                (timer_ptr -> tx_timer_internal.tx_timer_internal_extension_ptr) = (VOID *)(b); \
                                                            }
#define NX_TIMER_EXTENSION_PTR_GET(a, b, c)                 { \
                                                                NX_PARAMETER_NOT_USED(c); \
                                                                if (!_tx_timer_expired_timer_ptr -> tx_timer_internal_extension_ptr) \
                                                                    return; \
                                                                (a) = (b *)(_tx_timer_expired_timer_ptr -> tx_timer_internal_extension_ptr); \
                                                            }

#endif
