# main.py — Python client for DelegateMQ cross-language interop demonstration.
#
# This application receives sensor data from and sends commands to a C++ server 
# using the DelegateMQ native core DLL and the DmqDataBus Python wrapper.
#
# Requirements:
# - DmqInterop.dll (Windows) or libDmqInterop.so (Linux) must be built.
# - pip install msgpack

import sys
import os
import time
import msgpack

# Add interop/python to path so we can find dmq_databus
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../interop/python")))

from dmq_databus import DmqDataBus

# 1. Configuration: Match these with the C++ Server settings
SERVER_HOST = "127.0.0.1"
DATA_RECV_PORT = 8000 # Server's PUB port
CMD_SEND_PORT = 8001  # Server's SUB port

# Topic IDs (Must match C++ and C# definitions)
SENSOR_DATA_ID = 100
COMMAND_ID = 101

def find_dll():
    """Helper to find the DmqInterop library in common build locations."""
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../interop/native/build/bin"))
    
    if sys.platform == "win32":
        # Check Release then Debug on Windows
        paths = [
            os.path.join(base_dir, "Release/DmqInterop.dll"),
            os.path.join(base_dir, "Debug/DmqInterop.dll")
        ]
    else:
        # Check standard Linux build location
        paths = [
            os.path.join(base_dir, "libDmqInterop.so"),
            os.path.join(base_dir, "Debug/libDmqInterop.so") # In case it's in a subfolder
        ]

    for p in paths:
        if os.path.exists(p):
            return p
    return None

def on_sensor_data(payload):
    """Callback invoked when sensor data arrives from the C++ server."""
    # SensorData: [id, value] (Matches C++ MSGPACK_DEFINE order)
    data = msgpack.unpackb(payload)
    print(f"[RECV] SensorData: id={data[0]} val={data[1]}")

def main():
    print("Starting Python Interop Sample...")
    
    # 2. Locate and load the native interop core
    dll_path = find_dll()
    if not dll_path:
        print("ERROR: Could not find DmqInterop library. Please build interop/native first.")
        return

    try:
        bus = DmqDataBus(dll_path)
    except Exception as e:
        print(f"ERROR: Failed to load native library: {e}")
        return

    # 3. Setup: Register interest and start transport
    # Note: register_callback can be called before or after start()
    bus.register_callback(SENSOR_DATA_ID, on_sensor_data)
    
    try:
        # Start the background native receive loop
        bus.start(SERVER_HOST, DATA_RECV_PORT, CMD_SEND_PORT)
    except Exception as e:
        print(f"ERROR: {e}")
        return

    # 4. Main Loop: Toggle server polling rate every 5 seconds
    print("Running polling rate toggle loop (Press Ctrl+C to exit)...")
    polling_rate = 250
    try:
        while True:
            # Command: [pollingRateMs]
            print(f"[SEND] Command: pollingRateMs={polling_rate}")
            
            # Send serialized command to the C++ server via the DLL
            bus.send(COMMAND_ID, [polling_rate])
            
            # Toggle between 250 and 1000ms
            polling_rate = 1000 if polling_rate == 250 else 250
            
            time.sleep(5)
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        # 5. Cleanup: Shut down native threads and close ports
        bus.stop()

if __name__ == "__main__":
    main()
