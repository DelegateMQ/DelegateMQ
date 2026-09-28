"""
Launch Pumptron.

Simulation (default) -- starts the FreeRTOS-simulator controller and the
operator console, each in its own terminal window, linked over UDP:
    python run_pumptron.py

Real hardware -- the controller is the flashed STM32F4 Discovery board, so only
the operator console is started, on the given serial port:
    python run_pumptron.py --serial COM5
    python run_pumptron.py --serial /dev/ttyUSB0 --baud 115200

Headless end-to-end check instead of the interactive console (either mode):
    python run_pumptron.py --selftest
    python run_pumptron.py --serial COM5 --selftest

Watch all DataBus traffic, including everything received from the controller,
in dmq-spy -- and optionally plot it live in PlotJuggler (either mode):
    python run_pumptron.py --spy
    python run_pumptron.py --serial COM5 --plotjuggler

Publish pump status, telemetry and alarms to an MQTT broker (default
tcp://127.0.0.1:1883), and optionally accept pump commands from it:
    python run_pumptron.py --mqtt
    python run_pumptron.py --serial COM5 --mqtt tcp://192.168.1.10:1883 --mqtt-control

Build first with 03_generate_samples.py + 04_build_samples.py, or from this
directory: cmake -B build . && cmake --build build --config <Debug|Release>
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
BASE_PATH = os.path.dirname(os.path.abspath(__file__))


def find_exe(name, config):
    """Newest matching executable across the multi-config (Windows) and
    single-config (Linux) output layouts of example/pumptron/build."""
    candidates = [
        os.path.join(BASE_PATH, "build", "bin", config, name + EXE_SUFFIX),
        os.path.join(BASE_PATH, "build", "bin", name + EXE_SUFFIX),
    ]
    existing = [p for p in candidates if os.path.isfile(p)]
    if not existing:
        return None, candidates
    return max(existing, key=os.path.getmtime), candidates


def find_tool(name):
    """Newest dmq tool executable from tools/build (Release or Debug)."""
    tools_build = os.path.normpath(os.path.join(BASE_PATH, "..", "..", "tools", "build"))
    candidates = [
        os.path.join(tools_build, "Release", name + EXE_SUFFIX),
        os.path.join(tools_build, "Debug", name + EXE_SUFFIX),
        os.path.join(tools_build, name + EXE_SUFFIX),
    ]
    existing = [p for p in candidates if os.path.isfile(p)]
    return max(existing, key=os.path.getmtime) if existing else None


def launch_in_new_terminal(exe_path, args):
    """Start a process in its own console window (Windows) or terminal emulator
    (Linux desktop); fall back to the current console. The operator console is
    a full-screen FTXUI app, so each process needs a terminal of its own."""
    cwd = os.path.dirname(exe_path)
    if IS_WINDOWS:
        return subprocess.Popen([exe_path] + args, cwd=cwd, creationflags=subprocess.CREATE_NEW_CONSOLE)

    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        terminals = [
            ("gnome-terminal", ["--window", "--wait", "--"]),
            ("konsole", ["-e"]),
            ("xfce4-terminal", ["-e"]),
            ("xterm", ["-e"]),
        ]
        for term, flags in terminals:
            if shutil.which(term):
                try:
                    return subprocess.Popen([term] + flags + [exe_path] + args, cwd=cwd)
                except Exception as e:
                    print(f"DEBUG: Failed to launch via {term}: {e}")

    return subprocess.Popen([exe_path] + args, cwd=cwd)


def kill_orphans():
    """Linux: remove leftovers from a previous run so the UDP ports are free."""
    if IS_WINDOWS or os.environ.get("SKIP_CLEANUP") == "1":
        return
    subprocess.run(["pkill", "-9", "-f", "pumptron_"], stderr=subprocess.DEVNULL)
    time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description="Launch Pumptron (simulator or real STM32F4 hardware).")
    parser.add_argument("--serial", metavar="PORT",
                        help="Controller is a real STM32F4 board on this serial port "
                             "(e.g. COM5, /dev/ttyUSB0). Only the operator console is started.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--config", choices=["Debug", "Release"], default="Debug",
                        help="Build configuration to launch (default: Debug -- see PUMPTRON.md for "
                             "the known Release-build FreeRTOS Windows simulator stall)")
    parser.add_argument("--selftest", action="store_true",
                        help="Run the headless end-to-end self-test in this console instead of the UI; "
                             "exits with its result")
    parser.add_argument("--spy", action="store_true",
                        help="Open dmq-spy and have the GUI mirror its DataBus traffic to it -- including "
                             "every message received from the controller over the link")
    parser.add_argument("--plotjuggler", action="store_true",
                        help="Like --spy, and dmq-spy also forwards numeric values to PlotJuggler's UDP "
                             "Server (127.0.0.1:9870) for live plots. See tools/TOOLS.md")
    parser.add_argument("--mqtt", nargs="?", const="tcp://127.0.0.1:1883", metavar="BROKER_URI",
                        help="Publish pump status/telemetry/alarms to this MQTT broker "
                             "(default tcp://127.0.0.1:1883). See PUMPTRON.md, 'MQTT Gateway'")
    parser.add_argument("--mqtt-prefix", default="pumptron",
                        help="MQTT topic prefix (default: pumptron)")
    parser.add_argument("--mqtt-control", action="store_true",
                        help="Also accept pump commands on <prefix>/pump/cmd/set. This is a remote "
                             "control path into the pump: use only on a trusted broker")
    args = parser.parse_args()

    simulate = args.serial is None
    use_spy = args.spy or args.plotjuggler
    if args.mqtt_control and not args.mqtt:
        parser.error("--mqtt-control requires --mqtt")

    spy_exe = None
    if use_spy:
        spy_exe = find_tool("dmq-spy")
        if not spy_exe:
            print("ERROR: dmq-spy not found under tools/build. Build the tools first (see tools/TOOLS.md).")
            sys.exit(1)

    gui_exe, gui_searched = find_exe("pumptron_gui", args.config)
    if not gui_exe:
        print(f"ERROR: pumptron_gui not found. Searched: {gui_searched}")
        print("Build the project first (see PUMPTRON.md).")
        sys.exit(1)

    ctrl_exe = None
    if simulate:
        ctrl_exe, ctrl_searched = find_exe("pumptron_controller", args.config)
        if not ctrl_exe:
            print(f"ERROR: pumptron_controller not found. Searched: {ctrl_searched}")
            print("Build the project first (see PUMPTRON.md).")
            sys.exit(1)

    gui_args = ["--udp"] if simulate else ["--serial", args.serial, "--baud", str(args.baud)]
    if args.selftest:
        gui_args.append("--selftest")
    if use_spy:
        gui_args.append("--spy")
    if args.mqtt:
        gui_args += ["--mqtt", args.mqtt, "--mqtt-prefix", args.mqtt_prefix]
        if args.mqtt_control:
            gui_args.append("--mqtt-control")

    kill_orphans()

    print("--- Starting Pumptron ---")
    print(f"  Mode:   {'Simulation (FreeRTOS simulator over UDP)' if simulate else f'Hardware (STM32F4 on {args.serial} @ {args.baud})'}")
    print(f"  Config: {args.config}")
    if args.mqtt:
        print(f"  MQTT:   {args.mqtt}, topics under '{args.mqtt_prefix}/'"
              + (f", accepting commands on {args.mqtt_prefix}/pump/cmd/set" if args.mqtt_control else ""))
    if args.plotjuggler:
        print("  PlotJuggler: forwarding enabled (127.0.0.1:9870) -- in PlotJuggler's Streaming")
        print("  panel select UDP Server, port 9870, protocol JSON, then Start.")

    processes = []
    try:
        if use_spy:
            # Only one dmq-spy can listen on port 9999; this one must be the only one.
            spy_args = ["9999"] + (["--plotjuggler"] if args.plotjuggler else [])
            print(f"Launching dmq-spy ({spy_exe} {' '.join(spy_args)})...")
            processes.append(launch_in_new_terminal(spy_exe, spy_args))
            time.sleep(0.5)     # let it bind before traffic starts

        if simulate:
            print(f"Launching Controller ({ctrl_exe})...")
            processes.append(launch_in_new_terminal(ctrl_exe, []))
            time.sleep(1.5)     # let the controller bind its UDP port

        if args.selftest:
            # Run in this console so the PASS/FAIL report and exit code are visible here.
            print(f"Running self-test ({gui_exe} {' '.join(gui_args)})...\n")
            result = subprocess.run([gui_exe] + gui_args, cwd=os.path.dirname(gui_exe)).returncode
            return result

        print(f"Launching GUI ({gui_exe} {' '.join(gui_args)})...")
        gui = launch_in_new_terminal(gui_exe, gui_args)
        processes.append(gui)

        print("\nPumptron started. Quit the GUI with 'q', or press Ctrl+C here to stop everything.")
        while gui.poll() is None:
            time.sleep(1)
        return 0

    except KeyboardInterrupt:
        print("\nShutting down...")
        return 0

    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
        if simulate and not IS_WINDOWS:
            subprocess.run(["pkill", "-9", "-f", "pumptron_controller"], stderr=subprocess.DEVNULL)
        if use_spy and not IS_WINDOWS:
            subprocess.run(["pkill", "-9", "-f", "dmq-spy"], stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    sys.exit(main())
