"""
Script: 05_run_samples.py
Description:
    Test runner for all DelegateMQ sample projects.
    Discovers, runs, and reports pass/fail for every built sample.

    See Examples Setup within DETAILS.md before running script.

Key Features:
    1. Platform Awareness: Skips Windows-only projects on Linux and vice versa.
    2. Client/Server Support: Launches paired apps concurrently, waits for both,
       then proceeds to the next sample.
    3. Failure Detection: Catches non-zero exit codes, crashes, and assertion/
       exception text in output.
    4. Timeout Handling: Kills apps that do not exit on their own (e.g. infinite-
       loop databus samples). Timeout is not counted as a failure.
    5. Summary Report: Prints a final pass/fail/skipped table.

Skip List (always excluded):
    - atfe-armv7m-bare-metal  Embedded ARM target, requires ATfE toolchain + QEMU
    - bare-metal-arm          Embedded ARM target, cannot run on host
    - bare-metal-riscv        Embedded RISC-V target, cannot run on host (needs qemu-system-riscv32)
    - stm32-freertos          Embedded STM32 target, cannot run on host
    - mqtt-rapidjson          Requires an external MQTT broker
    - serialport-serializer   Requires physical serial port hardware

Windows-only (skipped on Linux):
    - databus-freertos        Server uses FreeRTOS Win32 simulator (32-bit); full pair only runs on Windows
    - freertos-bare-metal     FreeRTOS Windows simulator
    - win32-*                 Windows API transport projects

Cross-language pairs (server from one project, non-C++ client):
    - system-architecture-python  C++ server (system-architecture), Python client
    - databus-interop             C++ DataBus server, Python client (msgpack required)

Usage:
    Run this script FIFTH, after building all sample projects.
    > python 05_run_samples.py
"""

import argparse
import os
import shutil
import sys
import platform
import signal
import subprocess
import threading
import time

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
    "atfe-armv7m-bare-metal",
    "bare-metal-arm",
    "bare-metal-riscv",
    "stm32-freertos",
    "mqtt-rapidjson",
    "serialport-serializer",
    "test",
    "tools",
}

# Projects that pair with a server from a *different* project directory.
# Keys are the project name; values define the server project/subdir and
# the client command (relative to the project's own directory).
CROSS_PAIRS = {
    "system-architecture-python": {
        "server_project": "system-architecture",
        "server_subdir":  "server",
        "client_cmd":     [sys.executable, "-u", "main.py"],
        "client_cwd":     "client",
    },
    "databus-interop": {
        "server_project": "databus-interop",
        "server_subdir":  "server",
        "client_cmd":     [sys.executable, "-u", "main.py"],
        "client_cwd":     "python-client",
    },
}

WINDOWS_ONLY = {
    "databus-freertos",        # server uses FreeRTOS Win32 simulator (32-bit); client/server pair only runs on Windows
    "freertos-bare-metal",
    "win32-pipe-serializer",
    "win32-tcp-serializer",
    "win32-udp-serializer",
}

LINUX_ONLY = {
    "linux-tcp-serializer",
    "linux-udp-serializer",
}

# Projects that cannot exit on their own (e.g. FreeRTOS scheduler never returns).
# If this string appears anywhere in the output, the project is marked passed
# regardless of exit code or timeout.
SUCCESS_OVERRIDES = {
    "freertos-bare-metal": "ALL TESTS PASSED",
}

# Projects where we require specific strings in the output to confirm that
# client/server apps actually exchanged data (not just started without crashing).
# Keys are project names; values are dicts mapping role ("client"/"server"/"app")
# to a required substring.  Only roles listed here are checked.
REQUIRED_OUTPUT = {
    "databus":                     {"client": "Client received DataMsg"},
    "databus-freertos":            {"client": "[Client] sensor/temp"},
    "databus-multicast":           {"client": "Received Multicast Data"},
    "system-architecture":         {"client": "Actuators:"},
    "system-architecture-no-deps": {"client": "Actuators:"},
    "system-architecture-python":  {"client": "[RECV] Data:"},
    "databus-interop":             {"client": "[RECV] DataMsg"},
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

def run_process(cmd, cwd, label, results):
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
        )
        _register(proc)
        try:
            stdout, stderr = proc.communicate(timeout=APP_TIMEOUT)
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


