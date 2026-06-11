import subprocess
import os
import sys
import time
import argparse
import platform
import shutil

IS_WINDOWS = platform.system() == "Windows"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

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

def launch_process(exe_path, args, cwd):
    """Launches a process in a new window if possible, otherwise falls back to current console."""
    popen_args = {"cwd": cwd}
    
    if IS_WINDOWS:
        popen_args["creationflags"] = subprocess.CREATE_NEW_CONSOLE
        return subprocess.Popen([exe_path] + args, **popen_args)
    
    # Linux: Try to find a terminal emulator to mimic Windows behavior
    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        # List of terminal emulators and their command-to-execute flags
        terminals = [
            ("gnome-terminal", ["--wait", "--"]),
            ("konsole", ["-e"]),
            ("xfce4-terminal", ["-e"]),
            ("xterm", ["-e"]),
        ]
        
        for term, flags in terminals:
            if shutil.which(term):
                # Special handling for gnome-terminal to ensure it opens a new window
                if term == "gnome-terminal":
                    full_cmd = [term, "--window"] + flags + [exe_path] + args
                else:
                    full_cmd = [term] + flags + [exe_path] + args
                
                try:
                    return subprocess.Popen(full_cmd, **popen_args)
                except Exception as e:
                    print(f"DEBUG: Failed to launch via {term}: {e}")
                    continue

    # Fallback: Launch in the same console
    return subprocess.Popen([exe_path] + args, **popen_args)

def main():
    parser = argparse.ArgumentParser(description="Launch Cellutron Distributed System.")
    parser.add_argument(
        "--config", 
        choices=["Debug", "Release"], 
        default="Release",
        help="Build configuration to launch for main apps (default: Release)"
    )
    parser.add_argument(
        "--log", 
        choices=["on", "off"], 
        default="on",
        help="Enable or disable DMQ Spy logging to spy_logs.txt (default: on)"
    )
    args_parsed = parser.parse_args()
    config = args_parsed.config
    log_enabled = args_parsed.log == "on"

    # Linux Cleanup: Kill any orphan processes from previous runs
    if (not IS_WINDOWS) and (os.environ.get("SKIP_CLEANUP") != "1"):
        print("Cleaning up existing Cellutron processes...")
        try:
            # pkill -9 -f matches the full command line and kills forcefully
            subprocess.run(["pkill", "-9", "-f", "cellutron_"], stderr=subprocess.DEVNULL)
            subprocess.run(["pkill", "-9", "-f", "dmq-"], stderr=subprocess.DEVNULL)
            time.sleep(1) # Wait for sockets to be released
        except: pass

    # Define paths to executables relative to this script
    base_path = os.path.dirname(os.path.abspath(__file__))
    
    # Tool definitions (without fixed config path)
    spy_args = ["9999"]
    if log_enabled:
        spy_args += ["--log", "spy_logs.txt"]

    tools_definitions = [
        {
            "name": "DMQ Monitor", 
            "exe": f"dmq-monitor{EXE_SUFFIX}",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Thread", 
            "exe": f"dmq-thread{EXE_SUFFIX}",
            "args": ["9998", "--multicast", "239.1.1.1"]
        },
        {
            "name": "DMQ Spy", 
            "exe": f"dmq-spy{EXE_SUFFIX}",
            "args": spy_args
        },
    ]

    # Define app search paths
    app_definitions = [
        {"name": "Safety",     "exe": f"cellutron_safety{EXE_SUFFIX}"},
        {"name": "Controller", "exe": f"cellutron_controller{EXE_SUFFIX}"},
        {"name": "GUI",        "exe": f"cellutron_gui{EXE_SUFFIX}"},
    ]

    apps_to_launch = []
    for app in app_definitions:
        # Search for the app in various common build locations
        search_paths = [
            # Global build (from example/cellutron)
            f"build/{app['name'].lower()}/{config}/{app['exe']}",
            f"build/{app['name'].lower()}/{app['exe']}",
            # Local build (from example/cellutron/app)
            f"{app['name'].lower()}/build/{config}/{app['exe']}",
            f"{app['name'].lower()}/build/{app['exe']}",
        ]
        
        exe_path = get_newest_exe(base_path, search_paths)
        if exe_path:
            apps_to_launch.append({"name": app["name"], "path": exe_path, "args": []})
        else:
            print(f"ERROR: Could not find {app['name']} ({app['exe']}) in any expected build location.")
            print(f"Searched in: {search_paths}")

    processes = []

    print(f"--- Starting Cellutron Distributed System ---")
    print(f"  Apps Config: {config}")
    print(f"  Tools: Auto-selecting newest (Debug/Release)")

    # 1. Launch Tools (Monitor/Spy/Thread)
    for tool in tools_definitions:
        # Search for the newest tool in either Debug or Release
        search_paths = [
            f"../../tools/build/Release/{tool['exe']}",
            f"../../tools/build/Debug/{tool['exe']}"
        ]
        if not IS_WINDOWS:
            search_paths.append(f"../../tools/build/{tool['exe']}")

        exe_path = get_newest_exe(base_path, search_paths)

        if exe_path:
            # Special case: Clean up log for DMQ Spy in its specific directory
            if "dmq-spy" in tool["exe"] and log_enabled:
                spy_log = os.path.join(os.path.dirname(exe_path), "spy_logs.txt")
                if os.path.exists(spy_log):
                    try:
                        os.remove(spy_log)
                        print(f"Deleted {spy_log}")
                    except: pass

            config_found = "Release" if "Release" in exe_path else "Debug" if "Debug" in exe_path else "Standard"
            print(f"Launching {tool['name']} ({config_found})...")
            
            p = launch_process(exe_path, tool["args"], os.path.dirname(exe_path))
            processes.append(p)
            time.sleep(0.5)
        else:
            print(f"INFO: {tool['name']} not found in Debug or Release. Skipping.")

    # 2. Launch Cellutron Apps
    # First, verify all core apps exist to avoid partial system starts
    missing_apps = [app["name"] for app in apps_to_launch if not os.path.exists(app["path"])]
    if missing_apps:
        print(f"ERROR: The following core applications are missing: {', '.join(missing_apps)}")
        print("Please build the project first.")
        sys.exit(1)

    # Require exactly 3 core apps for a complete system
    if len(apps_to_launch) < 3:
        print("ERROR: Not all core applications (Safety, Controller, GUI) were found.")
        sys.exit(1)

    for app in apps_to_launch:
        print(f"Launching {app['name']}...")
        
        # Start each process
        p = launch_process(app["path"], app["args"], os.path.dirname(app["path"]))
        processes.append(p)
        time.sleep(0.5)

    if not processes:
        print("ERROR: No processes were started.")
        sys.exit(1)

    print("\nAll applications started.")
    print("Terminate this script (Ctrl+C) to stop all processes.")

    try:
        while any(p.poll() is None for p in processes):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        for p in processes:
            p.terminate()
        
        # Linux: Aggressively clean up to prevent orphan processes from terminal wrappers
        if not IS_WINDOWS:
            try:
                subprocess.run(["pkill", "-9", "-f", "cellutron_"], stderr=subprocess.DEVNULL)
                subprocess.run(["pkill", "-9", "-f", "dmq-"], stderr=subprocess.DEVNULL)
            except: pass

if __name__ == "__main__":
    main()
