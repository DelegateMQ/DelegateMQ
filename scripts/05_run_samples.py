"""
Script: 05_run_samples.py
Description:
    Test runner for all DelegateMQ sample projects.
    Discovers, runs, and reports pass/fail for every built sample.

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

Key Features:
    1. Platform Awareness: Skips Windows-only projects on Linux and vice versa.
    2. Client/Server Support: Launches paired apps concurrently, waits for both,
       then proceeds to the next sample. Multi-node projects (MULTI_NODE, e.g.
       databus/relay's gateway/monitor/sensor) launch every node the same way.
    3. Failure Detection: Catches non-zero exit codes, crashes, and assertion/
       exception text in output.
    4. Timeout Handling: Kills apps that do not exit on their own (e.g. infinite-
       loop databus samples). Timeout is not counted as a failure.
    5. Summary Report: Prints a final pass/fail/skipped table.
    6. Full suite (--full): also runs the unit-test app (test/build, plus
       test/build-clang with --clang and every full_suite.TEST_VARIANTS
       build), each to completion. A sample that should have run but wasn't
       built is then a failure; samples --full doesn't cover yet
       (full_suite.PENDING) stay skipped.
    7. Results file (--results FILE): writes passed/failed/skipped as JSON,
       for combining runs from several machines.

Zephyr and NuttX: the Zephyr native_sim apps (build/zephyr/zephyr.exe) and
NuttX's sim (build/nuttx/nuttx) run as host processes, checked for their
success text (see run_zephyr, run_databus_zephyr, run_nuttx).

MQTT: transport/mqtt's sub and pub run against a mosquitto broker this script
starts for the run (see run_mqtt).

Serial: transport/serialport runs on Linux over a socat pty pair, a virtual
null-modem cable between its sender and receiver ports (see run_serial).

QEMU targets: each built target of example/sample-projects/bare-metal/qemu and
os/freertos-bare-metal (build-<target>, full_suite.QEMU_TARGET_SAMPLES) runs on
QEMU; it passes if QEMU exits 0, which the program's semihosting exit makes
happen only when every check passed, and prints ALL TESTS PASSED.
os/freertos-bare-metal's Win32 simulator build/ also runs, on Windows.

Skip List (always excluded):
    - boards/stm32-freertos         Embedded STM32 target, cannot run on host
    - boards/esp32s3-devkitc        ESP32-S3 board, cannot run on host
    - boards/stm32h755-nucleo       Dual-core STM32H755 board, cannot run on host

Windows-only (skipped on Linux):
    - databus/freertos        Server uses FreeRTOS Win32 simulator (32-bit); full pair only runs on Windows
    - os/freertos-bare-metal     FreeRTOS Windows simulator (its QEMU targets run on both)
    - win32-*                 Windows API transport projects

Cross-language pairs (server from one project, non-C++ client):
    - system-architecture/python  C++ server (system-architecture), Python client
    - databus/interop             C++ DataBus server, Python client (msgpack required)
    - databus/interop (C#)        the same server with its C# client (Windows)
    - sample-interop              example/sample-interop's C++ server with its Python
                                  client and, on Windows, its C# client, all at once

Tools (tools/apps), against dmq-target:
    - dmq-spy --echo and dmq-monitor --once, then dmq-thread --once

Usage:
    Run this script FIFTH, after building all sample projects.
    > python scripts/05_run_samples.py
    > python scripts/05_run_samples.py --full --clang --yes --results results.json
"""

import argparse
import json
import os
import shutil
import sys
import platform
import signal
import subprocess
import threading
import time

import full_suite

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Duration (seconds) passed to each sample app via argv[1].
# Apps use this to self-terminate instead of running indefinitely.
APP_DURATION = 10

# Hard timeout: kill any process that has not exited after this many seconds.
# Must exceed APP_DURATION by a comfortable margin to allow clean shutdown.
APP_TIMEOUT = APP_DURATION + 15

SKIP_ALWAYS = {
    "boards/stm32-freertos",
    "boards/esp32s3-devkitc",
    "boards/stm32h755-nucleo",
    "test",
    "tools",
}

# Projects that pair with a server from a *different* project directory.
# Keys are the project name; values define the server project/subdir and
# the client command (relative to the project's own directory).
CROSS_PAIRS = {
    "system-architecture/python": {
        "server_project": "system-architecture/full",
        "server_subdir":  "server",
        "client_cmd":     [sys.executable, "-u", "main.py"],
        "client_cwd":     "client",
    },
    "databus/interop": {
        "server_project": "databus/interop",
        "server_subdir":  "server",
        "client_cmd":     [sys.executable, "-u", "main.py"],
        "client_cwd":     "python-client",
    },
}

# Projects made of more than two node apps, each in its own sub-directory with
# its own CMakeLists.txt. Values list the node sub-directories in start order;
# each node's result is reported under its sub-directory name.
MULTI_NODE = {
    "databus/relay": ["gateway", "monitor", "sensor"],  # Windows: gateway creates the pipe the monitor connects to
}

# Extra runs of a MULTI_NODE project with added node arguments, each reported
# as "<project> (<variant>)": {project: {variant: {node: [args]}}}.
MULTI_NODE_VARIANTS = {
    # gateway <-> monitor over shared memory instead of the pipe/Unix socket
    "databus/relay": {"shm": {"gateway": ["--shm"], "monitor": ["--shm"]}},
}

WINDOWS_ONLY = {
    "databus/freertos",        # server uses FreeRTOS Win32 simulator (32-bit); client/server pair only runs on Windows
    "os/freertos-bare-metal",
    "os/win32",                # DMQ_THREAD_WIN32 port
}

LINUX_ONLY = {
    "os/threadx",           # ThreadX Linux/GNU simulation port
    "os/freertos",          # FreeRTOS POSIX simulator port
    "os/posix",             # DMQ_THREAD_POSIX port; raw pthread_create, no Windows equivalent
    "os/qt",                # DMQ_THREAD_QT port; Qt 6 Core
    "os/zephyr",            # Zephyr native_sim
    "transport/zephyr-udp",
    "transport/netx-udp",      # ThreadX Linux/GNU simulation port + NetX Duo
    "os/cmsis-rtos2",
    "databus/zephyr",
    "os/nuttx",               # NuttX sim
    "os/freertos-esp32",      # ESP-IDF, Espressif's QEMU
}

# Projects that cannot exit on their own (e.g. FreeRTOS scheduler never returns).
# If this string appears anywhere in the output, the project is marked passed
# regardless of exit code or timeout.
SUCCESS_OVERRIDES = {
    "os/freertos-bare-metal": "ALL TESTS PASSED",
}

