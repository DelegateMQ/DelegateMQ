"""
Script: 04_build_samples.py
Description:
    Build orchestrator for all DelegateMQ sample projects.
    Assumes '03_generate_samples.py' has already been run to configure
    (generate) the build directories. This script compiles every project
    and reports pass/fail.

    See Examples Setup within DETAILS.md before running script.

Key Features:
    1. Platform Awareness: Skips Windows-only projects on Linux and vice versa.
    2. Client/Server Support: Builds client and server sub-projects independently.
    3. Build Config: Builds Release configuration on both Windows (MSVC
       multi-config) and Linux (single-config generators).
    4. Summary Report: Prints a final pass/fail/skipped table.

Skip List (always excluded):
    - atfe-armv7m-bare-metal  Embedded ARM target, requires ATfE toolchain + QEMU
    - bare-metal-arm          Embedded ARM target, requires cross-compiler
    - stm32-freertos          Embedded STM32 target, requires cross-compiler
    - mqtt-rapidjson          Requires external MQTT broker (excluded from run)
    - serialport-serializer   Requires physical serial port hardware
    - system-architecture-python  Python-only client, no C++ build

Usage:
    Run this script FOURTH, after generating project files with 03_generate_samples.py.
    > python 04_build_samples.py
"""

import argparse
import os
import shutil
import sys
import platform
import subprocess

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SKIP_ALWAYS = {
    "atfe-armv7m-bare-metal",
    "bare-metal-arm",
    "stm32-freertos",
    "mqtt-rapidjson",
    "serialport-serializer",
    "system-architecture-python",
}

WINDOWS_ONLY = {
    "databus-freertos",        # server uses FreeRTOS Win32 simulator (32-bit); full pair only meaningful on Windows
    "freertos-bare-metal",
    "win32-pipe-serializer",
    "win32-tcp-serializer",
    "win32-udp-serializer",
}

LINUX_ONLY = {
    "linux-tcp-serializer",
    "linux-udp-serializer",
}

# Projects that contain dotnet sub-projects requiring 'dotnet build' in addition
# to any cmake targets.  Keys are project names; values are subdirectory names.
DOTNET_BUILDS = {
    "databus-interop": ["csharp-client"],
    "sample-interop": ["csharp"],
}

