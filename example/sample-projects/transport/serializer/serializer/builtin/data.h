#ifndef DATA_H
#define DATA_H

/// @file data.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Data classes sent by remote delegate, serialized with DelegateMQ's
/// built-in serialize class (no third-party library). Each class derives
/// from serialize::I and writes/reads its members (data.cpp).

#include "DelegateMQ.h"
#include <string>
#include <vector>
#include <iostream>

/// The serializer class for a remote delegate signature
template <class Sig>
using SampleSerializer = dmq::serialization::serializer::Serializer<Sig>;

/// 1: the remote delegate takes two arguments, void(Data&, DataAux&)
#define SAMPLE_AUX 1

class DataPoint : public serialize::I
{
public:
    int x = 0;
    int y = 0;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override;
    virtual std::istream& read(serialize& ms, std::istream& is) override;
};

/// @brief Argument data sent to endpoint.
class Data : public serialize::I
{
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override;
    virtual std::istream& read(serialize& ms, std::istream& is) override;
};

/// @brief Second argument, to show a remote delegate with several arguments.
class DataAux : public serialize::I
{
public:
    std::string auxMsg;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override;
    virtual std::istream& read(serialize& ms, std::istream& is) override;
};

/// The remote delegate's signature
using SampleSig = void(Data&, DataAux&);

#endif
