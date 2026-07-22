#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include "DmqHeader.h"
#include "../../delegate/DelegateOpt.h"

namespace dmq::transport {

/// @TODO Implement the ITransport interface if necessary.
/// @brief DelegateMQ transport interface. 
class ITransport
{
public:
    virtual ~ITransport() = default;
    /// Send data to a remote
    /// @param[in] os Output stream to send.
    /// @param[in] header The header to send.
    /// @return 0 if success.
    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) = 0;

    /// Receive data from a remote
    /// @param[out] is The received incoming data bytes, not including the header.
    /// @param[out] header Incoming delegate message header.
    /// @return 0 if success.
    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) = 0;
};

} // namespace dmq::transport


#endif