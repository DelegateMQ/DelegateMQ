#ifndef _DELEGATE_SEMAPHORE_H
#define _DELEGATE_SEMAPHORE_H

/// @file
/// @brief Delegate library semaphore wrapper class.
///
/// dmq::Semaphore is available (DMQ_HAS_SEMAPHORE) on every port except
/// bare metal. On stdlib/Win32/Qt/POSIX (DMQ_HAS_CV -- a real
/// dmq::ConditionVariable) it's the generic class defined below, built from
/// dmq::ConditionVariable + dmq::Mutex. On every RTOS port (FreeRTOS,
/// ThreadX, Zephyr, CMSIS-RTOS2, NuttX) DelegateOpt.h resolves dmq::Semaphore
/// to a port-native implementation instead (FreeRTOSSemaphore,
/// ThreadXSemaphore, ZephyrSemaphore, CmsisRtos2Semaphore, NuttXSemaphore) --
/// nothing more to define here for those. FreeRTOS and ThreadX also have a
/// ConditionVariable, so they mark their native semaphore with
/// DMQ_HAS_NATIVE_SEMAPHORE.

#include "DelegateOpt.h"

#ifdef DMQ_HAS_SEMAPHORE

// Fix compiler error on Windows
#undef max

#if defined(DMQ_HAS_CV) && !defined(DMQ_HAS_NATIVE_SEMAPHORE)

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

#endif // DMQ_HAS_CV && !DMQ_HAS_NATIVE_SEMAPHORE

#endif // DMQ_HAS_SEMAPHORE

#endif
