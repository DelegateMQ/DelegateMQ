#ifndef DMQ_NODE_PACKETS_H
#define DMQ_NODE_PACKETS_H

/// @file NodePackets.h
/// @brief What NodeReporter sends to DelegateMQ's diagnostic consoles, and the
/// remote IDs that identify each message: node heartbeats and health counters
/// for dmq-monitor, thread statistics (dmq::util::ThreadStatsPacket) for
/// dmq-thread. Each goes out as one DelegateMQ message: a header with the
/// remote ID below, then the serialized packet.

#include <cstdint>
#include "delegate/DelegateOpt.h"
#include "port/serialize/serialize/msg_serialize.h"

namespace dmq {

namespace databus {

/// @brief Remote IDs reserved for the diagnostic consoles, next to
/// SPY_REMOTE_ID (0xFFFE, SpyPacket.h). Don't use them for application messages.
inline constexpr uint16_t NODE_INFO_REMOTE_ID    = 0xFFFD;  ///< NodeInfoPacket (dmq-monitor)
inline constexpr uint16_t NODE_HEALTH_REMOTE_ID  = 0xFFFC;  ///< NodeHealthPacket (dmq-monitor)
inline constexpr uint16_t THREAD_STATS_REMOTE_ID = 0xFFFB;  ///< dmq::util::ThreadStatsPacket (dmq-thread)

} // namespace databus

/// @brief Heartbeat packet sent by NodeReporter to identify a node on the DataBus network.
/// @details Periodically serialized and transmitted by NodeReporter. The dmq-monitor console
/// listens for these packets and builds a live view of all active nodes.
struct NodeInfoPacket : public serialize::I {
    XALLOCATOR
    dmq::xstring nodeId;        ///< User-assigned node name (e.g. "SensorNode-1")
    dmq::xstring hostname;      ///< Machine hostname
    dmq::xstring ipAddress;     ///< Self-reported IP address
    uint64_t    uptimeMs    = 0;    ///< Milliseconds since NodeReporter::Start()
    uint32_t    totalMsgCount = 0;  ///< Total DataBus messages published since start
    dmq::xstring topicsStr;     ///< Semicolon-delimited topics (e.g. "sensor/temp;system/status")

    dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        ms.write(os, nodeId);
        ms.write(os, hostname);
        ms.write(os, ipAddress);
        ms.write(os, uptimeMs);
        ms.write(os, totalMsgCount);
        return ms.write(os, topicsStr);
    }

    dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        ms.read(is, nodeId);
        ms.read(is, hostname);
        ms.read(is, ipAddress);
        ms.read(is, uptimeMs);
        ms.read(is, totalMsgCount);
        return ms.read(is, topicsStr);
    }
};

/// @brief Health counters sent by NodeReporter next to each heartbeat.
/// @details A separate message (NODE_HEALTH_REMOTE_ID) rather than extra
/// NodeInfoPacket fields: dmq-monitor shows "-" for a node that never sends it.
/// Counters are totals since NodeReporter::Start(); dmq-monitor computes recent
/// activity from successive packets, so a lost packet loses nothing.
struct NodeHealthPacket : public serialize::I {
    XALLOCATOR

    /// Bits in `sources`: what this node is reporting on.
    static constexpr uint32_t SRC_DATABUS   = 1u << 0;  ///< DataBus errors (always)
    static constexpr uint32_t SRC_TRANSPORT = 1u << 1;  ///< A TransportMonitor or NetworkNode is watched
    static constexpr uint32_t SRC_RETRY     = 1u << 2;  ///< A RetryMonitor or NetworkNode is watched
    static constexpr uint32_t SRC_ALLOCATOR = 1u << 3;  ///< The fixed-block allocator (DMQ_ALLOCATOR): the pool* fields

    dmq::xstring nodeId;        ///< Same as NodeInfoPacket::nodeId
    dmq::xstring ipAddress;     ///< Same as NodeInfoPacket::ipAddress (with nodeId, the monitor's row key)
    uint32_t    sources          = 0;  ///< SRC_* bits
    uint32_t    dataBusErrors    = 0;  ///< DataBus::SubscribeError reports (latched: once per topic and error unless continuous errors are enabled)
    uint32_t    sendTimeouts     = 0;  ///< Reliable sends not ACKed in time; each is retried (TransportMonitor TIMEOUT)
    uint32_t    deliveryFailures = 0;  ///< Reliable sends abandoned after all retries (RetryMonitor / NetworkNode OnDeliveryFailed)
    uint32_t    capExceeded      = 0;  ///< Sends rejected: too many unacknowledged messages (OnCapExceeded)
    uint32_t    pendingExceeded  = 0;  ///< Expired entries piling up faster than they're processed (OnPendingExceeded)
    dmq::xstring lastEvent;     ///< Most recent problem, as text (e.g. "delivery failed, Controller id=12 seq=40, retries exhausted")

    // The fixed-block allocator (SRC_ALLOCATOR). With static pools
    // (DMQ_XALLOCATOR_STATIC_POOLS), the fullest pool by its peak: its blocks
    // most in use at once against its size. A pool whose peak reaches its
    // capacity faults on the next allocation. Blocks from the heap have no
    // fixed limit: capacity 0, and only the total is reported.
    uint32_t    poolBlockSize    = 0;  ///< Fullest pool: bytes per block
    uint32_t    poolPeak         = 0;  ///< Fullest pool: most blocks in use at once
    uint32_t    poolCapacity     = 0;  ///< Fullest pool: blocks in it (DMQ_XALLOC_POOL_<size>); 0: heap-backed
    uint32_t    poolPeakBytes    = 0;  ///< All block sizes: each one's peak blocks times its block size, summed

    dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        ms.write(os, nodeId);
        ms.write(os, ipAddress);
        ms.write(os, sources);
        ms.write(os, dataBusErrors);
        ms.write(os, sendTimeouts);
        ms.write(os, deliveryFailures);
        ms.write(os, capExceeded);
        ms.write(os, pendingExceeded);
        ms.write(os, lastEvent);
        ms.write(os, poolBlockSize);
        ms.write(os, poolPeak);
        ms.write(os, poolCapacity);
        return ms.write(os, poolPeakBytes);
    }

    dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        ms.read(is, nodeId);
        ms.read(is, ipAddress);
        ms.read(is, sources);
        ms.read(is, dataBusErrors);
        ms.read(is, sendTimeouts);
        ms.read(is, deliveryFailures);
        ms.read(is, capExceeded);
        ms.read(is, pendingExceeded);
        ms.read(is, lastEvent);
        // A node built before the pool fields sends none: read them as 0 (and
        // its sources has no SRC_ALLOCATOR), so a newer dmq-monitor still shows it
        poolBlockSize = poolPeak = poolCapacity = poolPeakBytes = 0;
        if (!is.good() || is.remaining() == 0)
            return is;
        ms.read(is, poolBlockSize);
        ms.read(is, poolPeak);
        ms.read(is, poolCapacity);
        return ms.read(is, poolPeakBytes);
    }
};

} // namespace dmq

#endif // DMQ_NODE_PACKETS_H
