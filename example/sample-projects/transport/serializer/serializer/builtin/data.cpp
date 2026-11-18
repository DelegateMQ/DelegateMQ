#include "data.h"

using namespace std;

std::ostream& DataPoint::write(serialize& ms, std::ostream& os)
{
    ms.write(os, x);
    ms.write(os, y);
    return os;
}

std::istream& DataPoint::read(serialize& ms, std::istream& is)
{
    ms.read(is, x);
    ms.read(is, y);
    return is;
}

std::ostream& Data::write(serialize& ms, std::ostream& os)
{
    ms.write(os, dataPoints);
    ms.write(os, msg);
    return os;
}

std::istream& Data::read(serialize& ms, std::istream& is)
{
    ms.read(is, dataPoints);
    ms.read(is, msg);
    return is;
}

std::ostream& DataAux::write(serialize& ms, std::ostream& os)
{
    ms.write(os, auxMsg);
    return os;
}

std::istream& DataAux::read(serialize& ms, std::istream& is)
{
    ms.read(is, auxMsg);
    return is;
}

