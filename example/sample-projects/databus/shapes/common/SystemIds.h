#ifndef SYSTEM_IDS_H
#define SYSTEM_IDS_H

#include "DelegateMQ.h"

namespace SystemTopic {
    // Topic names for different shapes
    inline constexpr const char* const Square = "Shape/Square";
    inline constexpr const char* const Circle = "Shape/Circle";
    inline constexpr const char* const Triangle = "Shape/Triangle";

    // Remote IDs for Multicast distribution
    enum RemoteId : dmq::DelegateRemoteId {
        SquareId = 200,
        CircleId = 201,
        TriangleId = 202
    };
}

#endif // SYSTEM_IDS_H
