#ifndef DMQ_WIN32_SHM_TRANSPORT_H
#define DMQ_WIN32_SHM_TRANSPORT_H

/// @file Win32ShmTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Shared-memory transport between two processes on one Windows machine:
/// a ShmRingTransport over a named file mapping, woken by named events.
///
/// @details
/// - **Region:** a page-file-backed mapping `Local\dmq-shm-<name>` holding the
///   ShmControlBlock. Side A creates it (or opens one left by a peer) and
///   initializes it; side B opens it and fails until side A has. Windows removes
///   the mapping when the last handle closes, so nothing is left behind.
/// - **Wake-up:** two auto-reset events, `Local\dmq-shm-<name>-a` (data for side
///   A) and `-b` (data for side B). Send() sets the peer's event; a Receive() that
///   finds its ring empty waits on its own for up to the receive timeout. An
///   auto-reset event stays set until a wait consumes it, so no wake-up is lost.
/// - Both processes must build with the same DMQ_SHM_SLOT_COUNT / DMQ_SHM_MAX_PAYLOAD
///   (or template arguments); side B's Create() fails on a mismatch.
/// - Start side A first, and don't restart it while side B runs: side A's
///   Create() empties the rings. Side B may close and reopen freely.
/// - `Local\` names are per login session; another session can't open them.
///
/// @code
///   dmq::transport::Win32ShmTransport<> a;   // process 1
///   a.Create("demo", dmq::transport::Win32ShmTransport<>::Side::A);
///   dmq::transport::Win32ShmTransport<> b;   // process 2, after process 1
///   while (b.Create("demo", dmq::transport::Win32ShmTransport<>::Side::B) != 0) Sleep(100);
/// @endcode
///
/// @note Desktop-only (Windows API), not included by DelegateMQ.h: include this header.

#if !defined(_WIN32)
#error "Win32ShmTransport.h is for Windows; on Linux use LinuxShmTransport.h"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "port/transport/shm-ring/ShmRingTransport.h"
#include <chrono>
#include <cstdio>
#include <new>

namespace dmq::transport {

template <size_t SlotCount = dmq::SHM_SLOT_COUNT, size_t MaxPayload = dmq::SHM_MAX_PAYLOAD>
class Win32ShmTransport : public ShmRingTransport<SlotCount, MaxPayload>, private IShmSignal
{
    using Base = ShmRingTransport<SlotCount, MaxPayload>;

public:
    using typename Base::ControlBlock;
    using typename Base::Side;

    Win32ShmTransport() = default;
    ~Win32ShmTransport() override { Close(); }

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

        char mapName[MAX_NAME], evA[MAX_NAME], evB[MAX_NAME];
        if (!MakeName(mapName, name, "") || !MakeName(evA, name, "-a") || !MakeName(evB, name, "-b"))
            return -1;

        const DWORD size = static_cast<DWORD>(sizeof(ControlBlock));
        if (side == Side::A) {
            // Creates the mapping, or opens the one a running side B still holds
            m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, mapName);
        }
        else {
            m_mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mapName);
        }
        if (!m_mapping)
            return Fail();

        void* view = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!view)
            return Fail();
        m_view = view;

        // Side B: the region must be large enough before it is read
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(view, &info, sizeof(info)) == 0 || info.RegionSize < size)
            return Fail();

        // Side A constructs the block in the fresh mapping (Create() then Init()s it);
        // side B uses the one side A built
        ControlBlock* block = (side == Side::A) ? new (view) ControlBlock() : static_cast<ControlBlock*>(view);

        // Auto-reset, initially clear; whichever side comes first creates each
        m_ownEvent = CreateEventA(nullptr, FALSE, FALSE, side == Side::A ? evA : evB);
        m_peerEvent = CreateEventA(nullptr, FALSE, FALSE, side == Side::A ? evB : evA);
        if (!m_ownEvent || !m_peerEvent)
            return Fail();

        if (Base::Create(*block, side, this) != 0)
            return Fail();
        return 0;
    }

    /// @brief Close this side and release its handles. Safe to call more than once.
    void Close()
    {
        Base::Close();
        if (m_view) { UnmapViewOfFile(m_view); m_view = nullptr; }
        if (m_mapping) { CloseHandle(m_mapping); m_mapping = nullptr; }
        if (m_ownEvent) { CloseHandle(m_ownEvent); m_ownEvent = nullptr; }
        if (m_peerEvent) { CloseHandle(m_peerEvent); m_peerEvent = nullptr; }
    }

private:
    static constexpr size_t MAX_NAME = 128;

    static bool MakeName(char* out, const char* name, const char* suffix)
    {
        const int n = snprintf(out, MAX_NAME, "Local\\dmq-shm-%s%s", name, suffix);
        return n > 0 && n < static_cast<int>(MAX_NAME);
    }

    int Fail()
    {
        Close();
        return -1;
    }

    // IShmSignal
    void NotifyPeer() override { SetEvent(m_peerEvent); }

    bool WaitForData(std::chrono::milliseconds timeout) override
    {
        return WaitForSingleObject(m_ownEvent, static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
    }

    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    HANDLE m_ownEvent = nullptr;        // set by the peer: data for this side
    HANDLE m_peerEvent = nullptr;       // set by this side: data for the peer
};

} // namespace dmq::transport

#endif // DMQ_WIN32_SHM_TRANSPORT_H
