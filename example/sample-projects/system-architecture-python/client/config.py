from enum import IntEnum

# ------------------------------------------------------------------------------
# Network Configuration
# ------------------------------------------------------------------------------
SERVER_IP = "localhost"
SEND_PORT = 5556  # Port the C++ Server Listens on (We send here)
RECV_PORT = 5555  # Port the C++ Server Publishes on (We receive here)

# Wire header (see DmqHeader.h): marker, node ID, protocol version
DMQ_MARKER = 0xAA55
DMQ_VERSION = 1
DMQ_HEADER_SIZE = 10
DMQ_NODE_ID = 0   # This client's node ID (1-255). 0 = unset.

# ------------------------------------------------------------------------------
# Protocol IDs
# ------------------------------------------------------------------------------
# These IDs must match the C++ "RemoteIds.h" file exactly.
class RemoteId(IntEnum):
    ACK         = 0  # Internal DelegateMQ ACK ID
    ALARM       = 1
    DATA        = 2
    COMMAND     = 3
    ACTUATOR    = 4