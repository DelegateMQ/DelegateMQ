#ifndef SYSTEM_MESSAGES_H
#define SYSTEM_MESSAGES_H

#include "port/serialize/serialize/msg_serialize.h"
#include <string>

// Message representing a shape's position and color
struct ShapeMsg : public serialize::I {
    int x = 0;
    int y = 0;
    int size = 10;
    int color = 0; // 0: Blue, 1: Red, 2: Green

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, x);
        ms.write(os, y);
        ms.write(os, size);
        return ms.write(os, color);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, x);
        ms.read(is, y);
        ms.read(is, size);
        return ms.read(is, color);
    }
};

#endif // SYSTEM_MESSAGES_H
