#ifndef SYSTEM_IDS_H
#define SYSTEM_IDS_H

/// @file SystemIds.h
/// Topic names and remote IDs shared between C++ server and interop clients.
/// Remote ID values must match the DATA_MSG_ID / CMD_MSG_ID constants in
/// the Python and C# client code.

#include "DelegateMQ.h"

namespace SystemTopic {
    // Topic names used on the local DataBus
    inline constexpr const char* const DataMsg    = "Topic/DataMsg";
    inline constexpr const char* const CommandMsg = "Topic/CommandMsg";

    // Remote IDs for network serialization — must match interop clients
    enum RemoteId : dmq::DelegateRemoteId {
        DataMsgId    = 100,
        CommandMsgId = 101,
    };
}

#endif // SYSTEM_IDS_H
