#ifndef DMQ_ISR_DELEGATE_H
#define DMQ_ISR_DELEGATE_H

/// @file IsrDelegate.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Calls a delegate from an interrupt, safely.
///
/// A delegate can't be called directly from an ISR when what it calls isn't
/// ISR-safe, and an async delegate can't be invoked from one at all: it
/// allocates its message and takes the thread's mutex. An IsrDelegate splits
/// the call in two:
///
/// 1. Post() in the ISR only copies the arguments into a fixed ring slot,
///    under dmq::CriticalSection. No heap, no mutex, no blocking.
/// 2. IsrDelegates::ProcessAll(), called from task context (or the bare-metal
///    main loop), takes each queued post and calls the target there. With an
///    async target (MakeDelegate(obj, &Class::Func, thread)), that sends it
///    to the thread like any other async call, so the thread's FullPolicy,
///    priority lanes and statistics apply.
///
/// @code
/// // Once, in task context: the target runs on controllerThread
/// dmq::util::IsrDelegate<void(uint32_t)> onButton(
///     dmq::MakeDelegate(&controller, &Controller::OnButton, controllerThread));
///
/// // The interrupt handler
/// void EXTI0_IRQHandler() {
///     ClearInterruptFlag();
///     onButton.Post(GPIOA->IDR);
/// }
///
/// // A task, the main loop, or alongside Timer::ProcessTimers() in an RTOS task
/// dmq::util::IsrDelegates::ProcessAll();
/// @endcode
///
/// - Latency: a post waits for the next ProcessAll(). To run it at once, set
///   IsrDelegates::SetPostHandler() to an ISR-safe function that wakes the
///   task calling ProcessAll(), e.g. one that gives its semaphore with
///   xSemaphoreGiveFromISR() (FreeRTOS), tx_semaphore_put() (ThreadX),
///   k_sem_give() (Zephyr) or osSemaphoreRelease() (CMSIS-RTOS2).
/// - Measured latency, ISR to target running, on an STM32F407 (Cortex-M4,
///   168 MHz, FreeRTOS), timer interrupt at 1 kHz, DWT cycle counter:
///     post handler wakes a drain task, synchronous target:   8.5 us
///     post handler wakes a drain task, async target thread:   29 us (33 max)
///     no post handler, drain task polls every 1 ms tick:     ~0.4-1 ms
///   The async target adds a heap allocation and a second context switch.
///   For the lowest latency, make the drain task the consumer: a synchronous
///   target, run by the task the post handler wakes.
/// - A full ring drops the post: Post() returns false and GetDroppedCount()
///   counts it. Size it (N, DMQ_ISR_DELEGATE_QUEUE_SIZE by default) for the
///   most posts that can arrive between two ProcessAll() calls.
/// - Arguments are copied by value, so each must be trivially copyable (no
///   pointers to the ISR's stack, no std::string). The target returns void.
/// - With an async target, ProcessAll() can send a burst faster than the
///   thread runs it, so the thread's FullPolicy applies: give a thread fed
///   from an interrupt FullPolicy::DROP (and a dropped handler to count), not
///   FAULT (the default) or TIMEOUT (blocks ProcessAll()).
/// - Posts to one IsrDelegate are called in order. Post() works from task
///   context too.
/// - Not from an ISR: ProcessAll(), and creating or destroying an IsrDelegate.
///   Stop the interrupt before destroying the IsrDelegate it posts to. Don't
///   destroy an IsrDelegate from its own target (it faults); destroying or
///   creating another one there is fine.
/// - Works on every port: it needs only dmq::CriticalSection, which bare metal
///   (DMQ_THREAD_NONE) has too. Don't call ProcessAll() from an ISR there
///   (e.g. from the SysTick handler with Timer::ProcessTimers()): it would
///   call the targets in interrupt context again.

#include "../../delegate/DelegateOpt.h"
#include "../../delegate/UnicastDelegate.h"
#include <array>
#include <cstddef>
#include <new>
#include <tuple>
#include <type_traits>

namespace dmq::util {

/// @brief Every IsrDelegate, and the call that runs their posts.
class IsrDelegates
{
public:
    /// Task context: call the target of every queued post, oldest first for
    /// each IsrDelegate. Returns the number of posts called.
    static size_t ProcessAll()
    {
        size_t called = 0;
        dmq::LockGuard<dmq::RecursiveMutex> lock(GetListLock());
        for (Node* node = GetHead(); node != nullptr; node = node->m_next) {
            // A target that unlinks another IsrDelegate updates node->m_next,
            // so the walk stays valid; unlinking this one faults (Unlink())
            Node* const outer = GetProcessing();
            GetProcessing() = node;
            called += node->Process();
            GetProcessing() = outer;
        }
        return called;
    }

    /// Optional: called by every successful Post(), in its context (usually an
    /// ISR), to wake the task that calls ProcessAll(). Must be ISR-safe.
    /// nullptr (the default) for none.
    static void SetPostHandler(void (*handler)()) { GetPostHandler() = handler; }

