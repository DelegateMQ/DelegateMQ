import subprocess
import os
import sys
import time

def main():
    # Define paths to executables relative to this script
    base_path = os.path.dirname(os.path.abspath(__file__))
    
    # Optional tools to launch first if they exist
    tools = [
        {
            "name": "DMQ Monitor", 
            "path": "../../tools/build/Release/dmq-monitor.exe",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Thread", 
            "path": "../../tools/build/Release/dmq-thread.exe",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Spy", 
            "path": "../../tools/build/Release/dmq-spy.exe",
            "args": ["9999", "--log", "spy_logs.txt"]
        },
    ]

    apps = [
        {"name": "Safety",     "path": "build/safety/Debug/cellutron_safety.exe",     "args": []},
        {"name": "Controller", "path": "build/controller/Debug/cellutron_controller.exe", "args": []},
        {"name": "GUI",        "path": "build/gui/Debug/cellutron_gui.exe",           "args": []},
    ]

    processes = []

    print("--- Starting Cellutron Distributed System ---")

    # Delete spy_logs.txt before launch to prevent slowdown from large log files
    spy_log = os.path.normpath(os.path.join(base_path, "../../tools/build/Release/spy_logs.txt"))
    if os.path.exists(spy_log):
        os.remove(spy_log)
        print(f"Deleted {spy_log}")

    # 1. Launch Tools (Monitor/Spy)
    for tool in tools:
        exe_path = os.path.normpath(os.path.join(base_path, tool["path"]))
        if os.path.exists(exe_path):
            print(f"Launching {tool['name']}...")
            p = subprocess.Popen(
                [exe_path] + tool["args"],
                creationflags=subprocess.CREATE_NEW_CONSOLE,
                cwd=os.path.dirname(exe_path)
            )
            processes.append(p)
            time.sleep(0.5)
        else:
            print(f"INFO: {tool['name']} not found at {exe_path}. Skipping.")

    # 2. Launch Cellutron Apps
    for app in apps:
        exe_path = os.path.normpath(os.path.join(base_path, app["path"]))
        
        if not os.path.exists(exe_path):
            print(f"ERROR: Could not find {app['name']} at {exe_path}")
            print("Did you build the project first?")
            # Don't exit here, maybe other apps exist
            continue

        print(f"Launching {app['name']}...")
        
        # Start each process in a new console window
        p = subprocess.Popen(
            [exe_path] + app["args"], 
            creationflags=subprocess.CREATE_NEW_CONSOLE,
            cwd=os.path.dirname(exe_path)
        )
        processes.append(p)
        
        # Small delay to ensure sockets are ready before next app starts
        time.sleep(0.5)

    if not processes:
        print("ERROR: No processes were started.")
        sys.exit(1)

    print("\nAll applications started.")
    print("Close the console windows or terminate this script to stop.")

    try:
        # Keep the script alive while processes are running
        while any(p.poll() is None for p in processes):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        for p in processes:
            p.terminate()

if __name__ == "__main__":
    main()
