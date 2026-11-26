/// @file ShmOsTransportTests.cpp
/// @brief Unit tests for the OS-backed shared-memory transports:
/// Win32ShmTransport (file mapping + events) on Windows, LinuxShmTransport
/// (shm_open + named semaphores) on Linux. Both sides run in this process but
/// go through the real named objects, as two processes would; the two-process
/// run is example/sample-projects/transport/shm-ring.
///
/// Covers: side B failing before side A exists; both directions; the named
/// event/semaphore waking a blocked receiver, and the receive timeout; side B
/// closing and reopening; on Linux, the last side to close removing the names.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "UnitTestCommon.h"

#if defined(_WIN32)
#include "port/transport/win32-shm/Win32ShmTransport.h"
#elif defined(__linux__)
#include "port/transport/linux-shm/LinuxShmTransport.h"
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#if defined(_WIN32) || defined(__linux__)

using namespace dmq;
using namespace dmq::transport;
using namespace std::chrono_literals;

namespace {

#if defined(_WIN32)
using OsShm = Win32ShmTransport<8, 64>;
#else
using OsShm = LinuxShmTransport<8, 64>;
#endif

/// A link name unique to this process, so parallel test runs don't collide
const char* LinkName()
{
    static char name[64];
#if defined(_WIN32)
    snprintf(name, sizeof(name), "ut-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
#else
    snprintf(name, sizeof(name), "ut-%ld", static_cast<long>(getpid()));
#endif
    return name;
}

int SendU32(ITransport& t, uint16_t seq, uint32_t value)
{
    ByteWriter os;
    os.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return t.Send(os, DmqHeader(1, seq));
}

int RecvU32(ITransport& t, uint32_t& value)
{
    DmqHeader h;
    ByteReader is;
    const int rc = t.Receive(is, h);
    if (rc != 0) return rc;
    if (is.remaining() != sizeof(value)) return -100;
    is.read(reinterpret_cast<char*>(&value), sizeof(value));
    return 0;
}

#if defined(__linux__)
bool ShmNameExists(const char* link)
{
    char path[128];
    snprintf(path, sizeof(path), "/dev/shm/dmq-shm-%s", link);
    return access(path, F_OK) == 0;
}
#endif

void ShmOs_SideBBeforeSideA_Fails()
{
    OsShm b;
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == -1);
    DMQ_ASSERT_TRUE(!b.IsOpen());
    std::cout << "ShmOs_SideBBeforeSideA_Fails() complete!" << std::endl;
}

void ShmOs_SendReceive_BothDirections()
{
    OsShm a, b;
    DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
    a.SetSendTransport(nullptr);
    b.SetSendTransport(nullptr);
    DMQ_ASSERT_TRUE(a.IsPeerOpen() && b.IsPeerOpen());

    uint32_t v = 0;
    DMQ_ASSERT_TRUE(SendU32(a, 1, 1111) == 0);
    DMQ_ASSERT_TRUE(SendU32(b, 2, 2222) == 0);
    DMQ_ASSERT_TRUE(RecvU32(b, v) == 0 && v == 1111);
    DMQ_ASSERT_TRUE(RecvU32(a, v) == 0 && v == 2222);

    a.SetRecvTimeout(0ms);
    DMQ_ASSERT_TRUE(RecvU32(a, v) == ITransport::RECEIVE_NO_DATA);
    std::cout << "ShmOs_SendReceive_BothDirections() complete!" << std::endl;
}

void ShmOs_Signal_WakesReceiver()
{
    OsShm a, b;
    DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
    b.SetSendTransport(nullptr);
    b.SetRecvTimeout(5000ms);

    std::atomic<int> rc{ -99 };
    std::atomic<uint32_t> value{ 0 };
    const auto start = std::chrono::steady_clock::now();
    std::thread receiver([&] {
        uint32_t v = 0;
        rc = RecvU32(b, v);
        value = v;
    });
    std::this_thread::sleep_for(50ms);
    DMQ_ASSERT_TRUE(SendU32(a, 1, 4321) == 0);
    receiver.join();
    DMQ_ASSERT_TRUE(rc == 0 && value == 4321);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - start < 2000ms);   // woken, not timed out

    // Nothing sent: the wait ends at the timeout. (Margin: Windows timer ticks
    // are ~15.6 ms, and a wait may end up to a tick early.)
    b.SetRecvTimeout(60ms);
    uint32_t v = 0;
    auto t0 = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(RecvU32(b, v) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t0 >= 40ms);

