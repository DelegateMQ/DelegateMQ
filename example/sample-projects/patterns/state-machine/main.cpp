// patterns/state-machine
//
// active-fsm's StateMachine (https://github.com/DelegateMQ/active-fsm) and
// DelegateMQ together: an automatic door controller (Door.h) runs as an
// active object on its own thread.
//
//  1. Events are called from any thread; the transition map posts them to the
//     door's thread, so the state machine needs no mutex.
//  2. dmq::util::Timer drives the timed transitions (travel time, auto-close);
//     their callbacks are queued to the door's thread too.
//  3. A control panel on another thread observes OnTransition: the signal
//     queues each transition to the panel's thread.
//  4. The timer driver sleeps until the next deadline
//     (Timer::GetNextExpiry() and Timer::SetScheduleChangedHandler()).
//  5. Self-checks, including four threads sending events at once.

#include "Door.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

using namespace dmq;
using dmq::util::Timer;
using Transition = std::pair<uint8_t, uint8_t>;   // (from, to)

//----------------------------------------------------------------------------
// Timer driver: sleeps until the next timer is due, or until Start() schedules
// one that may be due sooner.
//----------------------------------------------------------------------------
static std::mutex s_timerMutex;
static std::condition_variable s_timerCv;
static bool s_timerWake = false;
static bool s_timerExit = false;

static void WakeTimerDriver()
{
    {
        std::lock_guard<std::mutex> lock(s_timerMutex);
        s_timerWake = true;
    }
    s_timerCv.notify_one();
}

static void TimerDriver()
{
    for (;;)
    {
        Timer::ProcessTimers();
        const Duration next = Timer::GetNextExpiry();

        std::unique_lock<std::mutex> lock(s_timerMutex);
        auto woken = [] { return s_timerWake || s_timerExit; };
        if (next == Duration::max())
            s_timerCv.wait(lock, woken);
        else
            s_timerCv.wait_for(lock, next, woken);
        if (s_timerExit)
            return;
        s_timerWake = false;
    }
}

//----------------------------------------------------------------------------
// Control panel: observes the door from its own thread. m_log is only touched
// on the panel's thread, so it needs no lock either.
//----------------------------------------------------------------------------
class Panel
{
public:
    explicit Panel(Door& door) : m_thread("PanelThread")
    {
        m_thread.CreateThread();

        // Each transition is queued to the panel's thread, in order
        m_conn = door.OnTransition.Connect(
            MakeDelegate(this, &Panel::OnTransition, m_thread));
    }

    ~Panel() { Shutdown(); }

    void Shutdown()
    {
        m_conn.Disconnect();
        m_thread.ExitThread();
    }

    /// Number of transitions seen so far. Safe from any thread.
    size_t Count() const { return m_count.load(); }

    /// Take the log, from any thread: the call runs on the panel's thread
    /// (after every transition already queued to it) and waits for the result.
    std::vector<Transition> TakeLog()
    {
        return MakeDelegate(this, &Panel::TakeLogImpl, m_thread, WAIT_INFINITE)();
    }

    void SetVerbose(bool verbose)
    {
        MakeDelegate(this, &Panel::SetVerboseImpl, m_thread, WAIT_INFINITE)(verbose);
    }

private:
    void OnTransition(uint8_t from, uint8_t to)
    {
        if (m_verbose)
            printf("  [panel] %s -> %s\n", Door::StateName(from), Door::StateName(to));
        m_log.emplace_back(from, to);
        m_count++;
    }

    std::vector<Transition> TakeLogImpl()
    {
        std::vector<Transition> log;
        log.swap(m_log);
        m_count = 0;
        return log;
    }

    void SetVerboseImpl(bool verbose) { m_verbose = verbose; }

    dmq::os::Thread m_thread;
    ScopedConnection m_conn;
    std::vector<Transition> m_log;
    std::atomic<size_t> m_count{0};
    bool m_verbose = true;
};

//----------------------------------------------------------------------------
// Checks
//----------------------------------------------------------------------------
static int s_failures = 0;

static void Check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        s_failures++;
}

template <typename Pred>
static bool WaitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

static bool IsLegal(const Transition& t)
{
    static const Transition legal[] = {
        {Door::ST_CLOSED,  Door::ST_OPENING},   // Open
        {Door::ST_OPENING, Door::ST_OPEN},      // travel time
        {Door::ST_OPEN,    Door::ST_CLOSING},   // Close or auto-close
        {Door::ST_CLOSING, Door::ST_CLOSED},    // travel time
        {Door::ST_CLOSING, Door::ST_OPENING},   // Open or Obstacle
    };
    return std::find(std::begin(legal), std::end(legal), t) != std::end(legal);
}

