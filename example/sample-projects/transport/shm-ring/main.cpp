/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Remote delegates between two processes over shared memory
/// (Win32ShmTransport on Windows, LinuxShmTransport on Linux).
///
/// Side A, the "controller", calls `SetSpeed(Command)` on side B every 20 ms.
/// Side B, the "motor", runs it and calls `OnTelemetry(Telemetry)` back on side
/// A. Arguments are copied as raw bytes (raw::Serializer): both processes are
/// the same build. Each side's receive thread sleeps on the transport's named
/// event (Windows) or semaphore (Linux) until the other side sends: the same
/// wake-up an MCU gets from a cross-core interrupt.
///
/// Usage:
///   delegate_app [seconds]       side A, and starts side B as a child process
///   delegate_app a [seconds]     side A only (start side B in another terminal)
///   delegate_app b               side B only (start side A first)
///
/// Exits with status 1 if side A got no telemetry, or a reply didn't match.

#include "DelegateMQ.h"

#if defined(_WIN32)
#include "port/transport/win32-shm/Win32ShmTransport.h"
#else
#include "port/transport/linux-shm/LinuxShmTransport.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace dmq;
using namespace dmq::transport;
using namespace std::chrono_literals;
namespace raw = dmq::serialization::raw;

#if defined(_WIN32)
using ShmTransport = Win32ShmTransport<>;
#else
using ShmTransport = LinuxShmTransport<>;
#endif

static const char* LINK_NAME = "sample";

// Messages: plain structs of fixed-width fields, so raw byte copies are safe
struct Command {
    uint32_t seq;
    int32_t speed;
};
struct Telemetry {
    uint32_t seq;           // the Command this answers
    int32_t speed;          // speed the motor now runs at
    uint32_t commandsRun;   // Commands side B has run so far
};

static const DelegateRemoteId SET_SPEED_ID = 1;    // A -> B
static const DelegateRemoteId TELEMETRY_ID = 2;    // B -> A

static raw::Serializer<void(Command)> s_commandSer;
static raw::Serializer<void(Telemetry)> s_telemetrySer;

static void OnError(DelegateRemoteId id, DelegateError err, DelegateErrorAux)
{
    if (err != DelegateError::SUCCESS)
        printf("Remote error: id %u, error %d\n", static_cast<unsigned>(id), static_cast<int>(err));
}

/// Receive and dispatch until `stop` is set. One message per Receive();
/// each call sleeps up to the receive timeout when nothing is waiting.
template <class Receiver>
static void ReceiveLoop(ShmTransport& transport, DelegateRemoteId id, Receiver& receiver, std::atomic<bool>& stop)
{
    while (!stop) {
        DmqHeader header;
        ByteReader is;
        if (transport.Receive(is, header) == 0 && header.GetId() == id)
            receiver.Invoke(is);
    }
}

// ---------------------------------------------------------------- side B

static RemoteChannel<void(Telemetry)>* s_telemetry = nullptr;
static std::atomic<uint32_t> s_commandsRun{ 0 };

static void SetSpeed(Command cmd)
{
    // Runs on side B's receive thread; answer with telemetry
    const uint32_t run = ++s_commandsRun;
    (*s_telemetry)(Telemetry{ cmd.seq, cmd.speed, run });
}

static int RunSideB()
{
    ShmTransport transport;
    RemoteChannel<void(Telemetry)> telemetry(transport, s_telemetrySer, TELEMETRY_ID);
    telemetry.SetErrorHandler(MakeDelegate(&OnError));
    s_telemetry = &telemetry;

    // Side A creates the region; wait for it
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (transport.Create(LINK_NAME, ShmTransport::Side::B) != 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            printf("Side B: no side A within 10 s\n");
            return 1;
        }
        std::this_thread::sleep_for(50ms);
    }
    transport.SetSendTransport(nullptr);        // no ACKs: no TransportMonitor on this link
    printf("Side B: attached\n");

    DelegateFreeRemote<void(Command)> setSpeed(&SetSpeed, SET_SPEED_ID);
    setSpeed.SetSerializer(&s_commandSer);
    setSpeed.SetErrorHandler(MakeDelegate(&OnError));

    std::atomic<bool> stop{ false };
    std::thread receiver([&] { ReceiveLoop(transport, SET_SPEED_ID, setSpeed, stop); });

    // Run until side A closes its end
    while (transport.IsPeerOpen())
        std::this_thread::sleep_for(50ms);
    stop = true;
    receiver.join();
    printf("Side B: side A closed; ran %u commands\n", static_cast<unsigned>(s_commandsRun.load()));
    transport.Close();
    return 0;
}

// ---------------------------------------------------------------- side A

static std::atomic<uint32_t> s_telemetryCount{ 0 };
static std::atomic<uint32_t> s_badTelemetry{ 0 };