# Projects where we require specific strings in the output to confirm that
# client/server apps actually exchanged data (not just started without crashing).
# Keys are project names; values are dicts mapping role ("client"/"server"/"app")
# to a required substring.  Only roles listed here are checked.
REQUIRED_OUTPUT = {
    "databus/local":               {"app": "ALL TESTS PASSED"},
    "databus/basic":               {"client": "Client received DataMsg"},
    "databus/freertos":            {"client": "[Client] sensor/temp"},
    "databus/multicast":           {"client": "Received Multicast Data"},
    # Both directions must cross both hops (UDP and pipe/Unix socket) through the gateway relay.
    "databus/relay":               {"monitor": "[monitor] sensor/temp received",
                                    "sensor":  "[sensor] setpoint received"},
    "system-architecture/full":    {"client": "Actuators:"},
    "system-architecture/no-deps": {"client": "Actuators:"},
    "integration/no-cmake":        {"app": "ALL TESTS PASSED"},
    "integration/cmake-subdirectory": {"app": "ALL TESTS PASSED"},
    "patterns/state-machine":      {"app": "ALL TESTS PASSED"},
    "patterns/async-sqlite":       {"app": "ALL TESTS PASSED"},
    "os/qt":                       {"app": "ALL TESTS PASSED"},
    "os/win32":                    {"app": "ALL TESTS PASSED"},
    "transport/netx-udp":          {"app": "Data Message:"},
    "system-architecture/python":  {"client": "[RECV] Data:"},
    "databus/interop":             {"client": "[RECV] DataMsg"},
}

IS_WINDOWS = platform.system() == "Windows"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

