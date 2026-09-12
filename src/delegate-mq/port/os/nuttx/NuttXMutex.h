#ifndef NUTTX_MUTEX_H
#define NUTTX_MUTEX_H

/// @file NuttXMutex.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Mutex / dmq::RecursiveMutex backing for NuttX, using its
/// native POSIX pthread_mutex_t directly (NuttX implements the real POSIX
/// threading API, unlike FreeRTOS/ThreadX/Zephyr/CMSIS-RTOS2, which each
/// need a bespoke kernel-object wrapper here).
///
/// @note UNVERIFIED: written against documented NuttX POSIX API behavior.
/// No NuttX toolchain/simulator is available in this development
/// environment to build and run it.

#include <pthread.h>

namespace dmq::os {

    // =========================================================================
    // NuttXMutex (Non-Recursive)
    // =========================================================================
    class NuttXMutex {
    public:
        NuttXMutex() {
            pthread_mutex_init(&m_mutex, nullptr);
        }

        ~NuttXMutex() {
            pthread_mutex_destroy(&m_mutex);
        }

        void lock() {
            pthread_mutex_lock(&m_mutex);
        }

        // REQUIRED for std::unique_lock compatibility
        bool try_lock() {
            return pthread_mutex_trylock(&m_mutex) == 0;
        }

        void unlock() {
            pthread_mutex_unlock(&m_mutex);
        }

        NuttXMutex(const NuttXMutex&) = delete;
        NuttXMutex& operator=(const NuttXMutex&) = delete;

    private:
        pthread_mutex_t m_mutex;
    };

    // =========================================================================
    // NuttXRecursiveMutex (Recursive)
    // =========================================================================
    class NuttXRecursiveMutex {
    public:
        NuttXRecursiveMutex() {
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&m_mutex, &attr);
            pthread_mutexattr_destroy(&attr);
        }

        ~NuttXRecursiveMutex() {
            pthread_mutex_destroy(&m_mutex);
        }

        void lock() {
            pthread_mutex_lock(&m_mutex);
        }

        bool try_lock() {
            return pthread_mutex_trylock(&m_mutex) == 0;
        }

        void unlock() {
            pthread_mutex_unlock(&m_mutex);
        }

        NuttXRecursiveMutex(const NuttXRecursiveMutex&) = delete;
        NuttXRecursiveMutex& operator=(const NuttXRecursiveMutex&) = delete;

    private:
        pthread_mutex_t m_mutex;
    };

} // namespace dmq::os

#endif // NUTTX_MUTEX_H
