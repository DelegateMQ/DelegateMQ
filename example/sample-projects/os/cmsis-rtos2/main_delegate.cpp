/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on CMSIS-RTOS2 -- specifically
/// Zephyr's own CMSIS-RTOS v2 compatibility layer (CONFIG_CMSIS_RTOS_V2,
/// subsys/portability/cmsis_rtos_v2/), built for the native_sim simulation
/// board. This is a real, non-stub CMSIS-RTOS2 implementation (Zephyr's own
/// samples/tests run it on native_sim in CI), so this sample exercises
/// dmq::os::CmsisRtos2Thread/Mutex/Semaphore/DelegateQueue against genuine
/// osThreadNew/osMutexAcquire/osSemaphoreAcquire/osMessageQueuePut calls --
/// not the ZephyrThread port (see os/zephyr for that).
///
/// Identical test suite to os/zephyr/os/freertos/os/threadx -- same
/// delegate, Signal, ScopedConnection, Thread, and Timer usage -- to show
/// that the application code is identical across RTOS ports; only DMQ_THREAD
/// changes. Uses only CMSIS-RTOS2 API calls (osDelay, osTimerNew/osTimerStart,
/// osThreadGetName/osThreadGetId) rather than Zephyr's native kernel API, to
/// keep this genuinely a CMSIS-RTOS2 application rather than a Zephyr one
/// that merely links the compatibility layer in.
///
/// NOTE: this build still goes through `west build` / Zephyr's own main()
/// entry, since CONFIG_CMSIS_RTOS_V2 is a Zephyr kernel subsystem layered on
/// top of the Zephyr scheduler -- there is no truly OS-agnostic way to boot
/// a CMSIS-RTOS2 application outside of some concrete RTOS.

#include "DelegateMQ.h"
#include "cmsis_os2.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <atomic>
#include <type_traits>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// The portable tests every OS port sample runs (test/portable/PortableTests.h)
#include "PortableTests.h"

// Test consumer threads run below this sample's main thread (osPriorityNormal;
// CMSIS-RTOS2: higher value = higher priority), so posting never preempts it.
static PortableTests::Options TestOptions()
{
    PortableTests::Options options;
    options.lowerPriority = [](dmq::os::Thread& t) { t.SetThreadPriority(osPriorityLow); };
    // native_sim runs threads on host stacks; a CPU target (mps2/an385 on
    // QEMU) runs them on the stacks the kernel reports
#if !defined(CONFIG_ARCH_POSIX)
    options.realStacks = true;
#else
    // native_sim's time advances only while the CPU idles
    options.highResClock = false;
#endif
    return options;
}

// --------------------------------------------------------------------------
// CMSIS-RTOS2 CONFIGURATION & HELPERS
// --------------------------------------------------------------------------
// The osTimer callback runs in ISR context here (Zephyr's k_timer underneath),
// and Timer::ProcessTimers() takes OS mutexes, so the callback only signals;
// TimerThreadEntry() does the processing.
static osSemaphoreId_t g_timerTick;

static void SystemTimerHandler(void* /*argument*/) {
    osSemaphoreRelease(g_timerTick);
}

// Highest-priority application thread: processes all delegate-based timers.
static void TimerThreadEntry(void* /*argument*/) {
    while (true) {
        osSemaphoreAcquire(g_timerTick, osWaitForever);
        Timer::ProcessTimers();
    }
}

#if !defined(CONFIG_ARCH_POSIX)
// --------------------------------------------------------------------------
// HARDWARE ISR TEST HELPERS (Test 11, Cortex-M boards only)
// --------------------------------------------------------------------------
// On a Cortex-M board an osTimer callback runs in the kernel's SysTick
// interrupt (Zephyr's k_timer expiry), a genuine ISR. From there it checks
// dmq::CriticalSection (CmsisRtos2CriticalSection: PRIMASK through the real
// CMSIS-Core intrinsics) directly, and calls Timer::Start()/Stop(), which take
// Timer::GetLock(), while a thread does the same and TimerThreadEntry() runs
// ProcessTimers().
static_assert(std::is_same_v<dmq::CriticalSection, dmq::os::CmsisRtos2CriticalSection>,
              "CMSIS-RTOS2 port: dmq::CriticalSection is CmsisRtos2CriticalSection");

extern "C" DMQ_NORETURN void QemuExit(int status);   // cortex_m_qemu.cpp

