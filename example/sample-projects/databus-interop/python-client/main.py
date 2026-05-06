"""
main.py — Python DataBus interop client.

Receives DataMsg (sensor/actuator data) from and sends CommandMsg (polling
rate) to the C++ DataBus Interop Server in ../server/.

Uses the generic DmqDataBus library from interop/python/.

Requirements:
    pip install msgpack

Start the C++ server first, then:
    python main.py [duration_seconds]
"""

import os
import sys
import time

# Locate the interop library relative to this file's position in the repo
_INTEROP_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "../../../../interop/python")
)
sys.path.insert(0, _INTEROP_PATH)

from dmq_databus import DmqDataBus
from serializer import pack, unpack

# ---------------------------------------------------------------------------
# Configuration — must match C++ server (SystemIds.h, CMakeLists.txt ports)
# ---------------------------------------------------------------------------
SERVER_HOST    = "127.0.0.1"
DATA_RECV_PORT = 8000    # C++ server PUBs DataMsg here  — we bind and receive
CMD_SEND_PORT  = 8001    # C++ server SUBs CommandMsg here — we send

DATA_MSG_ID = 100        # SystemTopic::DataMsgId
CMD_MSG_ID  = 101        # SystemTopic::CommandMsgId

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 30

# ---------------------------------------------------------------------------
# Message helpers
# ---------------------------------------------------------------------------

def decode_data_msg(payload: bytes):
    """
    Decode DataMsg payload.
    C++ MSGPACK_DEFINE(actuators, sensors)
    Wire: [ [[id, position, voltage], ...], [[id, supplyV, readingV], ...] ]
    """
    fields    = unpack(payload)
    actuators = fields[0]   # list of [id, position, voltage]
    sensors   = fields[1]   # list of [id, supplyV, readingV]
    return actuators, sensors

def encode_command_msg(polling_rate_ms: int) -> bytes:
    """
    Encode CommandMsg payload.
    C++ MSGPACK_DEFINE(pollingRateMs)
    Wire: [pollingRateMs]
    """
    return pack(polling_rate_ms)

# ---------------------------------------------------------------------------
# Callbacks — invoked on the DmqDataBus receive thread
# ---------------------------------------------------------------------------

def on_data_msg(payload: bytes):
    actuators, sensors = decode_data_msg(payload)
    print(f"[RECV] DataMsg: {len(actuators)} actuator(s), {len(sensors)} sensor(s)")
    for a in actuators:
        print(f"  Actuator  id={a[0]}  pos={a[1]:.2f}  voltage={a[2]:.2f}V")
    for s in sensors:
        print(f"  Sensor    id={s[0]}  supply={s[1]:.2f}V  reading={s[2]:.2f}V")

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    bus = DmqDataBus()
    bus.register_callback(DATA_MSG_ID, on_data_msg)
    bus.start(SERVER_HOST, DATA_RECV_PORT, CMD_SEND_PORT)

    print(f"DataBus Python Client — connected to {SERVER_HOST}")
    print(f"  Receiving DataMsg   on port {DATA_RECV_PORT}")
    print(f"  Sending  CommandMsg  on port {CMD_SEND_PORT}")
    print(f"  Duration: {DURATION}s")

    # Give the server a moment before sending the first command
    time.sleep(1)

    bus.send(CMD_MSG_ID, encode_command_msg(500))
    print("[SEND] CommandMsg: pollingRateMs=500")

    time.sleep(DURATION - 1)

    bus.stop()
    print("Done.")
