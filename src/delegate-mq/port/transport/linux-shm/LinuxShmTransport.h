#ifndef DMQ_LINUX_SHM_TRANSPORT_H
#define DMQ_LINUX_SHM_TRANSPORT_H

/// @file LinuxShmTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Shared-memory transport between two processes on one Linux machine:
/// a ShmRingTransport over a POSIX shared-memory object, woken by named
/// semaphores.
///
/// @details
/// - **Region:** `/dmq-shm-<name>` (`/dev/shm/dmq-shm-<name>`) holding the
///   ShmControlBlock. Side A creates it (or reuses one left behind) and
///   initializes it; side B opens it and fails until side A has.
/// - **Wake-up:** two named semaphores, `/dmq-shm-<name>-a` (data for side A) and
///   `-b` (data for side B). Send() posts the peer's; a Receive() that finds its
///   ring empty waits on its own for up to the receive timeout. A post before the
///   wait is kept by the semaphore, so no wake-up is lost. Send() posts only when
///   the count is 0, so it stays small when the receiver polls instead of waiting.
/// - **Names on disk:** the last side to Close() unlinks the three names. A
///   process that crashes leaves them in /dev/shm; the next side A reuses them.
/// - Both processes must build with the same DMQ_SHM_SLOT_COUNT / DMQ_SHM_MAX_PAYLOAD
///   (or template arguments); side B's Create() fails on a mismatch.
/// - Start side A first, and don't restart it while side B runs: side A's
///   Create() empties the rings. Side B may close and reopen freely.
/// - Objects are created mode 0600: other users can't open them.
///
/// @code
///   dmq::transport::LinuxShmTransport<> a;   // process 1
///   a.Create("demo", dmq::transport::LinuxShmTransport<>::Side::A);
///   dmq::transport::LinuxShmTransport<> b;   // process 2, after process 1
///   while (b.Create("demo", dmq::transport::LinuxShmTransport<>::Side::B) != 0) usleep(100000);
/// @endcode
///
/// @note Desktop-only (POSIX API), not included by DelegateMQ.h: include this header.

#include "port/transport/shm-ring/ShmRingTransport.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dmq::transport {

template <size_t SlotCount = dmq::SHM_SLOT_COUNT, size_t MaxPayload = dmq::SHM_MAX_PAYLOAD>
class LinuxShmTransport : public ShmRingTransport<SlotCount, MaxPayload>, private IShmSignal
{
    using Base = ShmRingTransport<SlotCount, MaxPayload>;

public:
    using typename Base::ControlBlock;
    using typename Base::Side;

    LinuxShmTransport() = default;
    ~LinuxShmTransport() override { Close(); }

    /// @brief Open this side of the link named `name`.
    /// @param[in] name Link name shared by both processes (letters, digits, '-', '_').
    /// @param[in] side Side::A creates and initializes the region; Side::B opens it.
    /// @return 0 on success; -1 on failure (side B: side A not up yet, or built
    /// with another layout; retry).
    int Create(const char* name, Side side)
    {
        Close();
        if (!name || !*name)
            return -1;
        if (!MakeName(m_shmName, name, "") || !MakeName(m_semA, name, "-a") || !MakeName(m_semB, name, "-b"))
            return -1;

        const size_t size = sizeof(ControlBlock);
        const bool isA = (side == Side::A);
        m_side = side;
        const int fd = isA ? shm_open(m_shmName, O_CREAT | O_RDWR, 0600) : shm_open(m_shmName, O_RDWR, 0);
        if (fd < 0)
            return Fail();

        bool sized = false;
        if (isA) {
            sized = ftruncate(fd, static_cast<off_t>(size)) == 0;
        }
        else {
            // Side A may not have sized it yet
            struct stat st{};
            sized = fstat(fd, &st) == 0 && static_cast<size_t>(st.st_size) >= size;
        }
        void* view = sized ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
        close(fd);      // the mapping keeps the object
        if (view == MAP_FAILED)
            return Fail();
        m_view = view;

        // Side A constructs the block (Create() then Init()s it); side B uses side A's
        ControlBlock* block = isA ? new (view) ControlBlock() : static_cast<ControlBlock*>(view);

        if (isA) {
            m_ownSem = sem_open(m_semA, O_CREAT, 0600, 0);
            m_peerSem = sem_open(m_semB, O_CREAT, 0600, 0);
        }
        else {
            m_ownSem = sem_open(m_semB, 0);
            m_peerSem = sem_open(m_semA, 0);
        }
        if (m_ownSem == SEM_FAILED || m_peerSem == SEM_FAILED)
            return Fail();

        if (isA) {
            // Reused semaphores may hold counts from an earlier run
            while (sem_trywait(m_ownSem) == 0) {}
            while (sem_trywait(m_peerSem) == 0) {}
        }

        if (Base::Create(*block, side, this) != 0)
            return Fail();
        return 0;
    }

