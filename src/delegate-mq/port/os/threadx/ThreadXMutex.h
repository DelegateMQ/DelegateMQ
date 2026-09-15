#ifndef THREADX_MUTEX_H
#define THREADX_MUTEX_H

#include "tx_api.h"

namespace dmq::os {

    // =========================================================================
    // ThreadXMutex 
    // Wraps TX_MUTEX.
    // =========================================================================
    class ThreadXMutex {
    public:
        ThreadXMutex() {
            // Cast string literal to (CHAR*) for strict compliance
            UINT status = tx_mutex_create(&m_mutex, (CHAR*)"DMQ_Mutex", TX_INHERIT);
            DMQ_ASSERT_TRUE(status == TX_SUCCESS);
        }

        ~ThreadXMutex() {
            tx_mutex_delete(&m_mutex);
        }

        void lock() {
            tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);
        }

        // REQUIRED for std::unique_lock compatibility
        bool try_lock() {
            return tx_mutex_get(&m_mutex, TX_NO_WAIT) == TX_SUCCESS;
        }

        void unlock() {
            tx_mutex_put(&m_mutex);
        }

        ThreadXMutex(const ThreadXMutex&) = delete;
        ThreadXMutex& operator=(const ThreadXMutex&) = delete;

    private:
        TX_MUTEX m_mutex;
    };

    // ThreadX Mutexes are recursive by default, so this alias is valid.
    using ThreadXRecursiveMutex = ThreadXMutex;
} // namespace dmq::os

#endif // THREADX_MUTEX_H