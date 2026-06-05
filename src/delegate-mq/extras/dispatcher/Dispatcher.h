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
/// 2. **Stream Management:** Validates that the output stream is compatible 
///    (expects `xostringstream`).
/// 3. **Dispatch:** Forwards the header and the serialized payload (stream) to the 
///    registered `ITransport::Send()` method.
/// 
/// **Usage:**
/// This class is typically used internally by `DelegateRemote` to finalize a remote 
/// procedure call before transmission.

#include "delegate/IDispatcher.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransport.h"
#include <sstream>

namespace dmq {

/// @brief Dispatcher sends data to the transport for transmission to the endpoint.
class Dispatcher : public dmq::IDispatcher
{
    XALLOCATOR
public:
    Dispatcher() = default;
    ~Dispatcher() = default;

    void SetTransport(transport::ITransport* transport)
    {
        m_transport = transport;
    }

    // Send argument data to the transport
    int Dispatch(std::ostream& os, dmq::DelegateRemoteId id) override
    {
        dmq::xostringstream* ss = static_cast<dmq::xostringstream*>(&os);

        if (m_transport)
        {
            transport::DmqHeader header(id, transport::DmqHeader::GetNextSeqNum());
            int err = m_transport->Send(*ss, header);

            // Reset the stream content and error state for the next use.
            // This prevents the stream from growing indefinitely.
            ss->str("");
            ss->clear();

            LOG_INFO("Dispatcher::Dispatch id={} seqNum={} err={}", header.GetId(), header.GetSeqNum(), err);
            return err;
        }
        return -1;
    }

private:
    transport::ITransport* m_transport = nullptr;
};

} // namespace dmq


#endif