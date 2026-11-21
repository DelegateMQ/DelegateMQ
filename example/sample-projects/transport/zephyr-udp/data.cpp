#include "data.h"

using namespace std;

dmq::ByteWriter& DataPoint::write(serialize& ms, dmq::ByteWriter& os)
{
    ms.write(os, x);
    ms.write(os, y);
    return os;
}

dmq::ByteReader& DataPoint::read(serialize& ms, dmq::ByteReader& is)
{
    ms.read(is, x);
    ms.read(is, y);
    return is;
}

dmq::ByteWriter& Data::write(serialize& ms, dmq::ByteWriter& os)
{
    ms.write(os, dataPoints);
    ms.write(os, msg);
    return os;
}

dmq::ByteReader& Data::read(serialize& ms, dmq::ByteReader& is)
{
    ms.read(is, dataPoints);
    ms.read(is, msg);
    return is;
}

dmq::ByteWriter& DataAux::write(serialize& ms, dmq::ByteWriter& os)
{
    ms.write(os, auxMsg);
    return os;
}

dmq::ByteReader& DataAux::read(serialize& ms, dmq::ByteReader& is)
{
    ms.read(is, auxMsg);
    return is;
}

