#ifndef SENSOR_STRINGIFIERS_H
#define SENSOR_STRINGIFIERS_H

/// @file SensorStringifiers.h
/// @brief Text for each sensor-node topic, for DelegateMQ's diagnostic tools:
/// dmq-spy shows a message's value through the topic's stringifier ("?" without
/// one). Written as "name: value" pairs, which dmq-spy --plotjuggler turns into
/// named series (<sender>/<topic>/<name>; see tools/TOOLS.md).
///
/// Call RegisterSensorStringifiers() once, in a program that feeds the tools
/// (sensor_node --tools, sensor_pc --tools).

#include "SensorMessages.h"
#include "SensorTopics.h"
#include "DelegateMQ.h"

namespace sensornode {

namespace detail {
    inline dmq::xstring StringifyStats(const SignalStats& m) {
        dmq::xostringstream os;
        os << "rate: " << m.rateHz << " Hz n: " << m.count << " min: " << m.min
           << " max: " << m.max << " mean: " << m.mean;
        return os.str();
    }
    inline dmq::xstring StringifyTemperature(const Temperature& m) {
        dmq::xostringstream os;
        os << "temp: " << m.celsius << " C";
        return os.str();
    }
    inline dmq::xstring StringifyTouch(const Touch& m) {
        dmq::xostringstream os;
        os << "pad: " << static_cast<unsigned>(m.pad) << " touched: " << static_cast<unsigned>(m.touched);
        return os.str();
    }
    inline dmq::xstring StringifyStatus(const NodeStatus& m) {
        dmq::xostringstream os;
        os << "up(ms): " << m.uptimeMs << " rate: " << m.rateHz << " interval(ms): " << m.intervalMs
           << " led: " << static_cast<unsigned>(m.ledR) << "," << static_cast<unsigned>(m.ledG) << ","
           << static_cast<unsigned>(m.ledB) << " commands: " << m.commands << " dropped: " << m.netDropped
           << " crossCore(us): " << m.crossCoreUs << " sameCore(us): " << m.sameCoreUs;
        return os.str();
    }
    inline dmq::xstring StringifyRate(const SetRate& m) {
        dmq::xostringstream os;
        os << "seq: " << m.seq << " rate: " << m.rateHz << " Hz";
        return os.str();
    }
    inline dmq::xstring StringifyLed(const SetLed& m) {
        dmq::xostringstream os;
        os << "seq: " << m.seq << " r: " << static_cast<unsigned>(m.r) << " g: " << static_cast<unsigned>(m.g)
           << " b: " << static_cast<unsigned>(m.b);
        return os.str();
    }
    inline dmq::xstring StringifyStart(const StartPublishing& m) {
        dmq::xostringstream os;
        os << "seq: " << m.seq << " interval(ms): " << m.intervalMs << " session: " << std::hex << m.session;
        return os.str();
    }
}

inline void RegisterSensorStringifiers()
{
    using dmq::databus::DataBus;
    using dmq::MakeDelegate;
    DataBus::RegisterStringifier<SignalStats>(SensorTopic::SignalStats, MakeDelegate(&detail::StringifyStats));
    DataBus::RegisterStringifier<Temperature>(SensorTopic::Temperature, MakeDelegate(&detail::StringifyTemperature));
    DataBus::RegisterStringifier<Touch>(SensorTopic::Touch, MakeDelegate(&detail::StringifyTouch));
    DataBus::RegisterStringifier<NodeStatus>(SensorTopic::Status, MakeDelegate(&detail::StringifyStatus));
    DataBus::RegisterStringifier<SetRate>(SensorTopic::SetRate, MakeDelegate(&detail::StringifyRate));
    DataBus::RegisterStringifier<SetLed>(SensorTopic::SetLed, MakeDelegate(&detail::StringifyLed));
    DataBus::RegisterStringifier<StartPublishing>(SensorTopic::Start, MakeDelegate(&detail::StringifyStart));
}

} // namespace sensornode

#endif // SENSOR_STRINGIFIERS_H
