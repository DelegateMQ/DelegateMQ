#ifndef DISPATCHER_H
#define DISPATCHER_H

/// @file Dispatcher.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief The bridge between the serialization layer and the physical transport layer.
/// 
/// @details
/// The `Dispatcher` class is responsible for packaging serialized function arguments 
/// into a valid DelegateMQ message and handing it off to the transport.
/// 
/// **Key Responsibilities:**
/// 1. **Message Construction:** Creates the protocol header (`DmqHeader`) containing 
///    the Remote ID and a monotonic Sequence Number.
/// 2. **Buffer Handling:** Takes the serialized arguments in a `ByteWriter`
///    and resets it after sending.
/// 3. **Dispatch:** Forwards the header and the serialized payload (the `ByteWriter`'s bytes) to the 
///    registered `ITransport::Send()` method.
/// 
/// **Usage:**
/// This class is typically used internally by `DelegateRemote` to finalize a remote 
/// procedure call before transmission.

#include "delegate/IDispatcher.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransport.h"
#include <atomic>

namespace dmq {

/// @brief Dispatcher sends data to the transport for transmission to the endpoint.
class Dispatcher : public dmq::IDispatcher
{
    XALLOCATOR
public:
    Dispatcher() = default;
    ~Dispatcher() override = default;

    void SetTransport(transport::ITransport* transport)
    {
        m_transport = transport;
    }

    // Send argument data to the transport
    int Dispatch(dmq::ByteWriter& os, dmq::DelegateRemoteId id, uint16_t* outSeqNum = nullptr) override
    {
        if (m_transport)
        {
            uint16_t seq = m_seqNum.fetch_add(1);
            if (outSeqNum) *outSeqNum = seq;
            transport::DmqHeader header(id, seq);
            int err = m_transport->Send(os, header);

            // Empty the buffer for the next message, keeping its capacity
            os.reset();

            LOG_INFO("Dispatcher::Dispatch id={} seqNum={} err={}", header.GetId(), header.GetSeqNum(), err);
            return err;
        }
        return -1;
    }

private:
    transport::ITransport* m_transport = nullptr;
    inline static std::atomic<uint16_t> m_seqNum{0};
};

} // namespace dmq


#endif