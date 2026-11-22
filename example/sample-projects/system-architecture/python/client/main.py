# Python application shows how Python can communicate with a C++ server application 
# using DelegateMQ remote delegates using ZeroMQ transport and MessagePack.
#
# DelegateMQ is not implemented in Python. DelegateMQ is a C++ library. This Python
# demonstrates how Python communicates to a C++ DelegateMQ remote application using 
# ZeroMQ and MessagePack.
#
# This Python client app example connects to the C++ server app located here:
# DelegateMQ\example\sample-projects\system-architecture\full\server
#
# 1. Start the C++ server app
# 2. Run this Python client app
# 3. Python client and C++ server communicate and output data to console
#
# See DETAILS.md for more information.

import sys
import time
import threading
from messages import AlarmMsg, CommandMsg, DataMsg, ActuatorMsg, AlarmNote
from config import RemoteId
from network_client import NetworkClient

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 30

# ------------------------------------------------------------------------------
# Main Application Handlers
# ------------------------------------------------------------------------------
def handle_alarm(msg: AlarmMsg, note: AlarmNote):
    print(f"\n[RECV] Alarm: {msg} | Note: {note}")

def handle_command(msg: CommandMsg):
    print(f"\n[RECV] Command: {msg}")

def handle_data(msg: DataMsg):
    print(f"\n[RECV] Data: {len(msg.sensors)} Sensors, {len(msg.actuators)} Actuators")
    for s in msg.sensors:
        print(f"  -> Sensor ID {s.id}: Supply={s.supplyV:.2f}V, Reading={s.readingV:.2f}V")

# ------------------------------------------------------------------------------
# Actuator Update Task
# ------------------------------------------------------------------------------
def actuator_update_loop(client: NetworkClient, stop_event: threading.Event):
    """Periodically sends actuator commands."""
    print("Actuator update thread started.")
    
    toggle_state = False
    
    # Loop until the event is set (signaled to stop)
    while not stop_event.is_set():
        # 1. Toggle Position
        toggle_state = not toggle_state
        
        # 2. Create Message (Matches C++: msg.actuatorId = 1...)
        msg1 = ActuatorMsg(actuatorId=1, actuatorPosition=toggle_state)
        msg2 = ActuatorMsg(actuatorId=2, actuatorPosition=toggle_state)

        print(f"[SEND] Actuator Update: {toggle_state}")

        # 3. Send to C++
        client.send(RemoteId.ACTUATOR, msg1)
        client.send(RemoteId.ACTUATOR, msg2)

        # 4. Wait 1 second (Better than sleep: reacts instantly if we stop the app)
        stop_event.wait(1.0)

    print("Actuator update thread stopped.")

# ------------------------------------------------------------------------------
# Entry Point
# ------------------------------------------------------------------------------
if __name__ == "__main__":
    client = NetworkClient()
    
    # Create an event to control the thread
    stop_event = threading.Event()
    
    # Register callbacks
    client.register_callback(RemoteId.ALARM, handle_alarm)
    client.register_callback(RemoteId.COMMAND, handle_command)
    client.register_callback(RemoteId.DATA, handle_data)

    client.start()

    # 1. Start the Actuator Thread
    # We pass the client and the stop_event to the thread function
    actuator_thread = threading.Thread(
        target=actuator_update_loop, 
        args=(client, stop_event), 
        daemon=True
    )
    actuator_thread.start()

    try:
        # Give connection time to establish
        time.sleep(1)

        print("\n--- Sending Start Command to C++ ---")
        cmd = CommandMsg(action=CommandMsg.Action.START, pollTime=500)
        client.send(RemoteId.COMMAND, cmd)

        print(f"\n--- Running (Will auto-exit in {DURATION} seconds) ---")

        # 2. Keep Main Thread Alive for DURATION seconds
        for i in range(DURATION):
            # Check if interrupted during sleep (optional safety)
            if stop_event.is_set():
                break
            time.sleep(1)
            
        print(f"\n{DURATION} seconds elapsed. Exiting application...")

    except KeyboardInterrupt:
        print("\nStopping via KeyboardInterrupt...")

    finally:
        # 3. Cleanup (Runs on both auto-exit and Ctrl+C)
        # Signal the thread to stop
        stop_event.set()
        
        client.stop()
        
        # Wait for thread to finish cleanly
        if actuator_thread.is_alive():
            actuator_thread.join()
            
        print("Application exited.")