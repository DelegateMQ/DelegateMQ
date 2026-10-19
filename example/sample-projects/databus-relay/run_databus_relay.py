"""
Script: run_databus_relay.py
Description:
    Launches the 3-node DataBus relay sample, each node in its own console:

        sensor (A) <--- UDP ---> gateway (B) <--- local IPC ---> monitor (C)

    Local IPC is a Win32 named pipe on Windows and a Unix domain socket on Linux.
    Start order: gateway first (on Windows it creates the named pipe), then
    monitor (connects to the pipe), then sensor. On Linux any order works; the
    same order is used anyway.

    Build first: scripts/03_generate_samples.py + scripts/04_build_samples.py from the repo
    root, or `cmake -B build . && cmake --build build --config Release` in each
    of gateway/, monitor/ and sensor/.

Usage:
    > python run_databus_relay.py                 # run until Ctrl+C
    > python run_databus_relay.py --duration 30   # each node exits after 30 s
    > python run_databus_relay.py --config Debug
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

IS_WINDOWS = platform.system() == "Windows"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

# Start order matters on Windows: the gateway is the pipe server.
NODES = [
    {"name": "Gateway", "dir": "gateway", "exe": "delegate_databus_relay_gateway_app"},
    {"name": "Monitor", "dir": "monitor", "exe": "delegate_databus_relay_monitor_app"},
    {"name": "Sensor",  "dir": "sensor",  "exe": "delegate_databus_relay_sensor_app"},
]


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
        terminals = [
            ("gnome-terminal", ["--wait", "--"]),
            ("konsole", ["-e"]),
            ("xfce4-terminal", ["-e"]),
            ("xterm", ["-e"]),
        ]
        for term, flags in terminals:
            if shutil.which(term):
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
    parser = argparse.ArgumentParser(description="Launch the DataBus relay sample (sensor/gateway/monitor).")
    parser.add_argument(
        "--config",
        choices=["Debug", "Release"],
        default="Release",
        help="Preferred build configuration (default: Release). Falls back to the "
             "other configuration if that is the only one built."
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=0,
        help="Seconds each node runs before exiting on its own (default: 0 = until Ctrl+C)."
    )
    args = parser.parse_args()

    base_path = os.path.dirname(os.path.abspath(__file__))
    other_config = "Debug" if args.config == "Release" else "Release"

    to_launch = []
    for node in NODES:
        exe = node["exe"] + EXE_SUFFIX
        # Preferred config first; the other config (or a single-config build) only as fallback.
        exe_path = get_newest_exe(base_path, [f"{node['dir']}/build/{args.config}/{exe}"])
        if not exe_path:
            exe_path = get_newest_exe(base_path, [
                f"{node['dir']}/build/{other_config}/{exe}",
                f"{node['dir']}/build/{exe}",
            ])
        if not exe_path:
            print(f"ERROR: {node['name']} ({exe}) not found under {node['dir']}/build/. Build the sample first.")
            sys.exit(1)
        to_launch.append((node["name"], exe_path))

    print("--- Starting DataBus Relay Sample ---")
    ipc = "named pipe" if IS_WINDOWS else "Unix socket"
    print(f"  sensor (A) <--- UDP ---> gateway (B) <--- {ipc} ---> monitor (C)")
    print(f"  Duration: {args.duration} s" if args.duration > 0 else "  Duration: until Ctrl+C")

    node_args = [str(args.duration)] if args.duration > 0 else []
    processes = []
    for name, exe_path in to_launch:
        print(f"Launching {name}: {exe_path}")
        processes.append(launch_process(exe_path, node_args, os.path.dirname(exe_path)))
        time.sleep(1.0)  # Windows: gateway must create the pipe before the monitor connects

    print("\nAll nodes started.")
    print("Terminate this script (Ctrl+C) to stop all processes.")

    try:
        while any(p.poll() is None for p in processes):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        for p in processes:
            p.terminate()


if __name__ == "__main__":
    main()