    // A message taken without waiting leaves the event/semaphore set; the next
    // empty Receive() must still wait out its timeout
    DMQ_ASSERT_TRUE(SendU32(a, 9, 99) == 0);
    DMQ_ASSERT_TRUE(RecvU32(b, v) == 0 && v == 99);
    t0 = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(RecvU32(b, v) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t0 >= 40ms);

    // Several sends before a receive: all arrive, and the extra wake-ups do no harm
    for (uint16_t i = 0; i < 5; i++)
        DMQ_ASSERT_TRUE(SendU32(a, i, i) == 0);
    for (uint32_t i = 0; i < 5; i++)
        DMQ_ASSERT_TRUE(RecvU32(b, v) == 0 && v == i);
    DMQ_ASSERT_TRUE(RecvU32(b, v) == ITransport::RECEIVE_NO_DATA);
    std::cout << "ShmOs_Signal_WakesReceiver() complete!" << std::endl;
}

void ShmOs_SideBReopens()
{
    OsShm a, b;
    DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
    b.SetSendTransport(nullptr);

    DMQ_ASSERT_TRUE(SendU32(a, 1, 77) == 0);
    b.Close();
    DMQ_ASSERT_TRUE(!a.IsPeerOpen());
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);    // A still up: names kept
    b.SetSendTransport(nullptr);
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(b, v) == 0 && v == 77);                // queued while B was closed
    std::cout << "ShmOs_SideBReopens() complete!" << std::endl;
}

// Side B built with another layout is rejected, and leaves the link usable
void ShmOs_WrongLayout_Rejected()
{
#if defined(_WIN32)
    using Wrong = Win32ShmTransport<8, 128>;
#else
    using Wrong = LinuxShmTransport<8, 128>;
#endif
    OsShm a, b;
    DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
    Wrong wrongLayout;
    DMQ_ASSERT_TRUE(wrongLayout.Create(LinkName(), Wrong::Side::B) == -1);
    DMQ_ASSERT_TRUE(!a.IsPeerOpen());
    DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
    std::cout << "ShmOs_WrongLayout_Rejected() complete!" << std::endl;
}

#if defined(__linux__)
// While side A's process is alive, a second side A can't take the link over
void ShmOs_SecondSideA_Rejected()
{
    OsShm a, a2;
    DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
    DMQ_ASSERT_TRUE(a2.Create(LinkName(), OsShm::Side::A) == -1);
    DMQ_ASSERT_TRUE(a.IsOpen() && ShmNameExists(LinkName()));      // untouched
    a.Close();
    DMQ_ASSERT_TRUE(a2.Create(LinkName(), OsShm::Side::A) == 0);    // free again
    std::cout << "ShmOs_SecondSideA_Rejected() complete!" << std::endl;
}

void ShmOs_LastClose_RemovesNames()
{
    {
        OsShm a, b;
        DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
        DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
        DMQ_ASSERT_TRUE(ShmNameExists(LinkName()));
        a.Close();
        DMQ_ASSERT_TRUE(ShmNameExists(LinkName()));                // B still open
        b.Close();
        DMQ_ASSERT_TRUE(!ShmNameExists(LinkName()));               // last one out
    }
    {
        // A failed side B removes nothing
        OsShm a, b;
        DMQ_ASSERT_TRUE(a.Create(LinkName(), OsShm::Side::A) == 0);
        LinuxShmTransport<8, 128> wrongLayout;
        DMQ_ASSERT_TRUE(wrongLayout.Create(LinkName(), LinuxShmTransport<8, 128>::Side::B) == -1);
        DMQ_ASSERT_TRUE(ShmNameExists(LinkName()));
        DMQ_ASSERT_TRUE(b.Create(LinkName(), OsShm::Side::B) == 0);
    }
    DMQ_ASSERT_TRUE(!ShmNameExists(LinkName()));
    std::cout << "ShmOs_LastClose_RemovesNames() complete!" << std::endl;
}
#endif

} // namespace

void ShmOsTransportTests()
{
#if defined(__linux__)
    LinuxShmTransport<8, 64>::Remove(LinkName());     // a crashed earlier run
#endif
    ShmOs_SideBBeforeSideA_Fails();
    ShmOs_SendReceive_BothDirections();
    ShmOs_Signal_WakesReceiver();
    ShmOs_SideBReopens();
    ShmOs_WrongLayout_Rejected();
#if defined(__linux__)
    ShmOs_SecondSideA_Rejected();
    ShmOs_LastClose_RemovesNames();
#endif
    std::cout << "ShmOsTransportTests() complete!" << std::endl;
}

#else

void ShmOsTransportTests() {}

#endif
