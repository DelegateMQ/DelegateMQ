#ifndef PORTABLE_TESTS_H
#define PORTABLE_TESTS_H

/// @file PortableTests.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief Tests that use only DelegateMQ's portable APIs (dmq::os::Thread,
/// dmq::Mutex, dmq::ThisThread, dmq::Clock; no std::thread, std::mutex or
/// std::chrono clocks), so one copy runs on every thread port. The OS port
/// samples (posix-linux, qt-linux, freertos-linux, threadx-linux, zephyr-linux,
/// cmsis-rtos2-linux, nuttx-sim) build these files and call them after their
/// own demo tests; each sample keeps only its startup code and OS glue.
///
/// | File | Runs |
/// |---|---|
/// | DelegateThreadsPortableTests.cpp | cross-thread dispatch (free, member, shared_ptr member, std::function) and FullPolicy, then StartGatePortableTest |
/// | TimerDelegatePortableTests.cpp | PacedDispatch, TimerDelegate, Timer::ProcessTimers() draining |
/// | StartGatePortableTest.h | dmq::os::StartGate (also run by the desktop unit tests) |
///
/// Requirements on the sample:
/// - Call from a thread the port knows (an RTOS task, not a raw host thread),
///   after the kernel/scheduler is running.
/// - Something drives dmq::util::Timer::ProcessTimers() periodically (every
///   sample does, for its own timer test), since TimerDelegateTests() relies
///   on it.
/// - A failed check faults through DMQ_ASSERT_TRUE.

#include "DelegateMQ.h"

namespace PortableTests {

/// What differs between ports. The defaults suit the desktop ports.
struct Options
{
    /// Lowers a test's consumer thread below the priority of the thread running
    /// the tests, before it's created. On a preemptive RTOS, posting to an
    /// equal- or higher-priority thread switches to it at once, so a test that
    /// checks the publisher never stalls (FullPolicy::DROP) needs a
    /// lower-priority consumer. Priority values and their direction differ per
    /// RTOS, so the sample supplies this. nullptr: no change (desktop ports,
    /// where posting doesn't preempt the caller).
    void (*lowerPriority)(dmq::os::Thread& thread) = nullptr;

    /// false: never keep two test threads alive at once. Only for a port whose
    /// kernel can't run that (ThreadX's Linux/GNU simulation port, see
    /// threadx-linux/main_delegate.cpp); skips the tests that need it.
    bool concurrentThreads = true;
};

/// Cross-thread dispatch, FullPolicy and StartGate tests.
void DelegateThreadsTests(const Options& options = {});

/// PacedDispatch, TimerDelegate and Timer tests.
void TimerDelegateTests(const Options& options = {});

} // namespace PortableTests

#endif // PORTABLE_TESTS_H
