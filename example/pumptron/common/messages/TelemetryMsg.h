#ifndef PUMPTRON_TELEMETRY_MSG_H
#define PUMPTRON_TELEMETRY_MSG_H

#include "MessageBase.h"

namespace pumptron {

/// @brief Periodic pump telemetry (controller -> GUI). Sent UNRELIABLE at
///        TELEMETRY_PERIOD. All sensor values travel in one message to keep
///        per-frame overhead low on the serial link.
struct TelemetryMsg : public MessageBase
{
    float rpm = 0.0f;           ///< Motor speed (simulated)
    float flowLpm = 0.0f;       ///< Flow in litres/minute (simulated)
    float pressureBar = 0.0f;   ///< Discharge pressure in bar (simulated)
    float motorTempC = 0.0f;    ///< Motor winding temperature (simulated, seeded by ambient)
    float ambientTempC = 0.0f;  ///< F4: MCU die temperature sensor. Sim: modelled.
    float vibrationG = 0.0f;    ///< F4: LIS3DSH accelerometer + model. Sim: modelled.

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        ms.read(is, rpm);
        ms.read(is, flowLpm);
        ms.read(is, pressureBar);
        ms.read(is, motorTempC);
        ms.read(is, ambientTempC);
        return ms.read(is, vibrationG);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        ms.write(os, rpm);
        ms.write(os, flowLpm);
        ms.write(os, pressureBar);
        ms.write(os, motorTempC);
        ms.write(os, ambientTempC);
        return ms.write(os, vibrationG);
    }
};

} // namespace pumptron

#endif
