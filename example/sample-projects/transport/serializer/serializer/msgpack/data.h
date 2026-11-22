#ifndef DATA_H
#define DATA_H

/// @file data.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Data classes sent by remote delegate, serialized with MessagePack
/// (msgpack-c, C++ API).

#include "DelegateMQ.h"
#include <msgpack.hpp>
#include <string>
#include <vector>

/// The serializer class for a remote delegate signature
template <class Sig>
using SampleSerializer = dmq::serialization::msgpack::Serializer<Sig>;

/// 1: the remote delegate takes two arguments, void(Data&, DataAux&)
#define SAMPLE_AUX 1

class DataPoint
{
public:
    int x = 0;
    int y = 0;

    MSGPACK_DEFINE(x, y);
};

/// @brief Argument data sent to endpoint.
class Data
{
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    MSGPACK_DEFINE(dataPoints, msg);
};

/// @brief Second argument, to show a remote delegate with several arguments.
class DataAux
{
public:
    std::string auxMsg;

    MSGPACK_DEFINE(auxMsg);
};

/// The remote delegate's signature
using SampleSig = void(Data&, DataAux&);

#endif
