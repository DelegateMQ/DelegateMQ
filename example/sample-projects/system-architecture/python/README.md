# Python Client for DelegateMQ System Architecture Example

This project demonstrates cross-language interoperability between **Python** and a **C++** application using [DelegateMQ](https://github.com/delegate-mq).

While DelegateMQ is a native C++ library, this Python application implements a compatible network client. It uses **ZeroMQ** for transport and **MessagePack** for serialization to exchange commands, sensor data, and status updates seamlessly with the C++ server.

## Overview

- **Server (C++):** A DelegateMQ-based application that publishes sensor data and accepts commands.
  - Location: `DelegateMQ\example\sample-projects\system-architecture\full\server`
- **Client (Python):** This application. It subscribes to data, sends commands, and updates actuator states.
- **Protocol:**
  - **Transport:** TCP via ZeroMQ (`zmq.PAIR` sockets).
  - **Serialization:** MessagePack (Binary serialization).
  - **Header:** 6-byte binary header (Marker + RemoteID + Sequence).

## Prerequisites

1. **Python 3.6+** installed.
2. The **C++ Server Application** must be compiled and ready to run.

## Installation

1. Install the required Python libraries:
   ```bash
   pip install pyzmq msgpack
   ```

## Run

To run the Python client and C++ server applications:

1. Start the C++ server app
2. Run this Python client app `main.py`
3. Python client and C++ server communicate and output data to the console