    /// @brief Close this side and release its resources. The last side to close
    /// unlinks the names. Safe to call more than once.
    void Close()
    {
        const bool wasOpen = Base::IsOpen();
        Base::Close();
        // Peer's flag read after ours is cleared: if both close at once, both
        // may unlink, which is harmless
        bool peerOpen = false;
        if (wasOpen && m_view) {
            const ControlBlock* block = static_cast<const ControlBlock*>(m_view);
            peerOpen = (m_side == Side::A ? block->readyB : block->readyA).load(std::memory_order_acquire) != 0;
        }
        if (m_view) { munmap(m_view, sizeof(ControlBlock)); m_view = nullptr; }
        if (m_ownSem != SEM_FAILED) { sem_close(m_ownSem); m_ownSem = SEM_FAILED; }
        if (m_peerSem != SEM_FAILED) { sem_close(m_peerSem); m_peerSem = SEM_FAILED; }
        if (wasOpen && !peerOpen) {
            shm_unlink(m_shmName);
            sem_unlink(m_semA);
            sem_unlink(m_semB);
        }
    }

    /// @brief Remove the names a crashed process left behind. Only while no
    /// process uses the link.
    static void Remove(const char* name)
    {
        char buf[MAX_NAME];
        if (MakeName(buf, name, "")) shm_unlink(buf);
        if (MakeName(buf, name, "-a")) sem_unlink(buf);
        if (MakeName(buf, name, "-b")) sem_unlink(buf);
    }

private:
    static constexpr size_t MAX_NAME = 128;

    static bool MakeName(char* out, const char* name, const char* suffix)
    {
        const int n = snprintf(out, MAX_NAME, "/dmq-shm-%s%s", name, suffix);
        return n > 0 && n < static_cast<int>(MAX_NAME);
    }

    int Fail()
    {
        // Not open yet, so Close() unlinks nothing: a failed side B must not
        // remove side A's names
        Close();
        return -1;
    }

    // IShmSignal
    void NotifyPeer() override
    {
        int value = 0;
        if (sem_getvalue(m_peerSem, &value) != 0 || value == 0)
            sem_post(m_peerSem);
    }

    bool WaitForData(std::chrono::milliseconds timeout) override
    {
        // sem_timedwait takes an absolute CLOCK_REALTIME time
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        const long long ns = static_cast<long long>(ts.tv_nsec) + static_cast<long long>(timeout.count()) * 1000000LL;
        ts.tv_sec += static_cast<time_t>(ns / 1000000000LL);
        ts.tv_nsec = static_cast<long>(ns % 1000000000LL);
        for (;;) {
            if (sem_timedwait(m_ownSem, &ts) == 0)
                return true;
            if (errno != EINTR)
                return false;   // ETIMEDOUT
        }
    }

    void* m_view = nullptr;
    sem_t* m_ownSem = SEM_FAILED;       // posted by the peer: data for this side
    sem_t* m_peerSem = SEM_FAILED;      // posted by this side: data for the peer
    Side m_side = Side::A;
    char m_shmName[MAX_NAME] = {};
    char m_semA[MAX_NAME] = {};
    char m_semB[MAX_NAME] = {};
};

} // namespace dmq::transport

#endif // DMQ_LINUX_SHM_TRANSPORT_H
