#ifndef SENSOR_MESSAGES_H
#define SENSOR_MESSAGES_H

/// @file SensorMessages.h
/// @brief Messages exchanged between the sensor node and the PC client, with
/// DelegateMQ's built-in serializer. Plain fixed-size fields only, so the
/// encoding is the same on the ESP32-S3 and the PC.
///
/// Every message derives from sensornode::MessageBase, which stamps it with a
/// sequence number (seq) and serializes it first.
///
/// Commands (PC -> node) are sent reliably: ACKed, and retried until they are;
/// a retry repeats the command's seq. The node keeps a MonotonicGuard per
/// command and applies one only if its seq is newer than the last it applied,
/// so a retry after a lost ACK isn't applied twice, and an old retry arriving
/// after a newer command doesn't undo it.

#include "MessageBase.h"
#include "port/serialize/serialize/msg_serialize.h"
#include <cstdint>

using sensornode::MessageBase;

/// Statistics over one window of the fast signal (node -> PC)
struct SignalStats : public MessageBase {
    uint32_t rateHz = 0;        // sample rate the window was taken at
    uint32_t count = 0;         // samples in the window (rateHz * window / 1000)
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        ms.write(os, rateHz); ms.write(os, count);
        ms.write(os, min); ms.write(os, max);
        return ms.write(os, mean);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        ms.read(is, rateHz); ms.read(is, count);
        ms.read(is, min); ms.read(is, max);
        return ms.read(is, mean);
    }
};

/// Temperature reading, once a second (node -> PC)
struct Temperature : public MessageBase {
    float celsius = 0.0f;

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, celsius);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, celsius);
    }
};

/// A touch pad changed state (node -> PC)
struct Touch : public MessageBase {
    uint8_t pad = 0;
    uint8_t touched = 0;        // 1 = touched, 0 = released

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        ms.write(os, pad);
        return ms.write(os, touched);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        ms.read(is, pad);
        return ms.read(is, touched);
    }
};

/// The node's state: once a second, and right after each command (node -> PC)
struct NodeStatus : public MessageBase {
    uint32_t uptimeMs = 0;
    uint32_t rateHz = 0;
    uint32_t intervalMs = 0;    // stats window, set by the PC client's Start
    uint8_t ledR = 0, ledG = 0, ledB = 0;
    uint32_t commands = 0;      // commands applied so far
    uint32_t netDropped = 0;    // messages the network thread dropped (queue full)
    float crossCoreUs = 0.0f;   // mean async-wait round trip, ControlThread -> NetThread
    float sameCoreUs = 0.0f;    // mean async-wait round trip, ControlThread -> SampleThread

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        ms.write(os, uptimeMs); ms.write(os, rateHz); ms.write(os, intervalMs);
        ms.write(os, ledR); ms.write(os, ledG); ms.write(os, ledB);
        ms.write(os, commands); ms.write(os, netDropped);
        ms.write(os, crossCoreUs);
        return ms.write(os, sameCoreUs);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        ms.read(is, uptimeMs); ms.read(is, rateHz); ms.read(is, intervalMs);
        ms.read(is, ledR); ms.read(is, ledG); ms.read(is, ledB);
        ms.read(is, commands); ms.read(is, netDropped);
        ms.read(is, crossCoreUs);
        return ms.read(is, sameCoreUs);
    }
};

/// Set the sample rate: 100 to 10000 Hz, a multiple of 100 (PC -> node)
struct SetRate : public MessageBase {
    uint32_t rateHz = 0;

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, rateHz);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, rateHz);
    }
};

/// Set the LED colour (PC -> node)
struct SetLed : public MessageBase {
    uint8_t r = 0, g = 0, b = 0;

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        ms.write(os, r); ms.write(os, g);
        return ms.write(os, b);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        ms.read(is, r); ms.read(is, g);
        return ms.read(is, b);
    }
};

/// Every second: start (or keep) publishing (SensorTopic::Start). A lease:
/// the node stops publishing SensorNode::PC_TIMEOUT after the last one.
/// session: picked at random by each client run. A restarted client counts
/// its seq from 1 again; a new session tells the node to reset its guards.
/// intervalMs: how often to publish SignalStats, i.e. the statistics window
/// (100-10000 ms, a multiple of 100). Temperature and status stay at 1 s.
struct StartPublishing : public MessageBase {
    uint32_t session = 0;
    uint32_t intervalMs = 100;

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        ms.write(os, session);
        return ms.write(os, intervalMs);
    }
    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        ms.read(is, session);
        return ms.read(is, intervalMs);
    }
};

#endif // SENSOR_MESSAGES_H