int main()
{
    Timer::SetScheduleChangedHandler(&WakeTimerDriver);
    std::thread timerDriver(TimerDriver);

    {
        Door door;
        Panel panel(door);

        // -------------------------------------------------------------------
        printf("\nTest 1: Open, then the door closes on its own\n");
        door.Open();
        Check(WaitFor([&] { return panel.Count() >= 4; }), "four transitions");
        const std::vector<Transition> expected1 = {
            {Door::ST_CLOSED,  Door::ST_OPENING},
            {Door::ST_OPENING, Door::ST_OPEN},
            {Door::ST_OPEN,    Door::ST_CLOSING},
            {Door::ST_CLOSING, Door::ST_CLOSED},
        };
        Check(panel.TakeLog() == expected1, "open, auto-close, closed");

        // -------------------------------------------------------------------
        printf("\nTest 2: An obstacle while closing reverses the door\n");
        door.Open();
        Check(WaitFor([&] { return door.GetCurrentState() == Door::ST_OPEN; }), "door open");
        door.Close();
        door.Obstacle();     // queued behind Close(): handled in ST_CLOSING
        Check(WaitFor([&] { return panel.Count() >= 7; }), "seven transitions");
        const std::vector<Transition> expected2 = {
            {Door::ST_CLOSED,  Door::ST_OPENING},
            {Door::ST_OPENING, Door::ST_OPEN},
            {Door::ST_OPEN,    Door::ST_CLOSING},   // Close()
            {Door::ST_CLOSING, Door::ST_OPENING},   // Obstacle()
            {Door::ST_OPENING, Door::ST_OPEN},
            {Door::ST_OPEN,    Door::ST_CLOSING},   // auto-close
            {Door::ST_CLOSING, Door::ST_CLOSED},
        };
        Check(panel.TakeLog() == expected2, "reversed, reopened, auto-closed");

        // -------------------------------------------------------------------
        printf("\nTest 3: Four threads send events at once\n");
        panel.SetVerbose(false);
        // Send for a fixed time, several open/close cycles long, so the door
        // moves through its states while the events arrive
        const auto burstEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        std::atomic<int> sent{0};
        std::vector<std::thread> senders;
        for (int i = 0; i < 4; i++)
        {
            senders.emplace_back([&door, &sent, burstEnd, i] {
                std::mt19937 rng(static_cast<unsigned>(i + 1));
                for (int n = 1; std::chrono::steady_clock::now() < burstEnd; n++)
                {
                    switch (rng() % 3)
                    {
                    case 0: door.Open(); break;
                    case 1: door.Close(); break;
                    default: door.Obstacle(); break;
                    }
                    sent++;
                    if (n % 8 == 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1 + rng() % 4));
                }
            });
        }
        for (auto& t : senders)
            t.join();
        printf("  %d events sent\n", sent.load());

        // Events may still be queued to the door. Once they are handled, the
        // door must close on its own and stay closed: wait for it to stay
        // CLOSED longer than a full open/close cycle takes.
        const auto STABLE = std::chrono::milliseconds(300);
        auto closedSince = std::chrono::steady_clock::now();
        Check(WaitFor([&] {
            const auto now = std::chrono::steady_clock::now();
            if (door.GetCurrentState() != Door::ST_CLOSED)
                closedSince = now;
            return now - closedSince >= STABLE; }, std::chrono::milliseconds(5000)),
            "door closed after the burst");
        const std::vector<Transition> log = panel.TakeLog();

        // The panel saw one unbroken chain of legal transitions, CLOSED to CLOSED:
        // every event was handled on the door's thread, one at a time.
        bool chained = !log.empty() && log.front().first == Door::ST_CLOSED
            && log.back().second == Door::ST_CLOSED;
        bool legal = true;
        for (size_t i = 0; i < log.size(); i++)
        {
            legal = legal && IsLegal(log[i]);
            if (i > 0)
                chained = chained && log[i].first == log[i - 1].second;
        }
        printf("  %zu transitions\n", log.size());
        Check(chained, "transitions form one chain");
        Check(legal, "every transition is legal");

        // Stop the threads that call into door and panel before they go away
        door.Shutdown();
        panel.Shutdown();
    }

    {
        std::lock_guard<std::mutex> lock(s_timerMutex);
        s_timerExit = true;
    }
    s_timerCv.notify_one();
    timerDriver.join();
    Timer::SetScheduleChangedHandler(nullptr);

    if (s_failures == 0)
    {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(S) FAILED\n", s_failures);
    return 1;
}
