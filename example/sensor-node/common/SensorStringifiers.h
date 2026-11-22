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
#include <cstdio>

namespace sensornode {

namespace detail {
    // snprintf, not a stringstream: these run on the boards too, and
    // iostreams cost a lot of flash. %g prints a float as a stream would.
    template <typename... T>
    inline dmq::xstring Format(const char* fmt, T... args) {
        char text[192];
        snprintf(text, sizeof(text), fmt, args...);
        return text;
    }

    inline dmq::xstring StringifyStats(const SignalStats& m) {
        return Format("rate: %lu Hz n: %lu min: %g max: %g mean: %g",
                      (unsigned long)m.rateHz, (unsigned long)m.count, (double)m.min, (double)m.max, (double)m.mean);
    }
    inline dmq::xstring StringifyTemperature(const Temperature& m) {
        return Format("temp: %g C", (double)m.celsius);
    }
    inline dmq::xstring StringifyTouch(const Touch& m) {
        return Format("pad: %u touched: %u", (unsigned)m.pad, (unsigned)m.touched);
    }
    inline dmq::xstring StringifyStatus(const NodeStatus& m) {
        return Format("up(ms): %lu rate: %lu interval(ms): %lu led: %u,%u,%u commands: %lu dropped: %lu "
                      "crossCore(us): %g sameCore(us): %g",
                      (unsigned long)m.uptimeMs, (unsigned long)m.rateHz, (unsigned long)m.intervalMs,
                      (unsigned)m.ledR, (unsigned)m.ledG, (unsigned)m.ledB,
                      (unsigned long)m.commands, (unsigned long)m.netDropped,
                      (double)m.crossCoreUs, (double)m.sameCoreUs);
    }
    inline dmq::xstring StringifyRate(const SetRate& m) {
        return Format("seq: %lu rate: %lu Hz", (unsigned long)m.seq, (unsigned long)m.rateHz);
    }
    inline dmq::xstring StringifyLed(const SetLed& m) {
        return Format("seq: %lu r: %u g: %u b: %u", (unsigned long)m.seq, (unsigned)m.r, (unsigned)m.g, (unsigned)m.b);
    }
    inline dmq::xstring StringifyStart(const StartPublishing& m) {
        return Format("seq: %lu interval(ms): %lu session: %lx",
                      (unsigned long)m.seq, (unsigned long)m.intervalMs, (unsigned long)m.session);
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
