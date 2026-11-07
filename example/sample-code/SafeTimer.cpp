/// @brief Implement the safe timer test using RAII ScopedConnections.
/// This prevents latent callbacks on dead objects without manual Stop() calls.
/// 
/// See Object Lifetime and Async Delegates in DETAILS.md for more information.

#include "SafeTimer.h"
#include "DelegateMQ.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

namespace Example
{
    /// @brief Demonstrates safe asynchronous callback handling using RAII and shared ownership.
    /// 
    /// @details
    /// This class uses `std::enable_shared_from_this` to ensure the instance remains alive 
    /// if a callback is currently executing or queued. It also utilizes `dmq::ScopedConnection` 
    /// to automatically unsubscribe from the timer upon destruction, preventing "use-after-free" 
    /// errors if the object is deleted while the timer is still active.
    class SafeTimer : public std::enable_shared_from_this<SafeTimer>
    {
    public:
        SafeTimer() : m_thread("SafeTimer Thread") {}

        void Init() {
            m_thread.CreateThread();

            // Bind the callback using Connect().
            // We use MakeTimerDelegate with shared_from_this() to ensure:
            // 1. Object lifetime safety (via weak pointer capture)
            // 2. Queue flood prevention (at most one message in the queue)
            // We store the connection in m_timerConn so it automatically disconnects on destruction.
            m_timerConn = m_timer.OnExpired.Connect(
                MakeTimerDelegate(shared_from_this(), &SafeTimer::OnTimer, m_thread)
            );

            m_timer.Start(std::chrono::milliseconds(100));
        }

        // Unregister in Term to be clean
        void Term() {
            m_timer.Stop();

            // Drain the thread before disconnecting: ExitThread() runs or finishes any
            // OnTimer() tick already queued or in progress, so none is left running
            // on this object after Term() returns. (Disconnect() alone cancels ticks
            // still waiting in the queue, but not one already running, nor one a
            // ProcessTimers() emit queues while Disconnect() runs.)
            m_thread.ExitThread();

            m_timerConn.Disconnect();
        }

        ~SafeTimer() {
            // m_timerConn destructor runs here.
            // It guarantees the timer signal is disconnected immediately.
        }

    private:
        void OnTimer() {
            std::cout << "Timer fired safely!" << std::endl;
        }

        Timer m_timer;
        Thread m_thread;

        // RAII Connection Handle
        // When this object dies, the subscription to the timer is automatically removed.
        dmq::ScopedConnection m_timerConn;
    };

    void SafeTimerExample() {
        auto safeTimer = xmake_shared<SafeTimer>();
        safeTimer->Init();

        this_thread::sleep_for(chrono::milliseconds(500));

        safeTimer->Term();

        // When safeTimer goes out of scope here, everything cleans up automatically.
    }
}