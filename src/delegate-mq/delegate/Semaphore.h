#ifndef _DELEGATE_SEMAPHORE_H
#define _DELEGATE_SEMAPHORE_H

/// @file
/// @brief Delegate library semaphore wrapper class.
///
/// dmq::Semaphore is available (DMQ_HAS_SEMAPHORE) on every port except
/// bare metal. On stdlib/Win32/Qt, FreeRTOS, and ThreadX (DMQ_HAS_CV --
/// these have a real dmq::ConditionVariable) it's the generic class
/// defined below, built from dmq::ConditionVariable + dmq::Mutex. On
/// Zephyr and CMSIS-RTOS2 (no ConditionVariable port) DelegateOpt.h
/// already resolves dmq::Semaphore to a port-native implementation
/// instead (ZephyrSemaphore / CmsisRtos2Semaphore) -- nothing more to
/// define here for those two.

#include "DelegateOpt.h"

#ifdef DMQ_HAS_SEMAPHORE

// Fix compiler error on Windows
#undef max

#ifdef DMQ_HAS_CV

namespace dmq {

/// @brief A semaphore wrapper class. 
class Semaphore
{
public:
	Semaphore() = default;
	~Semaphore() = default;

	/// Called to wait on a semaphore to be signaled.
	/// @param[in] timeout - semaphore timeout 
	/// @return Return true if semaphore signaled, false if timeout occurred. 
	bool Wait(Duration timeout)
	{
        dmq::UniqueLock<dmq::Mutex> lk(m_lock);
        if (timeout == Duration::max())
        {
            m_sema.wait(lk, [this] { return m_signaled; });
        }
        else
        {
            if (!m_sema.wait_for(lk, timeout, [this] { return m_signaled; }))
            {
                return false; // Timeout occurred
            }
        }

        if (m_signaled)
        {
            m_signaled = false;
            return true;
        }
        else
        {
            return false;
        }
	}

	/// Called to signal a semaphore.
    void Signal()
    {
        {
            dmq::UniqueLock<dmq::Mutex> lk(m_lock);
            m_signaled = true;
        }
        m_sema.notify_one();
    }

private:
	// Prevent copying objects
	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;

	dmq::ConditionVariable m_sema;
	dmq::Mutex m_lock;
	bool m_signaled = false;
};

}

#endif // DMQ_HAS_CV

#endif // DMQ_HAS_SEMAPHORE

#endif
