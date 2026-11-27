#ifndef STACK_PORTABLE_TEST_H
#define STACK_PORTABLE_TEST_H

/// @file
/// @brief Thread::GetStackSize() and GetStackFreeMin() (the stack high-water
/// mark) on any thread port, with portable APIs only. Run by
/// DelegateThreadsPortableTests.cpp on every OS port sample, and by the
/// FreeRTOS Cortex-M suite (os/freertos-bare-metal, also on the H755 board).
/// A failed check faults through DMQ_ASSERT_TRUE.
///
/// Every port: unknown before CreateThread(); the reading never exceeds the
/// size and never grows; it is still there after ExitThread(). With
/// realStacks, a thread that goes down by half its remaining free stack must
/// show the reading drop: deeper than wherever it had been, whatever its
/// startup used (a fixed amount wasn't always, on the ESP32). Not on the simulators (FreeRTOS's Win32/POSIX ports, ThreadX
/// on Linux, Zephyr native_sim, NuttX sim): their threads run on host stacks,
/// not on the stack the port reports. On the desktop ports both values are
/// dmq::STACK_UNKNOWN, which is checked too.

#include "DelegateMQ.h"
#include <cstdint>
#include <cstdio>

namespace StackPortableTest {

/// Bytes each UseStack() level puts on the stack, besides its frame
constexpr uint32_t FRAME_BYTES = 128;

inline void Nothing() {}

/// How many UseStack() levels Run() asks for (set before dispatching it)
inline uint32_t& Levels() { static uint32_t levels = 0; return levels; }

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
inline uint32_t UseStackLevels(uint32_t levels)
{
    // volatile: the compiler must keep the buffer on the stack and write it;
    // reading it after the recursive call keeps that call from being a tail call
    volatile uint8_t buf[FRAME_BYTES];
    for (uint32_t i = 0; i < FRAME_BYTES; i++)
        buf[i] = static_cast<uint8_t>(i + levels);
    const uint32_t below = levels > 1 ? UseStackLevels(levels - 1) : 0;
    return below + buf[levels % FRAME_BYTES];
}

inline void UseStack() { (void)UseStackLevels(Levels()); }

inline const char* Text(uint32_t bytes, char (&out)[12])
{
    if (bytes == dmq::STACK_UNKNOWN)
        return "unknown";
    snprintf(out, sizeof out, "%lu", static_cast<unsigned long>(bytes));
    return out;
}

/// @param realStacks the thread's code runs on the stack the port reports
/// (a real target or QEMU), so using more of it must show
inline void Run(bool realStacks)
{
    dmq::os::Thread thread("StackTest");
    const uint32_t size = thread.GetStackSize();
    DMQ_ASSERT_TRUE(size != 0);
    DMQ_ASSERT_TRUE(thread.GetStackFreeMin() == dmq::STACK_UNKNOWN);   // not created yet

    DMQ_ASSERT_TRUE(thread.CreateThread());
    dmq::MakeDelegate(&Nothing, thread, dmq::WAIT_INFINITE)();      // the dispatch path once
    const uint32_t before = thread.GetStackFreeMin();

    char a[12], b[12];
    printf("  stack size %s bytes, least free %s\n", Text(size, a), Text(before, b));

    if (before == dmq::STACK_UNKNOWN) {
        // Not tracked by this port or kernel configuration: it must stay so
        Levels() = 2;
        dmq::MakeDelegate(&UseStack, thread, dmq::WAIT_INFINITE)();
        DMQ_ASSERT_TRUE(thread.GetStackFreeMin() == dmq::STACK_UNKNOWN);
        thread.ExitThread();
        DMQ_ASSERT_TRUE(thread.GetStackFreeMin() == dmq::STACK_UNKNOWN);
        printf("  -> not tracked here\n");
        return;
    }

    if (size != dmq::STACK_UNKNOWN)
        DMQ_ASSERT_TRUE(before < size);

    // Down by about half the free stack, counting a frame's overhead generously,
    // so the other half stays as margin
    const uint32_t useBytes = before / 2;
    Levels() = useBytes / (FRAME_BYTES + 64);
    if (Levels() == 0)
        Levels() = 1;
    dmq::MakeDelegate(&UseStack, thread, dmq::WAIT_INFINITE)();
    const uint32_t after = thread.GetStackFreeMin();
    printf("  after going about %lu bytes deeper: least free %s\n",
           static_cast<unsigned long>(Levels() * FRAME_BYTES), Text(after, a));
    DMQ_ASSERT_TRUE(after <= before);       // a high-water mark never recovers
    if (realStacks)
        DMQ_ASSERT_TRUE(after < before);    // the deeper use shows

    thread.ExitThread();
    const uint32_t final = thread.GetStackFreeMin();
    printf("  after ExitThread(): least free %s\n", Text(final, a));
    DMQ_ASSERT_TRUE(final != dmq::STACK_UNKNOWN);   // the last reading is kept
    DMQ_ASSERT_TRUE(final <= after);
}

} // namespace StackPortableTest

#endif // STACK_PORTABLE_TEST_H
