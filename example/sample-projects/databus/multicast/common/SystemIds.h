#ifndef SYSTEM_IDS_H
#define SYSTEM_IDS_H

#include "DelegateMQ.h"

namespace SystemTopic {
    // Topic names
    inline constexpr const char* const DataMsg = "Topic/DataMsg";
    inline constexpr const char* const CommandMsg = "Topic/CommandMsg";

    // Remote IDs (DelegateRemoteId) for network distribution
    // These must match between client and server
    enum RemoteId : dmq::DelegateRemoteId {
        DataMsgId = 100,
        CommandMsgId = 101
    };
}

#endif // SYSTEM_IDS_H
