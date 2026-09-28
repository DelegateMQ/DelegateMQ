/// @file
/// @brief Reply-to-caller: an async service delivers each completion on the
/// thread that made the request, found with dmq::ThisThread::GetCurrent().
///
/// @details Callers pass only a callback, never a thread. The service captures
/// the calling IThread when a request is made and later dispatches the callback
/// back onto it, so a UI thread's callbacks run on the UI thread, a network
/// thread's on the network thread, and so on, with no locking in the callbacks.
/// This is the pattern behind .NET's SynchronizationContext.

#include "DelegateMQ.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>

using namespace dmq;
using namespace dmq::os;
using namespace std;

namespace Example
{
    // -------------------------------------------------------------------------
    // Service: does its work on its own thread. Its async API takes no thread.
    // -------------------------------------------------------------------------
    class PrimeService
    {
    public:
        using Callback = std::function<void(int n, bool isPrime)>;

        PrimeService() : m_thread("PrimeService") { m_thread.CreateThread(); }
        ~PrimeService() { m_thread.ExitThread(); }

        /// Test `n` for primality on the service thread. Callable from any thread.
        /// `done` runs on the calling thread if it is an IThread worker; otherwise
        /// (e.g. called from main) it runs on the service thread.
        /// @pre An IThread caller must outlive the reply (a production service
        /// would guard this, e.g. with a weak_ptr or by cancelling on shutdown).
        void IsPrimeAsync(int n, Callback done)
        {
            // Captured now, on the caller's thread; used later, on the service thread.
            IThread* caller = ThisThread::GetCurrent();

            MakeDelegate([n, done, caller]() {
                const bool result = IsPrime(n);
                if (caller)
                    MakeDelegate(done, *caller)(n, result);  // Back to the caller's thread
                else
                    done(n, result);                         // No caller thread: run here
            }, m_thread)();
        }

        IThread& GetThread() { return m_thread; }

    private:
        static bool IsPrime(int n)
        {
            if (n < 2)
                return false;
            for (int i = 2; i * i <= n; i++)
                if (n % i == 0)
                    return false;
            return true;
        }

        Thread m_thread;
    };

    // -------------------------------------------------------------------------
    // Example
    // -------------------------------------------------------------------------
    void ReplyToCallerExample()
    {
        Thread uiThread("UiThread");
        Thread netThread("NetThread");
        uiThread.CreateThread();
        netThread.CreateThread();

        PrimeService service;
        std::atomic<int> pending{ 0 };
        std::atomic<int> wrongThread{ 0 };

        // Names for printing where each callback ran
        auto nameOf = [&](IThread* t) -> std::string {
            if (t == &uiThread) return "UiThread";
            if (t == &netThread) return "NetThread";
            if (t == &service.GetThread()) return "PrimeService";
            return t ? "other" : "none (main)";
        };

        // Each client asks from its own thread, then checks where the reply arrives
        auto request = [&](int n) {
            IThread* requester = ThisThread::GetCurrent();
            pending++;
            service.IsPrimeAsync(n, [&, requester](int value, bool isPrime) {
                IThread* here = ThisThread::GetCurrent();

                // Built first, then written once, so lines from different threads don't interleave
                const std::string line = "[ReplyToCaller] " + std::to_string(value) +
                    (isPrime ? " is prime" : " is not prime") +
                    " - requested on " + nameOf(requester) + ", reply on " + nameOf(here) + "\n";
                cout << line << flush;

                // An IThread caller gets its reply on its own thread; main's
                // reply runs on the service thread.
                IThread* expected = requester ? requester : &service.GetThread();
                if (here != expected)
                    wrongThread++;
                pending--;
            });
        };

        MakeDelegate([&]() { request(97); request(100); }, uiThread)();
        MakeDelegate([&]() { request(7919); }, netThread)();
        request(121);  // From main: not an IThread worker

        // Wait for all replies
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (pending > 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        if (pending == 0 && wrongThread == 0)
            cout << "[ReplyToCaller] All replies arrived on the requesting thread" << endl;
        else
            cout << "[ReplyToCaller] ERROR: " << pending << " missing, " << wrongThread << " on the wrong thread" << endl;

        uiThread.ExitThread();
        netThread.ExitThread();
    }
}
