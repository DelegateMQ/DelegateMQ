#ifndef _IDISPATCHER_H
#define _IDISPATCHER_H

/// @file
/// @brief Delegate dispatcher interface class. 

#include <cstdint>
#include "DelegateOpt.h"

namespace dmq {

// Remote identifier shared between sender and receiver remotes.
typedef uint16_t DelegateRemoteId;
const uint16_t INVALID_REMOTE_ID = static_cast<uint16_t>(-1);
const uint16_t ACK_REMOTE_ID = 0;

/// @TODO Implement the IDispatcher interface if necessary.
/// @brief Delegate interface class to dispatch serialized function argument data
/// to a remote destination. Implemented by the application if using remote delegates.
/// 
/// @details Incoming data from the remote must the `IDispatcher::Dispatch()` to 
/// invoke the target function using argument data. The argument data is serialized 
/// for transport using a concrete class implementing the `ISerialzer` interface 
/// allowing any data argument serialization method is supported.
/// @post The receiver calls `IRemoteInvoker::Invoke()` when the dispatched message
/// is received.
class IDispatcher
{
public:
    virtual ~IDispatcher() = default;

    /// Dispatch a stream of bytes to a remote system. The implementer is responsible
    /// for sending the bytes over a communication transport (UDP, TCP, shared memory, 
    /// serial, ...). 
    /// @param[in] os An outgoing stream to send to the remote destination.
    /// @param[in] id The unique delegate identifier shared between sender and receiver.
    virtual int Dispatch(dmq::xostringstream& os, DelegateRemoteId id, uint16_t* outSeqNum = nullptr) = 0;
};

}

#endif