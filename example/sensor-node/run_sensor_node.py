"""Start the sensor node system: DelegateMQ's diagnostic tools, the node and the
PC client, each in its own window.

    python run_sensor_node.py                  desktop node + PC client + tools
    python run_sensor_node.py --board 192.168.0.241
                                               PC client + tools, against an ESP32-S3
    python run_sensor_node.py --threadx        ThreadX node (Linux)
    python run_sensor_node.py --zephyr         Zephyr native_sim node (Linux)

The tools (dmq-monitor, dmq-thread, dmq-spy) come from tools/bin; build the tools
first. The node and the client run with --tools, which feeds them: dmq-spy shows
every DataBus message both send and receive, dmq-monitor the nodes and their
link health, dmq-thread their threads. The ThreadX, Zephyr and board nodes have
no diagnostic bridges, so there only the PC client's side shows up.

Executables are taken from a manual build (build/, build-threadx/, build-zephyr/
in this folder) or from build_all.py's out/ tree, whichever is newest.
Ctrl+C here stops everything.
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

IS_WINDOWS = platform.system() == "Windows"
EXE = ".exe" if IS_WINDOWS else ""
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "out", "windows" if IS_WINDOWS else "linux", "example", "sensor-node")


def newest(paths):
    """The most recently built of the existing paths, or None."""
    found = [p for p in paths if os.path.isfile(p)]
    return max(found, key=os.path.getmtime) if found else None


def candidates(build_dirs, exe, config):
    """exe in each build directory, with and without a config subfolder (MSVC)."""
    paths = []
    for d in build_dirs:
        paths += [os.path.join(d, config, exe), os.path.join(d, exe)]
    return paths


def warn_if_stale(exe):
    """Warn when exe is older than the sources it's built from: a stale build
    runs old code (one from before --tools sends nothing to the tools)."""
    newest_src = 0.0
    for root in (os.path.join(HERE, "common"), os.path.join(HERE, "node", "app"),
                 os.path.join(HERE, "node", "platform"), os.path.join(HERE, "pc-client"),
                 os.path.join(REPO, "src", "delegate-mq")):
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.endswith((".h", ".cpp", ".c")):
                    newest_src = max(newest_src, os.path.getmtime(os.path.join(dirpath, f)))
    if os.path.getmtime(exe) < newest_src:
        print(f"WARNING: {os.path.relpath(exe, REPO)} is older than the sources: rebuild it "
              f"(cmake --build {os.path.relpath(os.path.dirname(os.path.dirname(exe)) if os.path.basename(os.path.dirname(exe)) in ('Debug', 'Release') else os.path.dirname(exe), REPO)}"
              f"{' --config Release' if IS_WINDOWS else ''})")


def launch(name, exe, args, cwd):
    """Start exe in a new console window where possible, else in this one."""
    print(f"Launching {name}: {os.path.relpath(exe, REPO)} {' '.join(args)}")
    if IS_WINDOWS:
        return subprocess.Popen([exe] + args, cwd=cwd, creationflags=subprocess.CREATE_NEW_CONSOLE)
    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        for term, flags in [("gnome-terminal", ["--window", "--wait", "--"]), ("konsole", ["-e"]),
                            ("xfce4-terminal", ["-e"]), ("xterm", ["-e"])]:
            if shutil.which(term):
                try:
                    return subprocess.Popen([term] + flags + [exe] + args, cwd=cwd)
                except OSError:
                    continue
    return subprocess.Popen([exe] + args, cwd=cwd)


def main():
    parser = argparse.ArgumentParser(description="Start the sensor node, its PC client and DelegateMQ's tools.")
    parser.add_argument("--config", choices=["Debug", "Release"], default="Release",
                        help="build configuration of the node and client (MSVC builds; default Release)")
    parser.add_argument("--board", metavar="IP",
                        help="don't start a local node: the PC client talks to the ESP32-S3 at this address")
    rtos = parser.add_mutually_exclusive_group()
    rtos.add_argument("--threadx", action="store_true", help="start the ThreadX node (Linux)")
    rtos.add_argument("--zephyr", action="store_true", help="start the Zephyr native_sim node (Linux)")
    parser.add_argument("--no-tools", action="store_true", help="don't start dmq-monitor, dmq-thread or dmq-spy")
    parser.add_argument("--log", choices=["on", "off"], default="on",
                        help="dmq-spy logs to spy_logs.txt in tools/bin (default on)")
    parser.add_argument("--plotjuggler", action="store_true",
                        help="dmq-spy forwards numeric values to PlotJuggler (UDP 127.0.0.1:9870; see tools/TOOLS.md)")
    opts = parser.parse_args()

    if (opts.threadx or opts.zephyr) and IS_WINDOWS:
        sys.exit("--threadx and --zephyr are Linux builds")
    if opts.board and (opts.threadx or opts.zephyr):
        sys.exit("--board replaces the local node; leave out --threadx/--zephyr")

    if not IS_WINDOWS:     # leftovers from an earlier run would hold the ports
        for pattern in ("sensor_node", "sensor_pc", "zephyr.exe -testargs", "dmq-"):
            subprocess.run(["pkill", "-f", pattern], stderr=subprocess.DEVNULL)
        time.sleep(1)

    # The PC client, and the node unless --board
    pc = newest(candidates([os.path.join(HERE, "build"), os.path.join(OUT, "default")], f"sensor_pc{EXE}", opts.config))
    if not pc:
        sys.exit("sensor_pc not found: build example/sensor-node first (cmake -B build . && cmake --build build)")
    node, node_args = None, []
    if opts.threadx:
        node = newest(candidates([os.path.join(HERE, "build-threadx"), os.path.join(OUT, "threadx")], "sensor_node", ""))
        node_args = ["0"]
    elif opts.zephyr:
        zdirs = [os.path.join(HERE, "build-zephyr"), os.path.join(OUT, "node", "platform", "zephyr", "default")]
        node = newest([os.path.join(d, "zephyr", "zephyr.exe") for d in zdirs])
        node_args = ["-testargs", "0"]
    elif not opts.board:
        node = newest(candidates([os.path.join(HERE, "build"), os.path.join(OUT, "default")], f"sensor_node{EXE}", opts.config))
        node_args = ["0", "127.0.0.1", "--tools"]
    if not opts.board and not node:
        sys.exit("The node isn't built: see README.md (Build and Run)")

    print("--- Sensor node system ---")
    for exe in (pc, node):
        if exe and not exe.endswith("zephyr.exe"):
            warn_if_stale(exe)
    procs = []

    # 1. The tools, from tools/bin
    if not opts.no_tools:
        spy_args = ["9999"]
        if opts.log == "on":
            spy_args += ["--log", "spy_logs.txt"]
            log = os.path.join(REPO, "tools", "bin", "spy_logs.txt")
            if os.path.exists(log):
                os.remove(log)
        if opts.plotjuggler:
            spy_args.append("--plotjuggler")
        for name, exe, args in [("dmq-monitor", f"dmq-monitor{EXE}", ["9998", "--multicast", "239.1.1.1"]),
                                ("dmq-thread", f"dmq-thread{EXE}", ["9998", "--multicast", "239.1.1.1"]),
                                ("dmq-spy", f"dmq-spy{EXE}", spy_args)]:
            path = os.path.join(REPO, "tools", "bin", exe)
            if os.path.isfile(path):
                procs.append(launch(name, path, args, os.path.dirname(path)))
                time.sleep(0.5)
            else:
                print(f"  {name} not in tools/bin (build the tools first): skipped")

    # 2. The node, then the PC client (its self-test, then the node's data)
    if node:
        procs.append(launch("node", node, node_args, os.path.dirname(node)))
        time.sleep(1.5)
    target = opts.board or "127.0.0.1"
    procs.append(launch("PC client", pc, ["0", target, "--tools"], os.path.dirname(pc)))
    if opts.board:
        print(f"  The board at {opts.board} must be running the sensor node (node/platform/esp32s3).")

    print("\nRunning. Ctrl+C here stops everything.")
    try:
        while any(p.poll() is None for p in procs):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping...")
        for p in procs:
            p.terminate()
        if not IS_WINDOWS:
            for pattern in ("sensor_node", "sensor_pc", "zephyr.exe -testargs", "dmq-"):
                subprocess.run(["pkill", "-f", pattern], stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
