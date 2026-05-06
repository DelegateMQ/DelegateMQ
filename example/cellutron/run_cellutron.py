import subprocess
import os
import sys
import time
import argparse

def get_newest_exe(base, relative_paths):
    """Returns the full path to the newest existing file among the options."""
    newest_path = None
    newest_time = 0
    for rel in relative_paths:
        full = os.path.normpath(os.path.join(base, rel))
        if os.path.exists(full):
            mtime = os.path.getmtime(full)
            if mtime > newest_time:
                newest_time = mtime
                newest_path = full
    return newest_path

def main():
    parser = argparse.ArgumentParser(description="Launch Cellutron Distributed System.")
    parser.add_argument(
        "--config", 
        choices=["Debug", "Release"], 
        default="Release",
        help="Build configuration to launch for main apps (default: Release)"
    )
    args_parsed = parser.parse_args()
    config = args_parsed.config

    # Define paths to executables relative to this script
    base_path = os.path.dirname(os.path.abspath(__file__))
    
    # Tool definitions (without fixed config path)
    tools_definitions = [
        {
            "name": "DMQ Monitor", 
            "exe": "dmq-monitor.exe",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Thread", 
            "exe": "dmq-thread.exe",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Spy", 
            "exe": "dmq-spy.exe",
            "args": ["9999", "--log", "spy_logs.txt"]
        },
    ]

    apps = [
        {"name": "Safety",     "path": f"build/safety/{config}/cellutron_safety.exe",     "args": []},
        {"name": "Controller", "path": f"build/controller/{config}/cellutron_controller.exe", "args": []},
        {"name": "GUI",        "path": f"build/gui/{config}/cellutron_gui.exe",           "args": []},
    ]

    processes = []

    print(f"--- Starting Cellutron Distributed System ---")
    print(f"  Apps Config: {config}")
    print(f"  Tools: Auto-selecting newest (Debug/Release)")

    # 1. Launch Tools (Monitor/Spy/Thread)
    for tool in tools_definitions:
        # Search for the newest tool in either Debug or Release
        exe_path = get_newest_exe(base_path, [
            f"../../tools/build/Release/{tool['exe']}",
            f"../../tools/build/Debug/{tool['exe']}"
        ])

        if exe_path:
            # Special case: Clean up log for DMQ Spy in its specific directory
            if tool["exe"] == "dmq-spy.exe":
                spy_log = os.path.join(os.path.dirname(exe_path), "spy_logs.txt")
                if os.path.exists(spy_log):
                    try:
                        os.remove(spy_log)
                        print(f"Deleted {spy_log}")
                    except: pass

            config_found = "Release" if "Release" in exe_path else "Debug"
            print(f"Launching {tool['name']} ({config_found})...")
            p = subprocess.Popen(
                [exe_path] + tool["args"],
                creationflags=subprocess.CREATE_NEW_CONSOLE,
                cwd=os.path.dirname(exe_path)
            )
            processes.append(p)
            time.sleep(0.5)
        else:
            print(f"INFO: {tool['name']} not found in Debug or Release. Skipping.")

    # 2. Launch Cellutron Apps
    for app in apps:
        exe_path = os.path.normpath(os.path.join(base_path, app["path"]))
        
        if not os.path.exists(exe_path):
            print(f"ERROR: Could not find {app['name']} at {exe_path}")
            print(f"Did you build the project in {config} mode first?")
            continue

        print(f"Launching {app['name']}...")
        
        # Start each process in a new console window
        p = subprocess.Popen(
            [exe_path] + app["args"], 
            creationflags=subprocess.CREATE_NEW_CONSOLE,
            cwd=os.path.dirname(exe_path)
        )
        processes.append(p)
        time.sleep(0.5)

    if not processes:
        print("ERROR: No processes were started.")
        sys.exit(1)

    print("\nAll applications started.")
    print("Close the console windows or terminate this script to stop.")

    try:
        while any(p.poll() is None for p in processes):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        for p in processes:
            p.terminate()

if __name__ == "__main__":
    main()

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
