#ifndef DATA_H
#define DATA_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// serialize class serialized data classes for transport using remote delegates.

#include "DelegateMQ.h"
#include <string.h>
#include <iostream>

class DataPoint : public serialize::I
{
public:
    int x = 0;
    int y = 0;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override;
    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override;
};

/// @brief Argument data sent to endpoint.
class Data : public serialize::I
{
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override;
    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override;
};

class DataAux : public serialize::I
{
public:
    std::string auxMsg;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override;
    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override;
};

#endif