// Re-arm the 5 ms one-shot every 17 ms: ProcessTimers() runs every 10 ms, so
// each expiry is seen (within 15 ms) before the next re-arm. Not a multiple of
// 10: re-arming on ProcessTimers()'s own tick would restart it every time.
static constexpr uint32_t ISR_REARM_EVERY = 17;
static Timer* s_isrOneShot = nullptr;
static Timer* s_isrChurn = nullptr;
static volatile uint32_t s_isrCount = 0;
static volatile uint32_t s_isrNotInIsr = 0;           // callbacks not in an interrupt (IPSR == 0)
static volatile uint32_t s_isrLockBad = 0;            // CheckCriticalSection() failures in the ISR
static volatile uint32_t s_isrKeyNotZero = 0;         // current-thread key not 0 in the ISR
static volatile uint32_t s_isrInCmsisThread = 0;      // interrupts that landed on a CMSIS-RTOS2 thread
static std::atomic<int> s_isrOneShotFired{0};

static void OnIsrOneShot() { s_isrOneShotFired++; }

// Run on a dmq::os::Thread (a CMSIS-RTOS2 thread). Zephyr's main thread isn't
// one: osThreadGetId() returns NULL there, so it can't show the key.
static bool CurrentThreadKeySet() { return dmq::os::CmsisRtos2CurrentThreadKey() != 0; }

// Keeps a CMSIS-RTOS2 thread running so the timer interrupt lands on it
static void Spin(uint32_t ms) {
    const uint32_t end = osKernelGetTickCount() + ms;
    while (osKernelGetTickCount() < end) {
    }
}

// True if dmq::CriticalSection masks interrupts (PRIMASK = 1) while held,
// nested two deep, and puts back the caller's PRIMASK when released
static bool CheckCriticalSection() {
    const uint32_t before = __get_PRIMASK();
    dmq::CriticalSection outer;
    dmq::CriticalSection inner;
    outer.lock();
    bool ok = __get_PRIMASK() == 1;
    inner.lock();
    ok = ok && __get_PRIMASK() == 1;
    inner.unlock();
    ok = ok && __get_PRIMASK() == 1;    // inner restored outer's masked state
    outer.unlock();
    return ok && __get_PRIMASK() == before;
}

static void IsrTimerCallback(void* /*argument*/) {
    s_isrCount = s_isrCount + 1;
    if (__get_IPSR() == 0)
        s_isrNotInIsr = s_isrNotInIsr + 1;
    if (!CheckCriticalSection())
        s_isrLockBad = s_isrLockBad + 1;
    // In an interrupt the current-thread key is 0, so ThisThread::GetCurrent()
    // returns nullptr. osThreadGetId() here is the interrupted thread: non-NULL
    // when that is a CMSIS-RTOS2 thread, which the key must not report.
    if (osThreadGetId() != nullptr)
        s_isrInCmsisThread = s_isrInCmsisThread + 1;
    if (dmq::os::CmsisRtos2CurrentThreadKey() != 0)
        s_isrKeyNotZero = s_isrKeyNotZero + 1;
    if (s_isrCount % ISR_REARM_EVERY == 0)
        s_isrOneShot->Start(std::chrono::milliseconds(5), true);
    if (s_isrCount % 2)
        s_isrChurn->Start(std::chrono::milliseconds(1000));
    else
        s_isrChurn->Stop();
}
#endif

static const char* CurrentThreadName() {
    const char* name = osThreadGetName(osThreadGetId());
    return (name && name[0]) ? name : "unnamed";
}

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction: %d (Thread: %s)\n", val, CurrentThreadName());
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc: %d (Thread: %s, Instance: %p)\n", val, CurrentThreadName(), (void*)this);
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired! (Thread: %s)\n", CurrentThreadName());
    }
};

