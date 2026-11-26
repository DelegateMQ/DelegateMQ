#ifndef SHM_LINK_H
#define SHM_LINK_H

// The gateway <-> monitor hop over shared memory (--shm on both), in place of
// the named pipe / Unix domain socket. ShmRingTransport isn't selected by
// DMQ_TRANSPORT; its header is included here, beside the UDP or IPC transport.
//
//   Windows: file mapping Local\dmq-shm-relay + named events
//   Linux:   /dev/shm/dmq-shm-relay + named semaphores
//
// The gateway is side A (creates the region), the monitor side B (attaches),
// so start the gateway first; the monitor retries for up to 10 s.

#include "DelegateMQ.h"
#if defined(_WIN32)
#include "port/transport/win32-shm/Win32ShmTransport.h"
using ShmTransport = dmq::transport::Win32ShmTransport<>;
#else
#include "port/transport/linux-shm/LinuxShmTransport.h"
using ShmTransport = dmq::transport::LinuxShmTransport<>;
#endif

#include <cstdlib>
#include <cstring>

namespace SystemLink {
    inline constexpr const char* const ShmName = "relay";
}

/// Command line shared by the gateway and monitor: [seconds] [--shm], in any order.
struct NodeArgs {
    int duration = 0;
    bool shm = false;
};

inline NodeArgs ParseNodeArgs(int argc, char* argv[])
{
    NodeArgs a;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shm") == 0)
            a.shm = true;
        else
            a.duration = atoi(argv[i]);
    }
    return a;
}

#endif // SHM_LINK_H