static void OnTelemetry(Telemetry t)
{
    // Runs on side A's receive thread. Command n asked for speed n * 10.
    if (t.speed != static_cast<int32_t>(t.seq * 10))
        s_badTelemetry++;
    if (s_telemetryCount++ % 50 == 0)
        printf("Telemetry: seq %u speed %d (side B has run %u commands)\n",
            static_cast<unsigned>(t.seq), static_cast<int>(t.speed), static_cast<unsigned>(t.commandsRun));
}

static bool StartSideB(const char* self);
static int WaitSideB();

static int RunSideA(int seconds, bool spawnB, const char* self)
{
    ShmTransport transport;
    if (transport.Create(LINK_NAME, ShmTransport::Side::A) != 0) {
        printf("Side A: can't create the shared region\n");
        return 1;
    }
    transport.SetSendTransport(nullptr);
    printf("Side A: region created (%u bytes)\n", static_cast<unsigned>(sizeof(ShmTransport::ControlBlock)));

    if (spawnB && !StartSideB(self)) {
        printf("Side A: can't start side B\n");
        return 1;
    }

    DelegateFreeRemote<void(Telemetry)> telemetry(&OnTelemetry, TELEMETRY_ID);
    telemetry.SetSerializer(&s_telemetrySer);
    telemetry.SetErrorHandler(MakeDelegate(&OnError));

    std::atomic<bool> stop{ false };
    std::thread receiver([&] { ReceiveLoop(transport, TELEMETRY_ID, telemetry, stop); });

    RemoteChannel<void(Command)> setSpeed(transport, s_commandSer, SET_SPEED_ID);
    setSpeed.SetErrorHandler(MakeDelegate(&OnError));

    // Wait for side B, then send commands for the run time
    const auto attachDeadline = std::chrono::steady_clock::now() + 10s;
    while (!transport.IsPeerOpen() && std::chrono::steady_clock::now() < attachDeadline)
        std::this_thread::sleep_for(10ms);

    uint32_t sent = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (transport.IsPeerOpen() && std::chrono::steady_clock::now() < end) {
        setSpeed(Command{ sent, static_cast<int32_t>(sent * 10) });
        sent++;
        std::this_thread::sleep_for(20ms);
    }
    std::this_thread::sleep_for(100ms);         // let the last replies arrive

    stop = true;
    receiver.join();
    transport.Close();                          // side B sees this and exits

    int childStatus = spawnB ? WaitSideB() : 0;

    printf("Side A: sent %u commands, received %u telemetry, %u mismatched\n",
        static_cast<unsigned>(sent), static_cast<unsigned>(s_telemetryCount.load()),
        static_cast<unsigned>(s_badTelemetry.load()));
    if (s_telemetryCount == 0 || s_badTelemetry != 0 || childStatus != 0)
        return 1;
    return 0;
}

// ---------------------------------------------------------------- child process

#if defined(_WIN32)
static PROCESS_INFORMATION s_child{};

static bool StartSideB(const char* self)
{
    char cmd[MAX_PATH + 16];
    snprintf(cmd, sizeof(cmd), "\"%s\" b", self);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    return CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &s_child) != 0;
}

static int WaitSideB()
{
    DWORD code = 1;
    if (WaitForSingleObject(s_child.hProcess, 10000) == WAIT_OBJECT_0)
        GetExitCodeProcess(s_child.hProcess, &code);
    else
        TerminateProcess(s_child.hProcess, 1);
    CloseHandle(s_child.hProcess);
    CloseHandle(s_child.hThread);
    return static_cast<int>(code);
}
#else
static pid_t s_child = -1;

static bool StartSideB(const char*)
{
    s_child = fork();
    if (s_child == 0) {
        // The same executable, as side B
        char arg0[] = "delegate_app";
        char arg1[] = "b";
        char* argv[] = { arg0, arg1, nullptr };
        execv("/proc/self/exe", argv);
        _exit(127);
    }
    return s_child > 0;
}

static int WaitSideB()
{
    int status = 0;
    for (int i = 0; i < 100; i++) {         // up to 10 s
        if (waitpid(s_child, &status, WNOHANG) == s_child)
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        std::this_thread::sleep_for(100ms);
    }
    kill(s_child, SIGKILL);
    waitpid(s_child, &status, 0);
    return 1;
}
#endif

int main(int argc, char* argv[])
{
#if defined(_WIN32)
    char self[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, self, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        printf("Can't get this program's path\n");
        return 1;
    }
#else
    const char* self = argv[0];
#endif

    if (argc >= 2 && strcmp(argv[1], "b") == 0)
        return RunSideB();
    if (argc >= 2 && strcmp(argv[1], "a") == 0)
        return RunSideA(argc >= 3 ? atoi(argv[2]) : 30, false, self);
    return RunSideA(argc >= 2 ? atoi(argv[1]) : 5, true, self);
}
