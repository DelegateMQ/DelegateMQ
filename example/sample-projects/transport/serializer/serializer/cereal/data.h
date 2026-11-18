#ifndef DATA_H
#define DATA_H

/// @file data.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Data classes sent by remote delegate, serialized with Cereal.

#include "DelegateMQ.h"
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>
#include <string>
#include <vector>

/// The serializer class for a remote delegate signature
template <class Sig>
using SampleSerializer = dmq::serialization::cereal::Serializer<Sig>;

/// 1: the remote delegate takes two arguments, void(Data&, DataAux&)
#define SAMPLE_AUX 1

class DataPoint {
public:
    int x = 0;
    int y = 0;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(x, y);
    }
};

/// @brief Argument data sent to endpoint.
class Data {
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(dataPoints, msg);
    }
};

/// @brief Second argument, to show a remote delegate with several arguments.
class DataAux {
public:
    std::string auxMsg;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(auxMsg);
    }
};

/// The remote delegate's signature
using SampleSig = void(Data&, DataAux&);

#endif
