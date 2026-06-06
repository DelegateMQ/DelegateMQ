/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Keil MDK bare-metal example targeting ARM Cortex-M4 (ARMCM4).
/// Demonstrates synchronous delegates, UnicastDelegate, MulticastDelegate,
/// and Signal with RAII ScopedConnection — no RTOS or threading required.
/// See README.md for project setup instructions.

#include "DelegateMQ.h"

// C linkage version (called from C files and by the C++ overload below)
extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
		printf("FAULT: %s line %u\r\n", file, static_cast<unsigned int>(line));
		// TODO: add UART flush, LED blink, or hardware watchdog kick as needed
		while (1);  // halt — let watchdog reset or attach debugger
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
		printf("WATCHDOG EXPIRED: %s\r\n", threadName);
		while (1);
}

// C++ overload — delegates to the C version above
namespace dmq::util {
		[[noreturn]]
		void FaultHandler(const char* file, unsigned short line)
		{
				::FaultHandler(file, line);
		}
}

// Required by BareMetalClock (increment in SysTick_Handler or equivalent ISR)
extern "C" volatile uint64_t g_ticks = 0;

// --- Test targets ---

static void FreeFunc(int val) { (void)val; }

class MyClass {
public:
    void MemberFunc(int val) { (void)val; }
    static void StaticFunc(int val) { (void)val; }
};

int main(void)
{
    MyClass obj;

    // Synchronous free function delegate
    auto d1 = dmq::MakeDelegate(&FreeFunc);
    d1(1);

    // Synchronous member function delegate
    auto d2 = dmq::MakeDelegate(&obj, &MyClass::MemberFunc);
    d2(2);

    // Synchronous static function delegate
    auto d3 = dmq::MakeDelegate(&MyClass::StaticFunc);
    d3(3);

    // UnicastDelegate — stores one delegate, invokes on operator()
    dmq::UnicastDelegate<void(int)> unicast;
    unicast = d1;
    if (unicast)
        unicast(4);
    unicast = nullptr;

    // MulticastDelegate — stores many delegates, broadcasts on operator()
    dmq::MulticastDelegate<void(int)> multicast;
    multicast += d1;
    multicast += d2;
    multicast(5);
    multicast -= d1;
    multicast -= d2;

    // Signal — RAII connection handles, NullMutex on bare metal (no-op locking)
    dmq::Signal<void(int)> signal;
    {
        auto conn = signal.Connect(d1);
        signal(6);
    } // conn goes out of scope, auto-disconnects

    return 0;
}