FAILURE_KEYWORDS = [
    "assertion failed",
    "assert(",
    "terminate called",
    "segmentation fault",
    "access violation",
    "unhandled exception",
    "fatal error",
    "abort()",
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_executable(build_dir, exe_name):
    """Search common CMake output sub-directories for a built executable."""
    candidates = [
        os.path.join(build_dir, "Release",      exe_name + EXE_SUFFIX),
        os.path.join(build_dir, "Debug",         exe_name + EXE_SUFFIX),
        os.path.join(build_dir,                  exe_name + EXE_SUFFIX),
        os.path.join(build_dir, "x64", "Release",exe_name + EXE_SUFFIX),
        os.path.join(build_dir, "x64", "Debug",  exe_name + EXE_SUFFIX),
        os.path.join(build_dir, "Win32","Release",exe_name + EXE_SUFFIX),
        os.path.join(build_dir, "Win32","Debug",  exe_name + EXE_SUFFIX),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def get_exe_name(cmake_file):
    """Parse set(DELEGATE_APP "...") from a CMakeLists.txt."""
    if not os.path.isfile(cmake_file):
        return "delegate_app"
    with open(cmake_file, "r", errors="replace") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("set(DELEGATE_APP"):
                parts = stripped.split('"')
                if len(parts) >= 2:
                    return parts[1]
    return "delegate_app"


def has_failure_text(output):
    """Return True if output contains any known failure keyword."""
    lower = output.lower()
    return any(kw in lower for kw in FAILURE_KEYWORDS)


# ---------------------------------------------------------------------------
# Process registry — tracks all live Popen objects for clean Ctrl+C shutdown
# ---------------------------------------------------------------------------

_active_procs: list = []
_procs_lock = threading.Lock()

def _register(proc):
    with _procs_lock:
        _active_procs.append(proc)

def _unregister(proc):
    with _procs_lock:
        try:
            _active_procs.remove(proc)
        except ValueError:
            pass

def _kill_all():
    with _procs_lock:
        for proc in list(_active_procs):
            try:
                proc.kill()
            except Exception:
                pass

def _sigint_handler(signum, frame):
    print("\nInterrupted — killing all running sample processes...")
    _kill_all()
    sys.exit(1)

signal.signal(signal.SIGINT, _sigint_handler)

# ---------------------------------------------------------------------------
# Process execution
# ---------------------------------------------------------------------------

def run_process(cmd, cwd, label, results, timeout=APP_TIMEOUT, env=None):
    """Run one command, capture output, populate results[label]."""
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
        _register(proc)
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
            timed_out = False
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()
            timed_out = True
        finally:
            _unregister(proc)

        combined = (stdout or "") + (stderr or "")
        # A non-zero return code after a clean exit (not timeout) is a failure.
        crashed = (proc.returncode != 0) and not timed_out
        results[label] = {
            "failed":     crashed or has_failure_text(combined),
            "returncode": proc.returncode,
            "timed_out":  timed_out,
            "output":     combined,
        }
    except Exception as exc:
        results[label] = {
            "failed":     True,
            "returncode": -1,
            "timed_out":  False,
            "output":     str(exc),
        }


def run_standalone(exe_path, project_dir):
    """Run a single app and return (failed, results)."""
    results = {}
    run_process([exe_path, str(APP_DURATION)], project_dir, "app", results)
    r = results.get("app", {"failed": True, "output": "did not run", "returncode": -1, "timed_out": False})
    return r["failed"], results


def run_group(procs):
    """
    Launch several apps concurrently, in list order, 1 s apart; wait for all.
    procs is a list of (label, cmd, cwd). Return (failed, results).
    """
    results = {}
    threads = []
    for i, (label, cmd, cwd) in enumerate(procs):
        if i > 0:
            time.sleep(1.0)  # give earlier apps time to bind/listen before later ones connect
        t = threading.Thread(target=run_process, args=(cmd, cwd, label, results), daemon=True)
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    not_run = {"failed": True, "output": "did not run", "returncode": -1, "timed_out": False}
    did_fail = any(results.get(label, not_run)["failed"] for label, _, _ in procs)
    return did_fail, results


def run_pair(server_cmd, client_cmd, server_cwd, client_cwd):
    """Launch server then client concurrently; wait for both. Return (failed, results)."""
    return run_group([
        ("server", server_cmd, server_cwd),
        ("client", client_cmd, client_cwd),
    ])


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def try_run_clang(project_name, project_dir, is_pair, client_dir, server_dir):
    """
    Attempt to run the clang-built variant of a project (build-clang/ directories).
    Returns (ran, did_fail, results). ran=False means no clang build was found.
    Cross-pair projects are not supported and return (False, False, {}).
    """
    if is_pair:
        client_name = get_exe_name(os.path.join(client_dir, "CMakeLists.txt"))
        server_name = get_exe_name(os.path.join(server_dir, "CMakeLists.txt"))
        client_exe  = find_executable(full_suite.build_dir(client_dir, "build-clang"), client_name)
        server_exe  = find_executable(full_suite.build_dir(server_dir, "build-clang"), server_name)
        if not client_exe or not server_exe:
            return False, False, {}
        did_fail, results = run_pair(
            [server_exe, str(APP_DURATION)],
            [client_exe, str(APP_DURATION)],
            server_dir, client_dir,
        )
        return True, did_fail, results
    else:
        cmake_file = os.path.join(project_dir, "CMakeLists.txt")
        exe_name   = get_exe_name(cmake_file)
        exe_path   = find_executable(full_suite.build_dir(project_dir, "build-clang"), exe_name)
        if not exe_path:
            return False, False, {}
        did_fail, results = run_standalone(exe_path, project_dir)
        return True, did_fail, results


def find_dotnet_exe(project_dir):
    """The built C# app of the .csproj in project_dir (bin/<config>/<framework>/),
    or None. Windows only: the C# interop clients load the native DLL."""
    import re
    csproj = next((f for f in os.listdir(project_dir) if f.endswith(".csproj")), None)
    if not csproj:
        return None
    text = open(os.path.join(project_dir, csproj), encoding="utf-8", errors="replace").read()
    framework = re.search(r"<TargetFramework>([^<]+)</TargetFramework>", text)
    name = re.search(r"<AssemblyName>([^<]+)</AssemblyName>", text)
    exe_name = (name.group(1) if name else os.path.splitext(csproj)[0]) + ".exe"
    if not framework:
        return None
    path = os.path.join(project_dir, "bin", "Release", framework.group(1), exe_name)
    return path if os.path.isfile(path) else None


def run_interop(repo_root, samples_dir, passed, failed, skipped):
    """The interop runs beyond databus/interop's C++ server + Python client
    (CROSS_PAIRS), so both wrappers of the native DmqInterop library run:

    - databus/interop (C#): its C++ server with the C# client (Windows). The
      client must receive the server's DataMsg.
    - sample-interop: example/sample-interop's C++ server with its Python
      client and, on Windows, its C# client, all at once. Every process must
      exit 0; a client exits 1 unless it received sensor data and had a
      command acknowledged.
    """
    # databus/interop with the C# client
    label = "databus/interop (C#)"
    if not IS_WINDOWS:
        skipped.append((label, "Windows only"))
    else:
        project = os.path.join(samples_dir, "databus", "interop")
        server_dir = os.path.join(project, "server")
        server = find_executable(full_suite.build_dir(server_dir),
                                 get_exe_name(os.path.join(server_dir, "CMakeLists.txt")))
        client_dir = os.path.join(project, "csharp-client")
        client = find_dotnet_exe(client_dir)
        if not server or not client:
            skipped.append((label, "not built"))
        else:
            print(f"[RUNNING] {label}  (cross-language client/server)", flush=True)
            did_fail, results = run_pair([server, str(APP_DURATION)], [client, str(APP_DURATION)],
                                         server_dir, client_dir)
            client_out = results.get("client", {}).get("output", "")
            if not did_fail and "[RECV] DataMsg" not in client_out:
                print("   FAILED  (client output missing: '[RECV] DataMsg')")
                results["client"]["failed"] = did_fail = True
            report_group(label, did_fail, results, passed, failed)

    # example/sample-interop: C++ server, Python client, C# client (Windows)
    label = "sample-interop"
    base = os.path.join(repo_root, "example", "sample-interop")
    server_dir = os.path.join(base, "cpp-server")
    server = find_executable(full_suite.build_dir(server_dir), "InteropServer")
    csharp = find_dotnet_exe(os.path.join(base, "csharp")) if IS_WINDOWS else None
    if not server or (IS_WINDOWS and not csharp):
        skipped.append((label, "not built"))
        return
    procs = [
        # The server outlives the clients, so they all finish on their own
        ("server", [server, str(APP_DURATION + 4)], server_dir),
        ("python", [sys.executable, "-u", "main.py", str(APP_DURATION)], os.path.join(base, "python")),
    ]
    if csharp:
        procs.append(("csharp", [csharp, str(APP_DURATION)], os.path.dirname(csharp)))
    print(f"[RUNNING] {label}  ({'/'.join(p[0] for p in procs)})", flush=True)
    did_fail, results = run_group(procs)
    if any(r.get("timed_out") for r in results.values()):
        did_fail = True   # every app ends on its own here; a timeout is a hang
    report_group(label, did_fail, results, passed, failed)


def run_tools(repo_root, passed, failed, skipped):
    """The diagnostic tools (tools/apps) against dmq-target, the test app that
    sends DataBus traffic through SpyBridge (to port 9999) and heartbeats and
    ThreadMonitor statistics through NodeBridge (to port 9998), all unicast to
    127.0.0.1. One unicast listener per port, so it runs twice:

    1. dmq-spy --echo and dmq-monitor --once: the spy must print dmq-target's
       sensor topics, the monitor must list its node (TargetApp).
    2. dmq-thread --once: must list dmq-target's SpyReporter thread (behind
       SpyBridge), which tests ThreadMonitor end to end (thread stats ->
       DataBus -> NodeBridge -> UDP).

    The listeners start first, so they hear dmq-target from its first message.
    """
    label = "tools"
    tools_dir = os.path.join(repo_root, "tools")
    exes = {}
    for name in ("dmq-spy", "dmq-monitor", "dmq-thread", "dmq-target"):
        exes[name] = (find_executable(full_suite.build_dir(tools_dir), name)
                      or find_executable(os.path.join(tools_dir, "bin"), name))
    if not all(exes.values()):
        skipped.append((label, "not built"))
        return

    listen = 8   # seconds each listener runs; dmq-target outlives them
    checks = [
        ([("spy", [exes["dmq-spy"], "--echo", "sensor/", "--duration", str(listen)], tools_dir),
          ("monitor", [exes["dmq-monitor"], "--once", str(listen)], tools_dir),
          ("target", [exes["dmq-target"], str(listen + 2)], tools_dir)],
         {"spy": "sensor/temp", "monitor": "TargetApp"}),
        ([("thread", [exes["dmq-thread"], "--once", str(listen)], tools_dir),
          ("target", [exes["dmq-target"], str(listen + 2)], tools_dir)],
         {"thread": "SpyReporter"}),
    ]
    print(f"[RUNNING] {label}  (dmq-spy, dmq-monitor, dmq-thread against dmq-target)", flush=True)
    all_results, did_fail = {}, False
    for procs, required in checks:
        group_fail, results = run_group(procs)
        for role, text in required.items():
            r = results.get(role, {})
            if text not in r.get("output", ""):
                print(f"   FAILED  ({role} output missing: {text!r})")
                r["failed"] = group_fail = True
        if any(r.get("timed_out") for r in results.values()):
            group_fail = True   # every tool here ends on its own; a timeout is a hang
        did_fail = did_fail or group_fail
        for role, r in results.items():
            all_results[role if role not in all_results else role + " (2)"] = r
    report_group(label, did_fail, all_results, passed, failed)


def run_sensor_node(repo_root, passed, failed, skipped):
    """example/sensor-node: the sensor node and its PC client over loopback UDP,
    with the node on the desktop, and on Linux also on ThreadX (Linux port) and
    Zephyr (native_sim). The client runs its self-test (data, commands, touches)
    and must print ALL TESTS PASSED; the node must print NODE CHECKS PASSED."""
    project_dir = os.path.join(repo_root, "example", "sensor-node")
    build_dir = full_suite.build_dir(project_dir)
    pc = find_executable(build_dir, "sensor_pc")
    nodes = [("sensor-node", find_executable(build_dir, "sensor_node"), [])]
    if not IS_WINDOWS:
        nodes.append(("sensor-node/threadx",
                      find_executable(full_suite.build_dir(project_dir, "build-threadx"), "sensor_node"), []))
        zephyr_exe = os.path.join(full_suite.build_dir(os.path.join(project_dir, "node", "platform", "zephyr")),
                                  "zephyr", "zephyr.exe")
        # native_sim passes its own arguments after -testargs
        nodes.append(("sensor-node/zephyr", zephyr_exe if os.path.isfile(zephyr_exe) else None, ["-testargs"]))
    for label, node, prefix in nodes:
        if not node or not pc:
            skipped.append((label, "not built"))
            continue
        run_sensor_node_pair(label, node, prefix, pc, project_dir, passed, failed)


def run_sensor_node_pair(label, node, prefix, pc, project_dir, passed, failed):
    print(f"[RUNNING] {label}  (node/pc-client)", flush=True)
    did_fail, results = run_group([
        ("node", [node] + prefix + [str(APP_DURATION)], project_dir),
        ("pc", [pc, str(APP_DURATION)], project_dir),
    ])
    for role, text in {"node": "NODE CHECKS PASSED", "pc": "ALL TESTS PASSED"}.items():
        r = results.get(role, {})
        if text not in r.get("output", ""):
            print(f"   FAILED  ({role} output missing: {text!r})")
            r["failed"] = did_fail = True
    if any(r.get("timed_out") for r in results.values()):
        did_fail = True   # both end on their own; a timeout is a hang
    report_group(label, did_fail, results, passed, failed)


def run_multi_node_variant(project_name, project_dir, variant, node_args, passed, failed, skipped):
    """Run a MULTI_NODE project again with extra arguments for some nodes
    (MULTI_NODE_VARIANTS), checked against the same REQUIRED_OUTPUT."""
    label = f"{project_name} ({variant})"
    procs = []
    for node in MULTI_NODE[project_name]:
        node_dir = os.path.join(project_dir, node)
        node_exe = find_executable(full_suite.build_dir(node_dir),
                                   get_exe_name(os.path.join(node_dir, "CMakeLists.txt")))
        if not node_exe:
            skipped.append((label, "not built"))
            return
        procs.append((node, [node_exe, str(APP_DURATION)] + node_args.get(node, []), node_dir))

    print(f"[RUNNING] {label}  ({'/'.join(MULTI_NODE[project_name])})")
    did_fail, results = run_group(procs)
    if not did_fail:
        for role, required_str in REQUIRED_OUTPUT.get(project_name, {}).items():
            role_result = results.get(role)
            if role_result is not None and required_str not in role_result.get("output", ""):
                print(f"   FAILED  ({role} output missing: {required_str!r})")
                role_result["failed"] = True
                did_fail = True
    report_group(label, did_fail, results, passed, failed)


def report_group(label, did_fail, results, passed, failed):
    """Print and record the result of a group run."""
    if did_fail:
        print("   FAILED")
        for role, r in results.items():
            if r.get("failed") or r.get("timed_out"):
                print_result_detail(role, r)
        failed.append(label)
    else:
        print("   passed")
        passed.append(label)


def print_result_detail(label, r):
    rc   = r.get("returncode", "?")
    tout = " [timeout]" if r.get("timed_out") else ""
    print(f"      [{label}] exit={rc}{tout}")
    out = r.get("output", "").strip()
    if out:
        # Print last few lines which usually contain the relevant error
        lines = out.splitlines()
        snippet = "\n         ".join(lines[-8:])
        print(f"         {snippet}")


def run_tests(repo_root, use_clang, passed, failed, skipped):
    """
    --full: run each build of the unit-test app (test/build, test/build-clang,
    full_suite.TEST_VARIANTS) to completion. A run passes if it exits 0 within
    full_suite.TEST_TIMEOUT and, where the variant has one, prints its success
    marker. The sanitizer builds exit non-zero on their first report.
    """
    test_dir = os.path.join(repo_root, "test")
    exe_name = get_exe_name(os.path.join(test_dir, "CMakeLists.txt"))
    runs = [{"suffix": "build", "env": {}, "success": "Success!"}]
    if use_clang:
        runs.append({"suffix": "build-clang", "env": {}, "success": "Success!"})
    runs += full_suite.test_variants()

    for v in runs:
        label = f"test/{v['suffix']}"
        exe_path = find_executable(full_suite.build_dir(test_dir, v["suffix"]), exe_name)
        if not exe_path:
            skipped.append((label, "not built"))
            continue

        cross = v.get("cross")
        cmd = [exe_path]
        if cross:
            # Another CPU's binary: run it under QEMU user mode with that CPU's libraries
            cmd = full_suite.cross_command(cross, exe_path)
        print(f"[RUNNING] {label}  (unit tests{', ' + cross['qemu'] if cross else ''})", flush=True)
        env = dict(os.environ, **v["env"])
        results = {}
        start = time.time()
        run_process(cmd, test_dir, "app", results, timeout=full_suite.TEST_TIMEOUT, env=env)
        r = results["app"]
        elapsed = time.time() - start

        did_fail = r["returncode"] != 0 or r["timed_out"]
        if not did_fail and v["success"] and v["success"] not in r["output"]:
            print(f"   FAILED  (output missing: {v['success']!r})")
            did_fail = True
        if did_fail:
            print(f"   FAILED  ({elapsed:.0f}s)")
            print_result_detail("app", r)
            failed.append(label)
        else:
            print(f"   passed  ({elapsed:.0f}s)")
            passed.append(label)


def run_bare_metal(project_dir, passed, failed, skipped):
    """Run each built QEMU target of the sample (full_suite.QEMU_TARGET_SAMPLES)."""
    sample = full_suite.sample_name(project_dir)
    for target, spec in full_suite.QEMU_TARGET_SAMPLES[sample].items():
        label = f"{sample}/{target}"
        # A cross-compiled ELF: no .exe suffix, even when built on Windows
        kernel = os.path.join(full_suite.build_dir(project_dir, f"build-{target}"), "delegate_app")
        # A stale ELF from before the toolchain was removed isn't this code
        if not os.path.isfile(kernel) or full_suite.bare_metal_missing(target, sample):
            # On Windows a target whose toolchain isn't installed is left to
            # the Linux run (Docker has every toolchain)
            skipped.append((label, full_suite.LINUX_RUN_COVERS if IS_WINDOWS else "not built"))
            continue

        print(f"[RUNNING] {label}  (QEMU)", flush=True)
        results = {}
        cmd = spec["qemu"] + ["-kernel", kernel] + full_suite.QEMU_SEMIHOSTING
        run_process(cmd, project_dir, "app", results, timeout=full_suite.BARE_METAL_TIMEOUT)
        r = results["app"]
        ok = (r["returncode"] == 0 and not r["timed_out"]
              and full_suite.BARE_METAL_SUCCESS in r["output"])
        if ok:
            print("   passed")
            passed.append(label)
        else:
            print("   FAILED")
            print_result_detail("app", r)
            failed.append(label)


def run_transport(project_dir, use_clang, passed, failed, skipped):
    """transport/serializer: run each built combination (full_suite.transport_configs()),
    and its clang build with use_clang. Passes if the app exits 0 (it exits 1
    if no message arrived intact) and printed the received data."""
    sample = full_suite.sample_name(project_dir)
    exe_name = get_exe_name(os.path.join(project_dir, "CMakeLists.txt"))
    suffixes = [""] + (["-clang"] if use_clang and not IS_WINDOWS else [])
    for name, _, _ in full_suite.transport_configs():
        for suffix in suffixes:
            label = f"{sample}/{name}" + (" (clang)" if suffix else "")
            exe = find_executable(full_suite.build_dir(project_dir, f"build-{name}{suffix}"), exe_name)
            if not exe:
                skipped.append((label, "not built"))
                continue
            print(f"[RUNNING] {label}", flush=True)
            did_fail, results = run_standalone(exe, project_dir)
            r = results["app"]
            did_fail = did_fail or r["timed_out"]   # it ends on its own after APP_DURATION
            if not did_fail and full_suite.TRANSPORT_SUCCESS not in r["output"]:
                print(f"   FAILED  (output missing: {full_suite.TRANSPORT_SUCCESS!r})")
                did_fail = True
            if did_fail:
                print("   FAILED")
                print_result_detail("app", r)
                failed.append(label)
            else:
                print("   passed")
                passed.append(label)


def run_serial(project_dir, use_clang, passed, failed, skipped):
    """
    transport/serialport on a virtual null-modem cable: socat links two
    pseudo-terminals (full_suite.SERIAL_PTYS), the sender opens one and the
    receiver the other. Passes if the app exits 0 (it does only when the
    receiver got messages) and printed one. Linux only; on Windows the Linux
    run covers it.
    """
    name = full_suite.SERIAL_SAMPLE
    if IS_WINDOWS:
        skipped.append((name, full_suite.LINUX_RUN_COVERS))
        return
    exe_name = get_exe_name(os.path.join(project_dir, "CMakeLists.txt"))
    builds = [("build", name)] + ([("build-clang", f"{name} (clang)")] if use_clang else [])
    for build_dir, label in builds:
        exe = find_executable(full_suite.build_dir(project_dir, build_dir), exe_name)
        if not exe:
            skipped.append((label, "not built"))
            continue
        if not shutil.which("socat"):
            skipped.append((label, "socat not installed (virtual serial port pair)"))
            continue

        for link in full_suite.SERIAL_PTYS:
            if os.path.lexists(link):
                os.remove(link)   # stale link from an earlier run
        socat = subprocess.Popen(["socat"] + [f"pty,raw,echo=0,link={p}" for p in full_suite.SERIAL_PTYS],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        _register(socat)
        try:
            deadline = time.time() + 5
            while not all(os.path.exists(p) for p in full_suite.SERIAL_PTYS) and time.time() < deadline:
                time.sleep(0.1)
            if not all(os.path.exists(p) for p in full_suite.SERIAL_PTYS):
                print(f"[RUNNING] {label}  (socat pty pair)\n   FAILED  (socat did not create the ptys)")
                failed.append(label)
                continue

            print(f"[RUNNING] {label}  (socat pty pair)", flush=True)
            results = {}
            run_process([exe, str(APP_DURATION)] + list(full_suite.SERIAL_PTYS), project_dir, "app", results)
            r = results["app"]
            if not r["failed"] and not r["timed_out"] and full_suite.SERIAL_SUCCESS in r["output"]:
                print("   passed")
                passed.append(label)
            else:
                print("   FAILED")
                print_result_detail("app", r)
                failed.append(label)
        finally:
            socat.kill()
            socat.wait()
            _unregister(socat)


def run_mqtt(project_dir, use_clang, passed, failed, skipped):
    """
    transport/mqtt: start a mosquitto broker on full_suite.MQTT_PORT, run sub
    then pub against it for APP_DURATION seconds, stop the broker. Passes if
    both exit cleanly and sub received data (it exits 1 if it got nothing).
    Runs wherever mosquitto is installed; on Windows without it the Linux run
    covers it.
    """
    name = full_suite.MQTT_SAMPLE
    mosquitto = shutil.which("mosquitto")
    if not mosquitto and IS_WINDOWS:
        skipped.append((name, full_suite.LINUX_RUN_COVERS))
        return
    broker = f"tcp://localhost:{full_suite.MQTT_PORT}"
    builds = [("build", name)] + ([("build-clang", f"{name} (clang)")] if use_clang else [])
    for build_dir, label in builds:
        nodes = {}
        for node in ("sub", "pub"):
            node_dir = os.path.join(project_dir, node)
            nodes[node] = find_executable(full_suite.build_dir(node_dir, build_dir),
                                          get_exe_name(os.path.join(node_dir, "CMakeLists.txt")))
        if not all(nodes.values()):
            skipped.append((label, "not built"))
            continue
        if not mosquitto:
            skipped.append((label, "mosquitto not installed (MQTT broker)"))
            continue

        broker_proc = subprocess.Popen([mosquitto, "-p", str(full_suite.MQTT_PORT)],
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
        _register(broker_proc)
        try:
            time.sleep(1.0)   # let the broker start listening
            print(f"[RUNNING] {label}  (sub/pub via mosquitto)", flush=True)
            # sub first, so it is subscribed before pub publishes
            did_fail, results = run_group([
                (node, [nodes[node], broker, str(APP_DURATION)], os.path.join(project_dir, node))
                for node in ("sub", "pub")])
            sub = results.get("sub", {})
            if not did_fail and (sub.get("timed_out") or full_suite.MQTT_SUCCESS not in sub.get("output", "")):
                did_fail = True
            if did_fail:
                print("   FAILED")
                for node, r in results.items():
                    print_result_detail(node, r)
                failed.append(label)
            else:
                print("   passed")
                passed.append(label)
        finally:
            broker_proc.kill()
            broker_proc.wait()
            _unregister(broker_proc)


def run_zephyr(project_dir, name, passed, failed, skipped):
    """A Zephyr native_sim app (build/zephyr/zephyr.exe): no arguments (it takes
    native_sim's own options), must exit 0 and print its success text."""
    exe = os.path.join(full_suite.build_dir(project_dir), "zephyr", "zephyr.exe")
    if not os.path.isfile(exe):
        skipped.append((name, "not built"))
        return
    print(f"[RUNNING] {name}  (Zephyr native_sim)", flush=True)
    results = {}
    run_process([exe], project_dir, "app", results)
    r = results["app"]
    if not r["failed"] and not r["timed_out"] and full_suite.ZEPHYR_SAMPLES[name] in r["output"]:
        print("   passed")
        passed.append(name)
    else:
        print("   FAILED")
        print_result_detail("app", r)
        failed.append(name)
    if name in full_suite.ZEPHYR_QEMU_TARGETS:
        run_zephyr_qemu(project_dir, name, passed, failed, skipped)


def run_zephyr_qemu(project_dir, name, passed, failed, skipped):
    """The same Zephyr app built for a Cortex-M board (full_suite.ZEPHYR_QEMU_TARGETS),
    run under QEMU: passes if QEMU exits 0 (the app's semihosting exit, only when
    every check passed) and the app printed its success text."""
    spec = full_suite.ZEPHYR_QEMU_TARGETS[name]
    label = f"{name}/{spec['board'].replace('/', '-')}"
    elf = os.path.join(full_suite.build_dir(project_dir, spec["build"]), "zephyr", "zephyr.elf")
    if not os.path.isfile(elf):
        skipped.append((label, "not built"))
        return
    print(f"[RUNNING] {label}  (Zephyr {spec['board']}, QEMU)", flush=True)
    results = {}
    run_process(spec["qemu"] + ["-kernel", elf], project_dir, "app", results,
                timeout=full_suite.BARE_METAL_TIMEOUT)
    r = results["app"]
    if (r["returncode"] == 0 and not r["timed_out"]
            and full_suite.ZEPHYR_SAMPLES[name] in r["output"]):
        print("   passed")
        passed.append(label)
    else:
        print("   FAILED")
        print_result_detail("app", r)
        failed.append(label)


def run_databus_zephyr(project_dir, passed, failed, skipped):
    """databus/zephyr: the Zephyr server (native_sim, stopped with its --stop_at
    option once the client is done) and the host client exchange DataBus
    messages over the host loopback, in both directions."""
    name = full_suite.ZEPHYR_DATABUS_SAMPLE
    server_dir = os.path.join(project_dir, "server")
    client_dir = os.path.join(project_dir, "client")
    server = os.path.join(full_suite.build_dir(server_dir), "zephyr", "zephyr.exe")
    client = find_executable(full_suite.build_dir(client_dir),
                             get_exe_name(os.path.join(client_dir, "CMakeLists.txt")))
    if not os.path.isfile(server) or not client:
        skipped.append((name, "not built"))
        return
    print(f"[RUNNING] {name}  (Zephyr server / host client)", flush=True)
    did_fail, results = run_group([
        ("server", [server, f"--stop_at={APP_DURATION + 5}"], server_dir),
        ("client", [client, str(APP_DURATION)], client_dir),
    ])
    for role, text in full_suite.ZEPHYR_DATABUS_REQUIRED.items():
        r = results.get(role, {})
        if r.get("timed_out") or text not in r.get("output", ""):
            r["failed"] = did_fail = True
    if did_fail:
        print("   FAILED")
        for role, r in results.items():
            print_result_detail(role, r)
        failed.append(name)
    else:
        print("   passed")
        passed.append(name)


def run_nuttx(sample_dir, passed, failed, skipped):
    """os/nuttx: the NuttX sim keeps running after its tests (an OS doesn't
    power off when an app returns), so read its output and stop it as soon as
    the success text appears; fail if it doesn't within NUTTX_TIMEOUT."""
    name = full_suite.NUTTX_SAMPLE
    exe = os.path.join(full_suite.build_dir(sample_dir), "nuttx", "nuttx")
    if not os.path.isfile(exe):
        skipped.append((name, "not built"))
        return
    print(f"[RUNNING] {name}  (NuttX sim)", flush=True)
    output = []
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe), stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    _register(proc)
    timer = threading.Timer(full_suite.NUTTX_TIMEOUT, proc.kill)
    timer.start()
    try:
        for line in proc.stdout:
            output.append(line)
            if full_suite.NUTTX_SUCCESS in line:
                break
    finally:
        timer.cancel()
        proc.kill()
        proc.wait()
        _unregister(proc)
    text = "".join(output)
    r = {"failed": False, "returncode": proc.returncode, "timed_out": False, "output": text}
    if full_suite.NUTTX_SUCCESS in text and not has_failure_text(text):
        print("   passed")
        passed.append(name)
    else:
        r["failed"] = True
        print("   FAILED")
        print_result_detail("app", r)
        failed.append(name)


def run_esp32(sample_dir, passed, failed, skipped):
    """os/freertos-esp32 under Espressif's QEMU, which keeps running after the
    app is done: read its output and stop it at the success text or a fault;
    fail if neither comes within ESP32_TIMEOUT."""
    name = full_suite.ESP32_SAMPLE
    flash = os.path.join(full_suite.build_dir(sample_dir), "flash.bin")
    env = full_suite.esp_idf_env()
    if env is None or not os.path.isfile(flash):
        skipped.append((name, "not built"))
        return
    print(f"[RUNNING] {name}  (ESP32, QEMU)", flush=True)
    cmd = full_suite.ESP32_QEMU + ["-drive", f"file={flash},if=mtd,format=raw"]
    output = []
    proc = subprocess.Popen(cmd, cwd=sample_dir, env=env, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    _register(proc)
    timer = threading.Timer(full_suite.ESP32_TIMEOUT, proc.kill)
    timer.start()
    try:
        for line in proc.stdout:
            output.append(line)
            if full_suite.ESP32_SUCCESS in line or any(f in line for f in full_suite.ESP32_FAULT):
                break
    finally:
        timer.cancel()
        proc.kill()
        proc.wait()
        _unregister(proc)
    text = "".join(output)
    r = {"failed": False, "returncode": proc.returncode, "timed_out": False, "output": text}
    if (full_suite.ESP32_SUCCESS in text and not has_failure_text(text)
            and not any(f in text for f in full_suite.ESP32_FAULT)):
        print("   passed")
        passed.append(name)
    else:
        r["failed"] = True
        print("   FAILED")
        print_result_detail("app", r)
        failed.append(name)


def run_cross_endian(project_dir, passed, failed, skipped):
    """
    full_suite.CROSS_ENDIAN_SAMPLE with peers of different byte order: one side
    native (x86, little-endian), the other its CROSS_ENDIAN_BUILD (big-endian)
    under QEMU user mode, talking over the host loopback; both ways round.
    Passes if each side decoded the other's small, known IDs (a byte-swapped
    1 reads as 16777216) and no unknown ID arrived.
    """
    cpu = full_suite.CROSS_ENDIAN_CPU
    dirs = {role: os.path.join(project_dir, role) for role in ("server", "client")}
    exes = {}
    for role, d in dirs.items():
        name = get_exe_name(os.path.join(d, "CMakeLists.txt"))
        exes[role] = {"native": find_executable(full_suite.build_dir(d), name),
                      "cross": find_executable(full_suite.build_dir(d, full_suite.CROSS_ENDIAN_BUILD), name)}

    for cross_role in ("client", "server"):
        label = f"{full_suite.CROSS_ENDIAN_SAMPLE} ({cpu['processor']} {cross_role}, x86 peer)"
        if not all(e["native"] and e["cross"] for e in exes.values()):
            skipped.append((label, "not built"))
            continue
        if full_suite.cross_missing(cpu):
            skipped.append((label, "missing " + ", ".join(full_suite.cross_missing(cpu))))
            continue

        def command(role):
            if role == cross_role:
                return full_suite.cross_command(cpu, exes[role]["cross"]) + [str(APP_DURATION)]
            return [exes[role]["native"], str(APP_DURATION)]

        print(f"[RUNNING] {label}  (cross-endian)", flush=True)
        did_fail, results = run_group([(role, command(role), dirs[role]) for role in ("server", "client")])
        for role, texts in full_suite.CROSS_ENDIAN_REQUIRED.items():
            r = results.get(role, {})
            out = r.get("output", "")
            missing = [t for t in texts if t not in out]
            if missing or full_suite.CROSS_ENDIAN_FORBIDDEN in out:
                print(f"   {role} output: missing {missing}" if missing
                      else f"   {role} output has {full_suite.CROSS_ENDIAN_FORBIDDEN!r}")
                r["failed"] = did_fail = True
        if did_fail:
            print("   FAILED")
            for role, r in results.items():
                if r.get("failed"):
                    print_result_detail(role, r)
            failed.append(label)
        else:
            print("   passed")
            passed.append(label)


def write_results(path, passed, failed, skipped):
    """Write the run's outcome as JSON, for combining runs from several machines."""
    data = {
        "platform": "Windows" if IS_WINDOWS else platform.system(),
        "passed":   passed,
        "failed":   failed,
        "skipped":  [[name, reason] for name, reason in skipped],
    }
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def run_samples(use_clang=False, assume_yes=False, full=False, results_path=None):
    repo_root   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    samples_dir = os.path.join(repo_root, "example", "sample-projects")

    if not os.path.isdir(samples_dir):
        print(f"Error: sample-projects directory not found: {samples_dir}")
        return False

    clang_found = shutil.which("clang++") is not None
    clang_missing = False
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
        use_clang = False
    elif use_clang and not clang_found:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
        use_clang = False
        clang_missing = full

    os_label = "Windows" if IS_WINDOWS else "Linux"

    print("=" * 60)
    print("  DelegateMQ Sample Runner")
    print("=" * 60)
    print(f"  Platform : {os_label}")
    print(f"  Timeout  : {APP_TIMEOUT}s per project")
    if use_clang:
        print(f"  Clang    : enabled (build-clang/ variants will also run)")
    print()
    print("  WARNING: This will run all built sample projects.")
    print("  Each sample runs for up to 25 seconds. Total")
    print("  elapsed time may exceed several minutes.")
    print("=" * 60)
    if not assume_yes:
        answer = input("\nContinue? [y/N]: ").strip().lower()
        if answer != "y":
            print("Aborted.")
            return True
    print()

    passed  = []
    failed  = []
    skipped = []
    if clang_missing:
        failed.append("clang builds (clang++ not found)")

    for project_name, project_dir in full_suite.list_samples(samples_dir):

        # --- Skip checks ---
        if project_name in SKIP_ALWAYS:
            skipped.append((project_name, "excluded"))
            continue
        if project_name in full_suite.QEMU_TARGET_SAMPLES:
            run_bare_metal(project_dir, passed, failed, skipped)
            if project_name == full_suite.BARE_METAL_SAMPLE:
                continue
        if project_name in WINDOWS_ONLY and not IS_WINDOWS:
            skipped.append((project_name, "Windows only"))
            continue
        if project_name in LINUX_ONLY and IS_WINDOWS:
            skipped.append((project_name, "Linux only"))
            continue
        if project_name == full_suite.SERIAL_SAMPLE:
            run_serial(project_dir, use_clang, passed, failed, skipped)
            continue
        if project_name == full_suite.MQTT_SAMPLE:
            run_mqtt(project_dir, use_clang, passed, failed, skipped)
            continue
        if project_name in full_suite.ZEPHYR_SAMPLES:
            run_zephyr(project_dir, project_name, passed, failed, skipped)
            continue
        if project_name == full_suite.ZEPHYR_DATABUS_SAMPLE:
            run_databus_zephyr(project_dir, passed, failed, skipped)
            continue
        if project_name == full_suite.NUTTX_SAMPLE:
            run_nuttx(project_dir, passed, failed, skipped)
            continue
        if project_name == full_suite.ESP32_SAMPLE:
            run_esp32(project_dir, passed, failed, skipped)
            continue
        if project_name == full_suite.TRANSPORT_SAMPLE:
            run_transport(project_dir, use_clang, passed, failed, skipped)
            continue

        # --- Determine topology ---
        client_dir = os.path.join(project_dir, "client")
        server_dir = os.path.join(project_dir, "server")
        is_pair    = os.path.isdir(client_dir) and os.path.isdir(server_dir)
        did_fail   = False
        results    = {}

        if project_name in CROSS_PAIRS:
            spec        = CROSS_PAIRS[project_name]
            server_proj = os.path.join(samples_dir, spec["server_project"], spec["server_subdir"])
            server_name = get_exe_name(os.path.join(server_proj, "CMakeLists.txt"))
            server_exe  = find_executable(full_suite.build_dir(server_proj), server_name)

            if not server_exe:
                skipped.append((project_name, f"{spec['server_project']} server not built"))
                continue

            client_cwd = os.path.join(project_dir, spec["client_cwd"])
            server_cmd = [server_exe, str(APP_DURATION)]
            client_cmd = spec["client_cmd"] + [str(APP_DURATION)]

            print(f"[RUNNING] {project_name}  (cross-language client/server)")
            did_fail, results = run_pair(server_cmd, client_cmd, server_proj, client_cwd)

        elif project_name in MULTI_NODE:
            procs = []
            for node in MULTI_NODE[project_name]:
                node_dir = os.path.join(project_dir, node)
                node_exe = find_executable(full_suite.build_dir(node_dir),
                                           get_exe_name(os.path.join(node_dir, "CMakeLists.txt")))
                if not node_exe:
                    break
                procs.append((node, [node_exe, str(APP_DURATION)], node_dir))

            if len(procs) != len(MULTI_NODE[project_name]):
                skipped.append((project_name, "not built"))
                continue

            print(f"[RUNNING] {project_name}  ({'/'.join(MULTI_NODE[project_name])})")
            did_fail, results = run_group(procs)

        elif is_pair:
            client_name = get_exe_name(os.path.join(client_dir, "CMakeLists.txt"))
            server_name = get_exe_name(os.path.join(server_dir, "CMakeLists.txt"))
            client_exe  = find_executable(full_suite.build_dir(client_dir), client_name)
            server_exe  = find_executable(full_suite.build_dir(server_dir), server_name)

            if not client_exe or not server_exe:
                skipped.append((project_name, "not built"))
                continue

            print(f"[RUNNING] {project_name}  (client/server)")
            did_fail, results = run_pair(
                [server_exe, str(APP_DURATION)],
                [client_exe, str(APP_DURATION)],
                server_dir, client_dir,
            )

        else:
            cmake_file = os.path.join(project_dir, "CMakeLists.txt")
            if not os.path.isfile(cmake_file):
                skipped.append((project_name, "no CMakeLists.txt"))
                continue

            exe_name = get_exe_name(cmake_file)
            exe_path = find_executable(full_suite.build_dir(project_dir), exe_name)

            if not exe_path:
                skipped.append((project_name, "not built"))
                continue

            print(f"[RUNNING] {project_name}")
            did_fail, results = run_standalone(exe_path, project_dir)

        # Check success override — some apps (e.g. FreeRTOS) never exit on their
        # own, so we look for a known success string in the output instead.
        if did_fail and project_name in SUCCESS_OVERRIDES:
            success_str = SUCCESS_OVERRIDES[project_name]
            combined_out = "".join(r.get("output", "") for r in results.values())
            if success_str in combined_out:
                did_fail = False

        # Check required output — verify client/server actually communicated.
        if not did_fail and project_name in REQUIRED_OUTPUT:
            for role, required_str in REQUIRED_OUTPUT[project_name].items():
                role_result = results.get(role)
                if role_result is None:
                    continue
                if required_str not in role_result.get("output", ""):
                    print(f"   FAILED  ({role} output missing: {required_str!r})")
                    role_result["failed"] = True
                    did_fail = True

        if did_fail:
            print(f"   FAILED")
            for label, r in results.items():
                if r.get("failed"):
                    print_result_detail(label, r)
            failed.append(project_name)
        else:
            tout_note = ""
            if any(r.get("timed_out") for r in results.values()):
                tout_note = " (killed after timeout)"
            print(f"   passed{tout_note}")
            passed.append(project_name)

        # --- MULTI-NODE VARIANTS (e.g. databus/relay over shared memory) ---
        for variant, node_args in MULTI_NODE_VARIANTS.get(project_name, {}).items():
            run_multi_node_variant(project_name, project_dir, variant, node_args, passed, failed, skipped)

        # --- CROSS-ENDIAN RUNS (Linux) ---
        if project_name == full_suite.CROSS_ENDIAN_SAMPLE and not IS_WINDOWS:
            run_cross_endian(project_dir, passed, failed, skipped)

        # --- CLANG RUN (optional) ---
        # Cross-pair projects are skipped; their server lives in a different project dir.
        # Multi-node projects have no clang variant.
        if use_clang and project_name not in CROSS_PAIRS and project_name not in MULTI_NODE:
            clang_label = f"{project_name} (clang)"
            ran, clang_fail, clang_results = try_run_clang(
                project_name, project_dir, is_pair, client_dir, server_dir
            )
            if not ran:
                skipped.append((clang_label, "build-clang/ not found — run 03 --clang first"))
            else:
                print(f"[RUNNING] {clang_label}")
                if clang_fail and project_name in SUCCESS_OVERRIDES:
                    combined = "".join(r.get("output", "") for r in clang_results.values())
                    if SUCCESS_OVERRIDES[project_name] in combined:
                        clang_fail = False
                if not clang_fail and project_name in REQUIRED_OUTPUT:
                    for role, required_str in REQUIRED_OUTPUT[project_name].items():
                        role_result = clang_results.get(role)
                        if role_result and required_str not in role_result.get("output", ""):
                            role_result["failed"] = True
                            clang_fail = True
                if clang_fail:
                    print(f"   FAILED")
                    for label, r in clang_results.items():
                        if r.get("failed"):
                            print_result_detail(label, r)
                    failed.append(clang_label)
                else:
                    tout_note = " (killed after timeout)" if any(r.get("timed_out") for r in clang_results.values()) else ""
                    print(f"   passed{tout_note}")
                    passed.append(clang_label)

    # --- Interop: the C# wrapper, and example/sample-interop ---
    run_interop(repo_root, samples_dir, passed, failed, skipped)

    # --- The diagnostic tools, against dmq-target ---
    run_tools(repo_root, passed, failed, skipped)

    # --- example/sensor-node: the desktop node and its PC client ---
    run_sensor_node(repo_root, passed, failed, skipped)

    # --- Unit tests and strict skips (--full) ---
    if full:
        run_tests(repo_root, use_clang, passed, failed, skipped)
        skipped, not_run = full_suite.strict_split(skipped)
        for name, reason in not_run:
            failed.append(f"{name} ({reason})")

    if results_path:
        write_results(results_path, passed, failed, skipped)

    # --- Summary ---
    total = len(passed) + len(failed)
    print("\n" + "=" * 60)
    print(f"RESULTS  {len(passed)}/{total} passed   {len(failed)} failed   {len(skipped)} skipped")
    print("=" * 60)

    if failed:
        print("\nFAILED:")
        for name in failed:
            print(f"   {name}")

    if skipped:
        print("\nSKIPPED:")
        for name, reason in skipped:
            print(f"   {name:<40} ({reason})")

    print()
    return len(failed) == 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run all built DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also run build-clang/ variants built by 04_build_samples.py --clang (Linux only)."
    )
    parser.add_argument(
        "--yes", "-y", action="store_true",
        help="Skip the confirmation prompt."
    )
    parser.add_argument(
        "--full", action="store_true",
        help="Also run the unit-test builds in test/; anything not built fails (see full_suite.py)."
    )
    parser.add_argument(
        "--results", metavar="FILE",
        help="Write passed/failed/skipped to FILE as JSON."
    )
    args = parser.parse_args()
    success = run_samples(use_clang=args.clang, assume_yes=args.yes,
                          full=args.full, results_path=args.results)
    sys.exit(0 if success else 1)
