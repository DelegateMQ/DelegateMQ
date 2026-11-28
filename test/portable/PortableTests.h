#ifndef PORTABLE_TESTS_H
#define PORTABLE_TESTS_H

/// @file PortableTests.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief Tests that use only DelegateMQ's portable APIs (dmq::os::Thread,
/// dmq::Mutex, dmq::ThisThread, dmq::Clock; no std::thread, std::mutex or
/// std::chrono clocks), so one copy runs on every thread port. The OS port
/// samples (os/posix, os/qt, os/win32, os/freertos, os/threadx, os/zephyr,
/// os/cmsis-rtos2, os/nuttx) build these files and call them after their
/// own demo tests; each sample keeps only its startup code and OS glue.
///
/// | File | Runs |
/// |---|---|
/// | DelegateThreadsPortableTests.cpp | cross-thread dispatch (free, member, shared_ptr member, std::function) and FullPolicy, then StartGatePortableTest |
/// | TimerDelegatePortableTests.cpp | PacedDispatch, TimerDelegate, Timer::ProcessTimers() draining |
/// | StartGatePortableTest.h | dmq::os::StartGate (also run by the desktop unit tests) |
/// | StackPortableTest.h | Thread::GetStackSize() and GetStackFreeMin() (run by DelegateThreadsPortableTests.cpp) |
/// | ClockPortableTest.h | dmq::HighResClock and ThisThread::sleep_until() (run by DelegateThreadsPortableTests.cpp) |
/// | PriorityPortableTest.h | dmq::os::ThreadPriority bands and Thread::SetPriority() (run by DelegateThreadsPortableTests.cpp) |
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
    /// false: never keep two test threads alive at once. Only for a port whose
    /// kernel can't run that (ThreadX's Linux/GNU simulation port, see
    /// os/threadx/main_delegate.cpp); skips the tests that need it.
    bool concurrentThreads = true;

    /// true: the threads run on the stacks the port reports (a real CPU or
    /// QEMU), so StackPortableTest also checks that using more stack lowers
    /// GetStackFreeMin(). false on the desktop ports and the RTOS simulators,
    /// whose threads run on host stacks.
    bool realStacks = false;

    /// true: dmq::HighResClock has a sub-millisecond source and time advances
    /// while a thread spins, so ClockPortableTest checks that it resolves a
    /// span under 1 ms. false for a tick-based HighResClock (see DelegateOpt.h)
    /// and for the simulators whose time stands still while a thread spins
    /// (Zephyr native_sim, NuttX sim).
    bool highResClock = true;

    /// true: one core and a priority scheduler, so PriorityPortableTest checks
    /// that of two threads made ready together, the more urgent band runs
    /// first. false on the desktop ports and multi-core targets (ESP32), where
    /// both may run at once, and where concurrentThreads is false.
    bool priorityOrder = false;
};

/// Cross-thread dispatch, FullPolicy and StartGate tests.
void DelegateThreadsTests(const Options& options = {});

/// PacedDispatch, TimerDelegate and Timer tests.
void TimerDelegateTests(const Options& options = {});

} // namespace PortableTests

#endif // PORTABLE_TESTS_H
