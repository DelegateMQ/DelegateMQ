#ifndef _CURRENT_THREAD_STORAGE_H
#define _CURRENT_THREAD_STORAGE_H

/// @file CurrentThreadStorage.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Per-thread storage behind dmq::ThisThread::GetCurrent().
///
/// @details Each port's ThisThread struct (e.g. StdlibThisThread) derives from one
/// of these to supply GetCurrent(), which returns the dmq::IThread whose worker is
/// the calling thread. Values are written only by dmq::CurrentThreadScope (IThread.h),
/// which a worker loop creates on its own thread:
/// * ThreadLocalCurrentThread - C++ thread_local (desktop ports).
/// * RegistryCurrentThread - a fixed table keyed by the RTOS's native thread handle,
///   for RTOS ports, where C++ thread_local is not tied to the RTOS's task switching.
/// * NoCurrentThread - always nullptr (bare metal: no threads).
///
/// Included from DelegateOpt.h (via each port's ThisThread header), after the
/// DelegateMQ config and Fault.h, which provide DMQ_MAX_CURRENT_THREADS and
/// DMQ_ASSERT_TRUE.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dmq {
    class IThread;
    class CurrentThreadScope;
}

namespace dmq::os {

/// @brief Current-thread storage using C++ thread_local, for ports whose threads
/// are native OS threads (stdlib, Win32, Qt, POSIX).
struct ThreadLocalCurrentThread
{
    /// @return the IThread whose worker is the calling thread, or nullptr.
    static dmq::IThread* GetCurrent() noexcept { return Slot(); }

private:
    friend class dmq::CurrentThreadScope;

    static dmq::IThread* SetCurrent(dmq::IThread* thread) noexcept
    {
        dmq::IThread* prev = Slot();
        Slot() = thread;
        return prev;
    }

    static dmq::IThread*& Slot() noexcept
    {
        static thread_local dmq::IThread* current = nullptr;
        return current;
    }
};

/// @brief Current-thread storage for RTOS ports: a fixed table of (native thread
/// key, IThread*) slots, one per thread currently inside a CurrentThreadScope.
///
/// @details GetCurrent() is lock-free: it scans for the calling thread's key, and
/// only that thread ever writes its own slot. SetCurrent() takes Lock only to claim
/// a free slot; it is called only from thread context (CurrentThreadScope), never
/// from an ISR. No heap and no RTOS configuration (task-local storage slots,
/// custom thread data) is required.
///
/// @tparam GetKey Returns a nonzero key identifying the calling thread (typically
///   its native handle), or 0 when there is no calling thread (e.g. inside an ISR,
///   where the port can detect it); GetCurrent() then returns nullptr.
/// @tparam Lock The port's mutex type (lock()/unlock()).
/// @tparam Capacity Max threads registered at once. Exceeding it faults; raise
///   DMQ_MAX_CURRENT_THREADS (dmq::MAX_CURRENT_THREADS) for more threads. The macro
///   is used directly because this is instantiated before DelegateOpt.h defines
///   the dmq:: constant.
template <uintptr_t (*GetKey)(), typename Lock, size_t Capacity = DMQ_MAX_CURRENT_THREADS>
class RegistryCurrentThread
{
public:
    /// @return the IThread whose worker is the calling thread, or nullptr.
    static dmq::IThread* GetCurrent() noexcept
    {
        const uintptr_t key = GetKey();
        if (key == 0)
            return nullptr;
        Slot* slot = Find(key);
        return slot ? slot->thread : nullptr;
    }

private:
    friend class dmq::CurrentThreadScope;

    struct Slot
    {
        std::atomic<uintptr_t> key{ 0 };  // 0 = free
        dmq::IThread* thread = nullptr;    // written only by the thread owning the key
    };

    static dmq::IThread* SetCurrent(dmq::IThread* thread)
    {
        const uintptr_t key = GetKey();
        DMQ_ASSERT_TRUE(key != 0);  // A CurrentThreadScope needs a calling thread

        Slot* slot = Find(key);
        dmq::IThread* prev = slot ? slot->thread : nullptr;

        if (slot)
        {
            slot->thread = thread;
            if (thread == nullptr)
                slot->key.store(0, std::memory_order_release);  // Release the slot
            return prev;
        }

        if (thread == nullptr)
            return prev;

        // Claim a free slot. Other threads may be claiming too; releases need no lock.
        GetLock().lock();
        for (Slot& s : s_slots)
        {
            if (s.key.load(std::memory_order_acquire) == 0)
            {
                s.thread = thread;
                s.key.store(key, std::memory_order_release);
                GetLock().unlock();
                return prev;
            }
        }
        GetLock().unlock();

        // More threads registered at once than DMQ_MAX_CURRENT_THREADS
        DMQ_ASSERT_TRUE(false);
        return prev;
    }

    static Slot* Find(uintptr_t key) noexcept
    {
        for (Slot& s : s_slots)
        {
            if (s.key.load(std::memory_order_acquire) == key)
                return &s;
        }
        return nullptr;
    }

    static Lock& GetLock()
    {
        static Lock lock;
        return lock;
    }

    static inline Slot s_slots[Capacity];
};

/// @brief For ports without threads (bare metal): GetCurrent() always returns nullptr.
struct NoCurrentThread
{
    /// @return always nullptr.
    static dmq::IThread* GetCurrent() noexcept { return nullptr; }

private:
    friend class dmq::CurrentThreadScope;

    static dmq::IThread* SetCurrent(dmq::IThread*) noexcept { return nullptr; }
};

} // namespace dmq::os

#endif