    /// One IsrDelegate in the list (base of IsrDelegate<>)
    class Node
    {
    public:
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

    protected:
        Node() = default;
        virtual ~Node() = default;

        /// Add to the list. The derived class calls it last in its
        /// constructor, so ProcessAll() never sees it half built.
        void Link()
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(GetListLock());
            m_next = GetHead();
            GetHead() = this;
        }

        /// Remove from the list. The derived class calls it first in its
        /// destructor, before its members go: waits for a ProcessAll() calling
        /// this one on another thread.
        void Unlink()
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(GetListLock());
            // ProcessAll() on this thread is calling this one: destroyed from its own target
            DMQ_ASSERT_TRUE(GetProcessing() != this);
            for (Node** p = &GetHead(); *p != nullptr; p = &(*p)->m_next) {
                if (*p == this) {
                    *p = m_next;
                    break;
                }
            }
        }

        /// Call the target for each queued post; returns how many
        virtual size_t Process() = 0;

        static void NotifyPost()
        {
            if (auto handler = GetPostHandler())
                handler();
        }

    private:
        friend class IsrDelegates;
        Node* m_next = nullptr;
    };

private:
    // Immortal, like Timer's locks: never destroyed, so an IsrDelegate with
    // static storage may still unlink itself at shutdown
    static Node*& GetHead()
    {
        static Node* head = nullptr;
        return head;
    }

    // The IsrDelegate ProcessAll() is calling (guarded by the list lock)
    static Node*& GetProcessing()
    {
        static Node* processing = nullptr;
        return processing;
    }

    static dmq::RecursiveMutex& GetListLock()
    {
        static dmq::RecursiveMutex* lock = new(std::nothrow) dmq::RecursiveMutex();
        if (!lock)
            BAD_ALLOC();
        return *lock;
    }

    // Set during init, before the interrupts that post are enabled
    static void (*&GetPostHandler())()
    {
        static void (*handler)() = nullptr;
        return handler;
    }
};

template <class Sig, size_t N = dmq::ISR_DELEGATE_QUEUE_SIZE>
class IsrDelegate;

/// @brief A delegate whose calls are posted from an ISR and run by
/// IsrDelegates::ProcessAll() (see the file comment).
/// @tparam Args The target's parameters (copied by value into the ring)
/// @tparam N Posts it holds until ProcessAll() takes them
template <class... Args, size_t N>
class IsrDelegate<void(Args...), N> : public IsrDelegates::Node
{
    static_assert(N > 0, "IsrDelegate: the ring needs at least one slot");
    static_assert((std::is_trivially_copyable_v<std::decay_t<Args>> && ...),
        "IsrDelegate: arguments are copied in an interrupt, so each must be trivially copyable");
    static_assert((std::is_default_constructible_v<std::decay_t<Args>> && ...),
        "IsrDelegate: each argument type must be default constructible (ring slots)");

    using Slot = std::tuple<std::decay_t<Args>...>;

public:
    /// Task context. The target may be synchronous (called by ProcessAll()
    /// itself) or async (sent to its thread).
    explicit IsrDelegate(const dmq::UnicastDelegate<void(Args...)>& target)
        : m_target(target)
    {
        DMQ_ASSERT_TRUE(!m_target.Empty());
        Link();
    }

    /// Task context, with the interrupt stopped.
    ~IsrDelegate() override { Unlink(); }

    /// From an ISR or a task: queue a call to the target.
    /// @return false if the ring was full and the post dropped.
    bool Post(Args... args)
    {
        {
            dmq::LockGuard<dmq::CriticalSection> lock(m_lock);
            if (m_count == N) {
                m_dropped = m_dropped + 1;
                return false;
            }
            m_ring[(m_head + m_count) % N] = Slot(args...);
            m_count = m_count + 1;
        }
        NotifyPost();
        return true;
    }

    /// Posts dropped because the ring was full.
    size_t GetDroppedCount() const
    {
        dmq::LockGuard<dmq::CriticalSection> lock(m_lock);
        return m_dropped;
    }

private:
    size_t Process() override
    {
        size_t called = 0;
        for (;;) {
            Slot slot;
            {
                // Only taking the slot is in the critical section, not the call
                dmq::LockGuard<dmq::CriticalSection> lock(m_lock);
                if (m_count == 0)
                    return called;
                slot = m_ring[m_head];
                m_head = (m_head + 1) % N;
                m_count = m_count - 1;
            }
            std::apply([this](auto&... a) { m_target(a...); }, slot);
            called++;
        }
    }

    dmq::UnicastDelegate<void(Args...)> m_target;
    mutable dmq::CriticalSection m_lock;    // guards the ring and m_dropped: taken in ISR and task context
    std::array<Slot, N> m_ring{};
    size_t m_head = 0;
    size_t m_count = 0;
    size_t m_dropped = 0;
};

} // namespace dmq::util

#endif // DMQ_ISR_DELEGATE_H
