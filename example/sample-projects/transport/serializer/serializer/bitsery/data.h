#ifndef DATA_H
#define DATA_H

/// @file data.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Data classes sent by remote delegate, serialized with Bitsery.

#include "DelegateMQ.h"
#include <bitsery/bitsery.h>
#include <bitsery/adapter/stream.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/string.h>
#include <string>
#include <vector>

/// The serializer class for a remote delegate signature
template <class Sig>
using SampleSerializer = dmq::serialization::bitsery::Serializer<Sig>;

/// 1: the remote delegate takes two arguments, void(Data&, DataAux&)
#define SAMPLE_AUX 1

class DataPoint {
public:
    int x = 0;
    int y = 0;

    template <typename S>
    void serialize(S& s) {
        s.value4b(x);
        s.value4b(y);
    }
};

/// @brief Argument data sent to endpoint.
class Data {
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    template <typename S>
    void serialize(S& s) {
        s.container(dataPoints, 100);  // Vector, max size hint
        s.text1b(msg, 255);            // String serialization
    }
};

/// @brief Second argument, to show a remote delegate with several arguments.
class DataAux {
public:
    std::string auxMsg;

    template <typename S>
    void serialize(S& s) {
        s.text1b(auxMsg, 255);
    }
};

/// The remote delegate's signature
using SampleSig = void(Data&, DataAux&);

#endif
