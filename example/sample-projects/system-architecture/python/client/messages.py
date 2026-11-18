# Python MessagePack structures match C++ MessagePack structures in:
#   DelegateMQ\example\sample-projects\system-architecture\full\server\common
#
# ------------------------------------------------------------------------------
# Data Structure Synchronization Options for Python and C++ MessagePack Structures
# ------------------------------------------------------------------------------
# 1. Manual Sync (Current):
#    Update this file manually whenever C++ structs change. 
#    - Pros: No extra tooling.
#    - Cons: High risk of field order/type mismatch errors.
#
# 2. Code Generation (Recommended for Production):
#    Define messages in a single YAML/JSON file and write a script to generate 
#    both C++ headers and this Python file.
#    - Pros: Guaranteed sync, single source of truth.

import msgpack
from enum import IntEnum
from typing import List

# ------------------------------------------------------------------------------
# 1. Enums (Scoped to match C++ classes below)
# ------------------------------------------------------------------------------

class ActionEnum(IntEnum):
    START = 0
    STOP = 1

class SourceEnum(IntEnum):
    CLIENT = 0
    SERVER = 1

class AlarmTypeEnum(IntEnum):
    SENSOR_ERROR = 0
    ACTUATOR_ERROR = 1

# ------------------------------------------------------------------------------
# 2. Sub-Objects (ActuatorState & SensorData)
# ------------------------------------------------------------------------------

class ActuatorState:
    """Matches ActuatorState in Actuator.h"""
    def __init__(self, id=0, position=False, voltage=0.0):
        self.id = id
        self.position = position
        self.voltage = voltage

    # Helper: Convert object to raw list [id, pos, volt]
    def to_list(self):
        return [self.id, self.position, self.voltage]

    # Helper: Create object from raw list
    @classmethod
    def from_list(cls, data):
        return cls(data[0], data[1], data[2])

    def to_msgpack(self):
        return msgpack.packb(self.to_list())

    @classmethod
    def from_msgpack(cls, data):
        return cls.from_list(msgpack.unpackb(data))

    def __repr__(self):
        return f"ActuatorState(id={self.id}, pos={self.position}, v={self.voltage:.2f})"


class SensorData:
    """Matches SensorData in Sensor.h"""
    def __init__(self, id=0, supplyV=0.0, readingV=0.0):
        self.id = id
        self.supplyV = supplyV
        self.readingV = readingV

    def to_list(self):
        return [self.id, self.supplyV, self.readingV]

    @classmethod
    def from_list(cls, data):
        return cls(data[0], data[1], data[2])

    def to_msgpack(self):
        return msgpack.packb(self.to_list())

    @classmethod
    def from_msgpack(cls, data):
        return cls.from_list(msgpack.unpackb(data))

    def __repr__(self):
        return f"SensorData(id={self.id}, supply={self.supplyV:.2f}, reading={self.readingV:.2f})"

# ------------------------------------------------------------------------------
# 3. Main Messages
# ------------------------------------------------------------------------------

class CommandMsg:
    """Matches CommandMsg in CommandMsg.h"""
    Action = ActionEnum  # Scope Enum: CommandMsg.Action.START

    def __init__(self, action=Action.START, pollTime=0):
        self.action = action
        self.pollTime = pollTime

    def to_msgpack(self):
        # MSGPACK_DEFINE(action, pollTime) -> Array of size 2
        return msgpack.packb([self.action, self.pollTime])

    @classmethod
    def from_msgpack(cls, data):
        items = msgpack.unpackb(data)
        return cls(cls.Action(items[0]), items[1])

    def __repr__(self):
        return f"CommandMsg(action={self.action.name}, pollTime={self.pollTime})"


class AlarmMsg:
    """Matches AlarmMsg in AlarmMsg.h"""
    Source = SourceEnum      # AlarmMsg.Source.CLIENT
    Alarm = AlarmTypeEnum    # AlarmMsg.Alarm.SENSOR_ERROR

    def __init__(self, source=Source.CLIENT, alarm=Alarm.SENSOR_ERROR):
        self.source = source
        self.alarm = alarm

    def to_msgpack(self):
        # MSGPACK_DEFINE(source, alarm) -> Array of size 2
        return msgpack.packb([self.source, self.alarm])

    @classmethod
    def from_msgpack(cls, data):
        items = msgpack.unpackb(data)
        return cls(cls.Source(items[0]), cls.Alarm(items[1]))

    def __repr__(self):
        return f"AlarmMsg(source={self.source.name}, alarm={self.alarm.name})"


class AlarmNote:
    """Matches AlarmNote in AlarmMsg.h"""
    def __init__(self, note=""):
        self.note = note

    def to_msgpack(self):
        # MSGPACK_DEFINE(note) -> Array of size 1
        return msgpack.packb([self.note])

    @classmethod
    def from_msgpack(cls, data):
        items = msgpack.unpackb(data)
        return cls(items[0])

    def __repr__(self):
        return f"AlarmNote(note='{self.note}')"


class DataMsg:
    """Matches DataMsg in DataMsg.h"""
    def __init__(self, actuators: List[ActuatorState] = None, sensors: List[SensorData] = None):
        self.actuators = actuators if actuators else []
        self.sensors = sensors if sensors else []

    def to_msgpack(self):
        # MSGPACK_DEFINE(actuators, sensors)
        # We must manually convert list[Object] -> list[list[primitives]]
        
        raw_actuators = [a.to_list() for a in self.actuators]
        raw_sensors   = [s.to_list() for s in self.sensors]
        
        # Structure: [ [ [id,pos,v], ... ], [ [id,sup,read], ... ] ]
        return msgpack.packb([raw_actuators, raw_sensors])

    @classmethod
    def from_msgpack(cls, data):
        # unpackb returns lists of lists
        items = msgpack.unpackb(data)
        
        raw_actuators_list = items[0] # List of lists
        raw_sensors_list   = items[1] # List of lists

        # Reconstruct objects
        actuators = [ActuatorState.from_list(x) for x in raw_actuators_list]
        sensors   = [SensorData.from_list(x) for x in raw_sensors_list]
        
        return cls(actuators, sensors)

    def __repr__(self):
        return f"DataMsg(\n  Actuators: {self.actuators},\n  Sensors:   {self.sensors}\n)"

    # ... existing imports ...

class ActuatorMsg:
    """Matches C++ ActuatorMsg struct usage in ClientApp"""
    def __init__(self, actuatorId=0, actuatorPosition=False):
        self.actuatorId = actuatorId
        self.actuatorPosition = actuatorPosition

    def to_msgpack(self):
        # Matches C++: MSGPACK_DEFINE(actuatorId, actuatorPosition)
        # Serializes as a flat array: [id, bool]
        return msgpack.packb([self.actuatorId, self.actuatorPosition])

    @classmethod
    def from_msgpack(cls, data):
        items = msgpack.unpackb(data)
        # items[0] = id, items[1] = position
        return cls(items[0], items[1])

    def __repr__(self):
        return f"ActuatorMsg(id={self.actuatorId}, pos={self.actuatorPosition})"