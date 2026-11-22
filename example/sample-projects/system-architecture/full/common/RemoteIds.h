#ifndef REMOTE_IDS_H
#define REMOTE_IDS_H

namespace ids
{
    constexpr dmq::DelegateRemoteId ALARM_MSG_ID = 1;
    constexpr dmq::DelegateRemoteId DATA_MSG_ID = 2;
    constexpr dmq::DelegateRemoteId COMMAND_MSG_ID = 3;
    constexpr dmq::DelegateRemoteId ACTUATOR_MSG_ID = 4;
}

#endif