IS_WINDOWS = platform.system() == "Windows"
BUILD_CONFIG = "Release"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_library(build_dir, lib_name):
    """Search common CMake output sub-directories for a built library."""
    suffix = ".dll" if IS_WINDOWS else ".so"
    candidates = [
        os.path.join(build_dir, "bin", BUILD_CONFIG, lib_name + suffix),
        os.path.join(build_dir, "lib", BUILD_CONFIG, lib_name + suffix),
        os.path.join(build_dir, "bin", lib_name + suffix),
        os.path.join(build_dir, "lib", lib_name + suffix),
        os.path.join(build_dir, BUILD_CONFIG, lib_name + suffix),
        os.path.join(build_dir, lib_name + suffix),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def copy_interop_dll(build_dir, repo_root):
    """Copy built DmqInterop DLL/SO to interop/python/."""
    lib_name = "DmqInterop"
    suffix = ".dll" if IS_WINDOWS else ".so"
    src = find_library(build_dir, lib_name)
    
    if src:
        dest_dir = os.path.join(repo_root, "interop", "python")
        dest = os.path.join(dest_dir, lib_name + suffix)
        try:
            os.makedirs(dest_dir, exist_ok=True)
            shutil.copy2(src, dest)
            print(f"   [COPY] {os.path.basename(src)} -> interop/python/")
        except Exception as e:
            print(f"   [COPY FAILED] {e}")


def build_project(build_dir, label):
    """
    Run cmake --build on an already-configured build directory.
    Returns (success, error_output).
    """
    if not os.path.isdir(build_dir):
        return False, f"Build directory not found: {build_dir}"

    cmd = ["cmake", "--build", build_dir, "--config", BUILD_CONFIG]
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0:
            return False, result.stdout
        return True, ""
    except Exception as exc:
        return False, str(exc)


def build_dotnet(project_subdir, label):
    """
    Run 'dotnet build' in a directory containing a .csproj file.
    Returns (success, error_output).
    """
    cmd = ["dotnet", "build", "--configuration", BUILD_CONFIG]
    try:
        result = subprocess.run(
            cmd,
            cwd=project_subdir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0:
            return False, result.stdout
        return True, ""
    except Exception as exc:
        return False, str(exc)


def collect_build_dirs(project_dir, use_clang=False):
    """
    Return a list of (label, build_dir) tuples for a project.
    Client/server projects yield two entries; standalone yields one.
    When use_clang=True, also includes 'build-clang' variants if they exist.
    """
    suffixes = ["build", "build-clang"] if use_clang else ["build"]

    client_dir = os.path.join(project_dir, "client")
    server_dir = os.path.join(project_dir, "server")

    client_builds = [(s, os.path.join(client_dir, s)) for s in suffixes]
    server_builds = [(s, os.path.join(server_dir, s)) for s in suffixes]

    client_exist = [(s, p) for s, p in client_builds if os.path.isdir(p)]
    server_exist = [(s, p) for s, p in server_builds if os.path.isdir(p)]

    if client_exist or server_exist:
        targets = []
        for suffix, path in client_exist:
            label = "client" if suffix == "build" else "client (clang)"
            targets.append((label, path))
        for suffix, path in server_exist:
            label = "server" if suffix == "build" else "server (clang)"
            targets.append((label, path))
        return targets

    targets = []
    for suffix in suffixes:
        path = os.path.join(project_dir, suffix)
        if os.path.isdir(path):
            label = "" if suffix == "build" else "clang"
            targets.append((label, path))
    return targets


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_samples(use_clang=False):
    repo_root    = os.path.dirname(os.path.abspath(__file__))
    target_dirs = [
        os.path.join(repo_root, "interop"),
        os.path.join(repo_root, "example", "sample-projects"),
        os.path.join(repo_root, "example", "sample-interop"),
        os.path.join(repo_root, "example", "cellutron"),
        os.path.join(repo_root, "test"),
        os.path.join(repo_root, "tools")
    ]

    clang_found = shutil.which("clang++") is not None
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
        use_clang = False
    elif use_clang and not clang_found:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
        use_clang = False

    os_label = "Windows" if IS_WINDOWS else "Linux"

    print("=" * 60)
    print("  DelegateMQ Sample Builder")
    print("=" * 60)
    print(f"  Platform : {os_label}")
    print(f"  Config   : {BUILD_CONFIG}")
    if use_clang:
        print(f"  Clang    : enabled (build-clang/ variants included)")
    print()
    print("  WARNING: This will build all projects in samples, test, and tools.")
    print("  Compilation may take several minutes depending on")
    print("  the number of projects and machine speed.")
    print("=" * 60)
    # answer = input("\nContinue? [y/N]: ").strip().lower()
    # if answer != "y":
    #     print("Aborted.")
    #     return True
    print()

    passed  = []
    failed  = []
    skipped = []

    for target_dir in target_dirs:
        if not os.path.isdir(target_dir):
            print(f"Warning: directory not found: {target_dir}")
            continue

        # 1. Check if the target_dir ITSELF is a configured project
        targets = collect_build_dirs(target_dir, use_clang=use_clang)
        if targets:
            project_name = os.path.basename(target_dir)
            project_failed = False
            for label, build_dir in targets:
                tag = f"{project_name}/{label}" if label else project_name
                print(f"[BUILDING] {tag}")
                success, err = build_project(build_dir, label)
                if success:
                    print(f"   ok")
                    if project_name == "native" and os.path.basename(target_dir) == "interop":
                        copy_interop_dll(build_dir, repo_root)
                else:
                    print(f"   FAILED")
                    lines = err.strip().splitlines()
                    for line in lines[-15:]:
                        print(f"   {line}")
                    project_failed = True
            if project_failed:
                failed.append(project_name)
            else:
                passed.append(project_name)

        # 2. Iterate through subdirectories
        for project_name in sorted(os.listdir(target_dir)):
            project_dir = os.path.join(target_dir, project_name)
            if not os.path.isdir(project_dir):
                continue

            # Skip common folders within test/tools that are not projects
            # Also skip 'unit-tests' as it is built as part of the 'test' project.
            if project_name in ["common", "include", "src", "unit-tests"]:
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

            targets = collect_build_dirs(project_dir, use_clang=use_clang)
            if not targets:
                # If it's a directory but has no build dir, it might not be a cmake project
                # or just hasn't been configured. 
                # Check if it has a CMakeLists.txt to decide if we should skip silently
                if os.path.isfile(os.path.join(project_dir, "CMakeLists.txt")):
                    skipped.append((project_name, "not configured — run 03_generate_samples.py first"))
                continue

            project_failed = False
            for label, build_dir in targets:
                tag = f"{project_name}/{label}" if label else project_name
                print(f"[BUILDING] {tag}")
                success, err = build_project(build_dir, label)
                if success:
                    print(f"   ok")
                    if project_name == "native" and os.path.basename(target_dir) == "interop":
                        copy_interop_dll(build_dir, repo_root)
                else:
                    print(f"   FAILED")
                    # Print the last 15 lines of compiler output for context
                    lines = err.strip().splitlines()
                    for line in lines[-15:]:
                        print(f"   {line}")
                    project_failed = True

            # Build any dotnet sub-projects (e.g. C# interop clients) — Windows only
            for subdir_name in (DOTNET_BUILDS.get(project_name, []) if IS_WINDOWS else []):
                subdir = os.path.join(project_dir, subdir_name)
                tag = f"{project_name}/{subdir_name}"
                print(f"[BUILDING] {tag}  (dotnet)")
                success, err = build_dotnet(subdir, subdir_name)
                if success:
                    print(f"   ok")
                else:
                    print(f"   FAILED")
                    lines = err.strip().splitlines()
                    for line in lines[-15:]:
                        print(f"   {line}")
                    project_failed = True

            if project_failed:
                failed.append(project_name)
            else:
                passed.append(project_name)

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
    parser = argparse.ArgumentParser(description="Build all DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also build build-clang/ directories configured by 03_generate_samples.py --clang (Linux only)."
    )
    args = parser.parse_args()
    success = build_samples(use_clang=args.clang)
    sys.exit(0 if success else 1)

# Post-Build Executable Locations (Windows Release):
# 1. Main test executable: 
#    DelegateMQ/test/build/Release/delegate_app.exe
# 2. Tools executables (dmq-spy, dmq-monitor, dmq-target):
#    DelegateMQ/tools/build/Release/
# 3. Sample project executables:
#    DelegateMQ/example/sample-projects/<project_name>/build/Release/
#    (Note: For client/server samples, they are in the 'client' and 'server' sub-folders)
