#ifndef PUMPTRON_IBOARD_H
#define PUMPTRON_IBOARD_H

namespace pumptron {
namespace board {

/// @brief Status indicators. On the F4 Discovery these map to the four user LEDs.
enum class Indicator {
    POWER,      ///< Orange LD3 - controller up
    RUNNING,    ///< Green  LD4 - pump PRIMING/RUNNING
    FAULT,      ///< Red    LD5 - latched fault
    ACTIVITY    ///< Blue   LD6 - toggles on every telemetry publish
};

/// @brief Hardware abstraction for everything the pump application reads from
///        or drives on the board.
///
/// The pump application (PumpController/PumpModel) depends only on this
/// interface. `SimBoard` backs it with a model on the FreeRTOS simulator;
/// `F4Board` backs it with real STM32F4 Discovery peripherals. Swapping boards
/// is the only application-visible difference between the two targets.
///
/// All methods are called from the pump controller thread only.
class IBoard {
public:
    virtual ~IBoard() = default;

    /// One-time peripheral initialization. Called from the pump thread before
    /// the first control tick.
    virtual void Init() = 0;

    /// Ambient temperature in deg C (F4: MCU die temperature sensor).
    virtual float ReadAmbientTempC() = 0;

    /// Externally sensed vibration in g, as deviation from rest
    /// (F4: LIS3DSH accelerometer). Added on top of the modelled pump vibration.
    virtual float ReadVibrationG() = 0;

    /// True while the local stop button is held (F4: blue user button).
    virtual bool IsLocalStopPressed() = 0;

    virtual void SetIndicator(Indicator indicator, bool on) = 0;
    virtual void ToggleIndicator(Indicator indicator) = 0;

    /// Human-readable board name for logging.
    virtual const char* Name() const = 0;
};

} // namespace board
} // namespace pumptron

#endif