def run_pair(server_cmd, client_cmd, server_cwd, client_cwd):
    """Launch server then client concurrently; wait for both. Return (failed, results)."""
    results = {}

    server_thread = threading.Thread(
        target=run_process,
        args=(server_cmd, server_cwd, "server", results),
        daemon=True,
    )
    client_thread = threading.Thread(
        target=run_process,
        args=(client_cmd, client_cwd, "client", results),
        daemon=True,
    )

    server_thread.start()
    time.sleep(1.0)          # give server time to bind/listen before client connects
    client_thread.start()

    server_thread.join()
    client_thread.join()

    server_r = results.get("server", {"failed": True, "output": "did not run", "returncode": -1, "timed_out": False})
    client_r = results.get("client", {"failed": True, "output": "did not run", "returncode": -1, "timed_out": False})
    return server_r["failed"] or client_r["failed"], results


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
        client_exe  = find_executable(os.path.join(client_dir, "build-clang"), client_name)
        server_exe  = find_executable(os.path.join(server_dir, "build-clang"), server_name)
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
        exe_path   = find_executable(os.path.join(project_dir, "build-clang"), exe_name)
        if not exe_path:
            return False, False, {}
        did_fail, results = run_standalone(exe_path, project_dir)
        return True, did_fail, results


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


def run_samples(use_clang=False):
    repo_root   = os.path.dirname(os.path.abspath(__file__))
    samples_dir = os.path.join(repo_root, "example", "sample-projects")

    if not os.path.isdir(samples_dir):
        print(f"Error: sample-projects directory not found: {samples_dir}")
        return False

    clang_found = shutil.which("clang++") is not None
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
        use_clang = False
    elif use_clang and not clang_found:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
        use_clang = False

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
    answer = input("\nContinue? [y/N]: ").strip().lower()
    if answer != "y":
        print("Aborted.")
        return True
    print()

    passed  = []
    failed  = []
    skipped = []

    for project_name in sorted(os.listdir(samples_dir)):
        project_dir = os.path.join(samples_dir, project_name)
        if not os.path.isdir(project_dir):
            continue

        # --- Skip checks ---
        if project_name in SKIP_ALWAYS:
            skipped.append((project_name, "excluded"))
            continue
        if project_name in WINDOWS_ONLY and not IS_WINDOWS:
            skipped.append((project_name, "Windows only"))
            continue
        if project_name in LINUX_ONLY and IS_WINDOWS:
            skipped.append((project_name, "Linux only"))
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
            server_exe  = find_executable(os.path.join(server_proj, "build"), server_name)

            if not server_exe:
                skipped.append((project_name, f"{spec['server_project']} server not built"))
                continue

            client_cwd = os.path.join(project_dir, spec["client_cwd"])
            server_cmd = [server_exe, str(APP_DURATION)]
            client_cmd = spec["client_cmd"] + [str(APP_DURATION)]

            print(f"[RUNNING] {project_name}  (cross-language client/server)")
            did_fail, results = run_pair(server_cmd, client_cmd, server_proj, client_cwd)

        elif is_pair:
            client_name = get_exe_name(os.path.join(client_dir, "CMakeLists.txt"))
            server_name = get_exe_name(os.path.join(server_dir, "CMakeLists.txt"))
            client_exe  = find_executable(os.path.join(client_dir, "build"), client_name)
            server_exe  = find_executable(os.path.join(server_dir, "build"), server_name)

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
            exe_path = find_executable(os.path.join(project_dir, "build"), exe_name)

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

        # --- CLANG RUN (optional) ---
        # Cross-pair projects are skipped; their server lives in a different project dir.
        if use_clang and project_name not in CROSS_PAIRS:
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
    args = parser.parse_args()
    success = run_samples(use_clang=args.clang)
    sys.exit(0 if success else 1)
