#ifndef SENSOR_NODE_HAL_H
#define SENSOR_NODE_HAL_H

/// @file Hal.h
/// @brief The hardware the sensor node uses, as small interfaces. Each
/// platform provides them: platform/desktop with stand-ins (a generated
/// signal, console output), platform/esp32s3 with the board's peripherals.

#include "DelegateMQ.h"
#include <cstddef>
#include <cstdint>

namespace sensornode {

/// The fast signal, read in blocks (like an ADC filling a DMA buffer)
class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    /// The next n samples at rateHz. Successive calls continue the signal.
    virtual void Read(float* out, size_t n, uint32_t rateHz) = 0;
};

class ITemperatureSensor {
public:
    virtual ~ITemperatureSensor() = default;
    virtual float ReadCelsius() = 0;
};

class ILed {
public:
    virtual ~ILed() = default;
    virtual void Set(uint8_t r, uint8_t g, uint8_t b) = 0;
};

/// Touch pads. The platform posts each change to the sink from its interrupt
/// (or, on the desktop, from a stand-in thread): Post(pad, touched).
class ITouchInput {
public:
    using Sink = dmq::util::IsrDelegate<void(uint8_t, uint8_t)>;
    virtual ~ITouchInput() = default;
    virtual void Start(Sink& sink) = 0;
    virtual void Stop() = 0;
};

/// Everything the node needs from its platform
struct Platform {
    ISignalSource& signal;
    ITemperatureSensor& temperature;
    ILed& led;
    ITouchInput& touch;
};

} // namespace sensornode

#endif // SENSOR_NODE_HAL_H