// --------------------------------------------------------------------------
// TEST LOGIC
// --------------------------------------------------------------------------
void ExecuteAllTests() {
    setvbuf(stdout, NULL, _IONBF, 0);
    osDelay(100);

    printf("\n=========================================\n");
    printf("   CMSIS-RTOS2 DELEGATE SYSTEM ONLINE     \n");
    printf("=========================================\n");

    // --- TEST 1: Unicast Delegate ---
    printf("\n[Test 1] Unicast Delegate (Free Function):\n");
    UnicastDelegate<void(int)> unicast;
    unicast = MakeDelegate(FreeFunction);
    unicast(100);

    // --- TEST 2: Lambda Support ---
    printf("\n[Test 2] Unicast Delegate (Lambda):\n");
    int capture_value = 42;
    unicast = MakeDelegate(std::function<void(int)>([capture_value](int val) {
        printf("  [Callback] Lambda called! Capture: %d, Arg: %d\n", capture_value, val);
        }));
    unicast(200);

    // --- TEST 3: Multicast Delegate ---
    printf("\n[Test 3] Multicast Delegate (Broadcast):\n");
    MulticastDelegate<void(int)> multicast;
    TestHandler handler;

    multicast += MakeDelegate(FreeFunction);
    multicast += MakeDelegate(&handler, &TestHandler::MemberFunc);

    multicast += MakeDelegate(std::function<void(int)>([](int val) {
        printf("  [Callback] Multicast Lambda called! Val: %d\n", val);
        }));

    printf("Firing all 3 targets...\n");
    multicast(300);

    // --- TEST 4: Removal ---
    printf("\n[Test 4] Removing a Delegate:\n");
    multicast -= MakeDelegate(FreeFunction);
    printf("Firing remaining targets (Expected: 2)...\n");
    multicast(400);

    // --- TEST 5: Signals & Connections (RAII) ---
    printf("\n[Test 5] Signals & Scoped Connections:\n");
    Signal<void(int)> signal;

    {
        printf("  -> Creating ScopedConnection inside block...\n");
        ScopedConnection conn = signal.Connect(MakeDelegate(FreeFunction));

        printf("  -> Firing Signal (Expect Callback):\n");
        signal(500);

        printf("  -> Exiting block (ScopedConnection will destruct)...\n");
    }
    printf("  -> Firing Signal outside block (Expect NO Callback):\n");
    signal(600);

    // --- TEST 6: Thread-Safe Delegates ---
    printf("\n[Test 6] Thread-Safe Multicast (Mutex Protected):\n");
    MulticastDelegateSafe<void(int)> safeMulticast;
    safeMulticast += MakeDelegate(FreeFunction);

    printf("Firing Thread-Safe Delegate...\n");
    safeMulticast(700);

    // --- TEST 7: Async Delegates (Cross-Thread) ---
    printf("\n[Test 7] Async Delegate (Cross-Thread Dispatch):\n");

    // Create a worker thread (Active Object)
    // NOTE: C++ Destructor ~Thread() MUST run to close the queue properly!
    Thread workerThread("WorkerThread");
    if (workerThread.CreateThread()) {

        auto asyncDelegate = MakeDelegate(FreeFunction, workerThread);

        printf("  -> Dispatching to Worker Thread (Non-Blocking)...\n");
        asyncDelegate(800);

        osDelay(100);
    }
    else {
        printf("  [Error] Failed to create WorkerThread!\n");
    }

    // --- TEST 8: Timers ---
    printf("\n[Test 8] Timer Delegate (One-Shot):\n");

    // NOTE: C++ Destructor ~Timer() MUST run to unregister from the global list!
    Timer myTimer;

    ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

    printf("  -> Starting Timer (200ms delay)...\n");
    myTimer.Start(std::chrono::milliseconds(200));

    // Wait for timer
    osDelay(300);
    myTimer.Stop();

    // --- TEST 9: Cross-Thread Dispatch & FullPolicy Stress Tests ---
    printf("\n[Test 9] Cross-Thread Dispatch & FullPolicy Tests:\n");
    PortableTests::DelegateThreadsTests(TestOptions());

    // --- TEST 10: PacedDispatch & TimerDelegate Tests ---
    printf("\n[Test 10] PacedDispatch & TimerDelegate Tests:\n");
    PortableTests::TimerDelegateTests(TestOptions());

#if !defined(CONFIG_ARCH_POSIX)
    // --- TEST 11: CmsisRtos2CriticalSection and Timer from a hardware ISR ---
    printf("\n[Test 11] CriticalSection and Timer Start/Stop from a hardware ISR:\n");
    {
        DMQ_ASSERT_TRUE(CheckCriticalSection());      // thread context
        printf("  -> thread context: PRIMASK set while held, restored after\n");

        // A CMSIS-RTOS2 thread has a current-thread key
        Thread busy("IsrBusy");
        busy.CreateThread();
        DMQ_ASSERT_TRUE(MakeDelegate(&CurrentThreadKeySet, busy, WAIT_INFINITE)());
        printf("  -> thread context: current-thread key set\n");

        Timer oneShot;
        Timer churn;
        ScopedConnection conn = oneShot.OnExpired.Connect(MakeDelegate(&OnIsrOneShot));
        s_isrOneShot = &oneShot;
        s_isrChurn = &churn;
        s_isrCount = 0;
        s_isrNotInIsr = 0;
        s_isrLockBad = 0;
        s_isrKeyNotZero = 0;
        s_isrInCmsisThread = 0;
        s_isrOneShotFired = 0;

        // Every tick (1 ms), in the SysTick interrupt
        osTimerId_t isrTimer = osTimerNew(IsrTimerCallback, osTimerPeriodic, nullptr, nullptr);
        DMQ_ASSERT_TRUE(isrTimer != nullptr);
        DMQ_ASSERT_TRUE(osTimerStart(isrTimer, 1) == osOK);

        // Keep a CMSIS-RTOS2 thread running for part of the window, so some
        // interrupts land on it rather than on Zephyr's main thread
        MakeDelegate(&Spin, busy)(200);

        // Meanwhile the same locks from thread context, contending with the ISR
        const uint32_t end = osKernelGetTickCount() + 500;
        unsigned threadOps = 0;
        unsigned threadLockBad = 0;
        while (osKernelGetTickCount() < end) {
            Timer local;
            local.Start(std::chrono::milliseconds(50));
            churn.Stop();
            local.Stop();
            if (!CheckCriticalSection())
                threadLockBad++;
            threadOps++;
            if (threadOps % 64 == 0)
                osDelay(1);
        }

        busy.ExitThread();
        osTimerStop(isrTimer);
        osTimerDelete(isrTimer);
        osDelay(50);    // let a last one-shot expire and fire
        oneShot.Stop();
        churn.Stop();

        const uint32_t isrCount = s_isrCount;
        const int fired = s_isrOneShotFired;
        printf("  -> %lu interrupts (%lu on a CMSIS-RTOS2 thread), %u thread-side loops, one-shot fired %d times\n",
               static_cast<unsigned long>(isrCount), static_cast<unsigned long>(s_isrInCmsisThread),
               threadOps, fired);
        DMQ_ASSERT_TRUE(isrCount >= 100);              // ~500 expected in 500 ms
        DMQ_ASSERT_TRUE(s_isrNotInIsr == 0);           // every callback ran in an interrupt
        DMQ_ASSERT_TRUE(s_isrLockBad == 0);
        // Some interrupts landed on a CMSIS-RTOS2 thread, where osThreadGetId()
        // is non-NULL; the current-thread key was still 0 in every one
        DMQ_ASSERT_TRUE(s_isrInCmsisThread > 0);
        DMQ_ASSERT_TRUE(s_isrKeyNotZero == 0);
        DMQ_ASSERT_TRUE(threadLockBad == 0);
        // Each re-arm (5 ms one-shot) normally fires before the next one, but a
        // re-arm that lands before ProcessTimers() saw the expiry restarts it
        const int rearms = static_cast<int>(isrCount / ISR_REARM_EVERY);
        DMQ_ASSERT_TRUE(fired >= rearms / 2 && fired <= rearms + 1);
        printf("  -> ISR context: PRIMASK set while held, restored after; timers fired\n");
    }
#endif

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");

    // FUNCTION RETURN:
    // This closing brace '}' forces ~Timer(), ~Thread(), etc. to execute.
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINT
// --------------------------------------------------------------------------
int main() {
    // osKernelInitialize()/osKernelStart() are unnecessary here: Zephyr's own
    // main() already runs with the kernel/scheduler up (its CMSIS-RTOS2
    // compat layer initializes at SYS_INIT time, before app main() is
    // called), unlike a bare CMSIS-RTOS2 RTX5 target where main() itself
    // must call osKernelInitialize()/osKernelStart() to boot the RTOS.

    // Periodic software timer wakes TimerThreadEntry(), which runs DelegateMQ's
    // Timer::ProcessTimers(). The timer uses the genuine CMSIS-RTOS2 osTimer API
    // (backed by Zephyr's k_timer under the compatibility layer).
    g_timerTick = osSemaphoreNew(1, 0, nullptr);
    osThreadAttr_t timerAttr = {};
    timerAttr.name = "TimerThread";
    timerAttr.stack_size = 2048;    // CONFIG_CMSIS_V2_THREAD_DYNAMIC_STACK_SIZE
    timerAttr.priority = osPriorityRealtime;
    osThreadNew(TimerThreadEntry, nullptr, &timerAttr);

    osTimerId_t systemTimer = osTimerNew(SystemTimerHandler, osTimerPeriodic, nullptr, nullptr);
    osTimerStart(systemTimer, 10); // 10ms period

    ExecuteAllTests();

#if defined(CONFIG_ARCH_POSIX)
    // native_sim runs as an ordinary Linux process -- exit() tears down
    // every thread in the process (WorkerThread, everything) in one step,
    // the same approach os/zephyr/os/freertos/os/threadx use.
    // Returning from main() normally hits a native_sim/posix-arch
    // thread-table teardown edge case instead.
    exit(0);
#else
    // Cortex-M under QEMU: end QEMU with status 0 (a failed check faults
    // before here, and cortex_m_qemu.cpp ends it with a non-zero status)
    QemuExit(0);
#endif
}
