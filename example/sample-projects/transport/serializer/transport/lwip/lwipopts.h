#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/// @file lwipopts.h
/// @brief lwIP options for transport/serializer's lwip build: lwIP on the host
/// (contrib/ports/unix, pthreads), UDP over the loopback interface only.
///
/// The socket API uses the host's own socket types and constants
/// (LWIP_SOCKET_EXTERNAL_HEADERS, as lwIP's contrib/ports/unix/posixlib does),
/// so lwIP coexists with the host <sys/socket.h> that DelegateMQ's desktop
/// headers include. On a board, lwipopts.h comes from the board's lwIP port
/// instead; ArmLwipUdpTransport needs LWIP_SOCKET and LWIP_SO_RCVTIMEO.

#define NO_SYS                          0
#define LWIP_TCPIP_CORE_LOCKING         1

// Socket API, prefixed names only (lwip_socket(), lwip_sendto(), ...)
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    1
#define LWIP_COMPAT_SOCKETS             0
#define LWIP_SOCKET_OFFSET              1
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SOCKET_EXTERNAL_HEADERS            1
#define LWIP_SOCKET_EXTERNAL_HEADER_SOCKETS_H   "posix/sockets.h"
#define LWIP_SOCKET_EXTERNAL_HEADER_INET_H      "posix/inet.h"
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS   1

// Memory from the C heap, so no pool sizes to tune for this demo
#define MEM_LIBC_MALLOC                 1
#define MEMP_MEM_MALLOC                 1
#if defined __LP64__
#define MEM_ALIGNMENT                   8
#else
#define MEM_ALIGNMENT                   4
#endif

// Protocols: IPv4 UDP over the loopback interface (127.0.0.1)
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_UDP                        1
#define LWIP_TCP                        0
#define LWIP_RAW                        0
#define LWIP_ICMP                       0
#define LWIP_IGMP                       0
#define LWIP_ARP                        0
#define LWIP_DHCP                       0
#define LWIP_DNS                        0
#define LWIP_NETIF_LOOPBACK             1
#define LWIP_HAVE_LOOPIF                1

// Mailboxes between the application and lwIP's tcpip thread
#define TCPIP_MBOX_SIZE                 64
#define DEFAULT_UDP_RECVMBOX_SIZE       64
#define DEFAULT_RAW_RECVMBOX_SIZE       16
#define DEFAULT_ACCEPTMBOX_SIZE         16
#define TCPIP_THREAD_STACKSIZE          0
#define TCPIP_THREAD_PRIO               1

#define LWIP_STATS                      0
#define LWIP_NOASSERT_ON_ERROR          1

#endif
