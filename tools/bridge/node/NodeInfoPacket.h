// NodeInfoPacket.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Node heartbeat packet for the DelegateMQ Node Monitor.

#ifndef NODE_INFO_PACKET_H
#define NODE_INFO_PACKET_H

#include <cstdint>
#include "delegate/DelegateOpt.h"
#include "port/serialize/serialize/msg_serialize.h"

namespace dmq {

enum class PacketType : uint8_t {
    NodeInfo    = 1,
    ThreadStats = 2,
    SpyData     = 3,
    Health      = 4,
};

/// @brief Heartbeat packet broadcast by NodeBridge to identify a node on the DataBus network.
/// @details Periodically serialized and transmitted by NodeBridge. The dmq-monitor console
/// listens for these packets and builds a live view of all active nodes.
struct NodeInfoPacket : public serialize::I {
    XALLOCATOR
    dmq::xstring nodeId;        ///< User-assigned node name (e.g. "SensorNode-1")
    dmq::xstring hostname;      ///< Machine hostname
    dmq::xstring ipAddress;     ///< Self-reported IP address
    uint64_t    uptimeMs    = 0;    ///< Milliseconds since NodeBridge::Start()
    uint32_t    totalMsgCount = 0;  ///< Total DataBus messages published since start
    dmq::xstring topicsStr;     ///< Semicolon-delimited topics (e.g. "sensor/temp;system/status")

    std::ostream& write(serialize& ms, std::ostream& os) override {
        ms.write(os, nodeId);
        ms.write(os, hostname);
        ms.write(os, ipAddress);
        ms.write(os, uptimeMs);
        ms.write(os, totalMsgCount);
        return ms.write(os, topicsStr);
    }

    std::istream& read(serialize& ms, std::istream& is) override {
        ms.read(is, nodeId);
        ms.read(is, hostname);
        ms.read(is, ipAddress);
        ms.read(is, uptimeMs);
        ms.read(is, totalMsgCount);
        return ms.read(is, topicsStr);
    }
};

/// @brief Health counters broadcast by NodeBridge next to each heartbeat.
/// @details A separate packet type rather than extra NodeInfoPacket fields, so
/// mixed versions keep working: an older dmq-monitor ignores packet types it
/// doesn't know, and a newer one shows "-" for nodes that never send this.
/// Counters are totals since NodeBridge::Start(); dmq-monitor computes recent
/// activity from successive packets, so a lost packet loses nothing.
struct NodeHealthPacket : public serialize::I {
    XALLOCATOR

    /// Bits in `sources`: what this node is reporting on.
    static constexpr uint32_t SRC_DATABUS   = 1u << 0;  ///< DataBus errors (always)
    static constexpr uint32_t SRC_TRANSPORT = 1u << 1;  ///< A TransportMonitor or NetworkNode is watched
    static constexpr uint32_t SRC_RETRY     = 1u << 2;  ///< A RetryMonitor or NetworkNode is watched

    dmq::xstring nodeId;        ///< Same as NodeInfoPacket::nodeId
    dmq::xstring ipAddress;     ///< Same as NodeInfoPacket::ipAddress (with nodeId, the monitor's row key)
    uint32_t    sources          = 0;  ///< SRC_* bits
    uint32_t    dataBusErrors    = 0;  ///< DataBus::SubscribeError reports (latched: once per topic and error unless continuous errors are enabled)
    uint32_t    sendTimeouts     = 0;  ///< Reliable sends not ACKed in time; each is retried (TransportMonitor TIMEOUT)
    uint32_t    deliveryFailures = 0;  ///< Reliable sends abandoned after all retries (RetryMonitor / NetworkNode OnDeliveryFailed)
    uint32_t    capExceeded      = 0;  ///< Sends rejected: too many unacknowledged messages (OnCapExceeded)
    uint32_t    pendingExceeded  = 0;  ///< Expired entries piling up faster than they're processed (OnPendingExceeded)
    dmq::xstring lastEvent;     ///< Most recent problem, as text (e.g. "delivery failed, Controller id=12 seq=40, retries exhausted")

    std::ostream& write(serialize& ms, std::ostream& os) override {
        ms.write(os, nodeId);
        ms.write(os, ipAddress);
        ms.write(os, sources);
        ms.write(os, dataBusErrors);
        ms.write(os, sendTimeouts);
        ms.write(os, deliveryFailures);
        ms.write(os, capExceeded);
        ms.write(os, pendingExceeded);
        return ms.write(os, lastEvent);
    }

    std::istream& read(serialize& ms, std::istream& is) override {
        ms.read(is, nodeId);
        ms.read(is, ipAddress);
        ms.read(is, sources);
        ms.read(is, dataBusErrors);
        ms.read(is, sendTimeouts);
        ms.read(is, deliveryFailures);
        ms.read(is, capExceeded);
        ms.read(is, pendingExceeded);
        return ms.read(is, lastEvent);
    }
};

} // namespace dmq

#endif // NODE_INFO_PACKET_H
