"""
Script: 04_build_samples.py
Description:
    Build orchestrator for all DelegateMQ sample projects.
    Assumes '03_generate_samples.py' has already been run to configure
    (generate) the build directories. This script compiles every project
    and reports pass/fail.

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

Key Features:
    1. Platform Awareness: Skips Windows-only projects on Linux and vice versa.
    2. Client/Server Support: Builds client and server sub-projects independently
       (and each node of a MULTI_NODE project, e.g. databus-relay).
    3. Build Config: Builds Release configuration on both Windows (MSVC
       multi-config) and Linux (single-config generators).
    4. Summary Report: Prints a final pass/fail/skipped table.
    5. Cellutron ThreadX variant: Also builds 'build-threadx/' (configured by
       03_generate_samples.py on Linux), proving DelegateMQ isolates app code
       from the RTOS choice.
    6. Pumptron: builds the desktop build (GUI + FreeRTOS-simulator
       controller) and, if 03_generate_samples.py configured it, the STM32F4
       firmware in example/pumptron/build/f4.
    7. Full suite (--full): also builds the extra unit-test builds in test/
       (full_suite.TEST_VARIANTS). A project that should have been configured
       but wasn't is then a failure, not a skip; samples --full doesn't cover
       yet (full_suite.PENDING) stay skipped.

Bare metal: builds each build-<target> of example/sample-projects/bare-metal
that 03_generate_samples.py configured (targets whose toolchain is installed).

Skip List (always excluded):
    - stm32-freertos          Embedded STM32 target, requires cross-compiler
    - system-architecture-python  Python-only client, no C++ build

Windows-only (skipped on Linux):
    - databus-freertos        Server uses FreeRTOS Win32 simulator (32-bit)

serialport-serializer needs libserialport, which 02_build_libs.py builds (MSBuild
on Windows, autotools on Linux); on Linux it is skipped if that build is missing.

Usage:
    Run this script FOURTH, after generating project files with 03_generate_samples.py.
    > python scripts/04_build_samples.py
    > python scripts/04_build_samples.py --full --clang
"""

import argparse
import os
import shutil
import sys
import platform
import subprocess

import full_suite

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SKIP_ALWAYS = {
    "stm32-freertos",
    "system-architecture-python",
}

# Projects made of more than two node apps, each in its own sub-directory with
# its own CMakeLists.txt (client/server projects need no entry).
MULTI_NODE = {
    "databus-relay": ["gateway", "monitor", "sensor"],
    "mqtt-rapidjson": ["pub", "sub"],
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
    "threadx-linux",           # ThreadX Linux/GNU simulation port; scheduling requires POSIX threads/signals
    "freertos-linux",          # FreeRTOS POSIX simulator port; each task is a real pthread
    "posix-linux",             # DMQ_THREAD_POSIX port; raw pthread_create, no Windows equivalent
    "qt-linux",                # DMQ_THREAD_QT port; Qt 6 Core (qt6-base-dev)
    "zephyr-linux",            # Zephyr native_sim (host process), configured with -DBOARD
    "zephyr-udp-serializer",
    "cmsis-rtos2-linux",
    "databus-zephyr",          # Zephyr native_sim server + host client
    "nuttx-sim",               # NuttX sim board, built with NuttX's own make
}

# Projects that contain dotnet sub-projects requiring 'dotnet build' in addition
# to any cmake targets.  Keys are project names; values are subdirectory names.
DOTNET_BUILDS = {
    "databus-interop": ["csharp-client"],
    "sample-interop": ["csharp"],
}

IS_WINDOWS = platform.system() == "Windows"
BUILD_CONFIG = "Release"
# Bare `--parallel` (no count) makes CMake pass a bare `-j` to Make, which is
# unlimited concurrent jobs, not one-per-core. Multi-subproject trees like
# cellutron can then fire off enough concurrent, template-heavy compiles to
# exhaust RAM+swap. Cap it explicitly, leaving one core free for the desktop.
BUILD_JOBS = max(1, (os.cpu_count() or 1) - 1)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_library(build_dir, lib_name):
    """Search common CMake output sub-directories for a built library."""
    suffix = ".dll" if IS_WINDOWS else ".so"
    # Linux adds 'lib' prefix to shared libraries
    full_name = lib_name if IS_WINDOWS else "lib" + lib_name
    
    candidates = [
        os.path.join(build_dir, "bin", BUILD_CONFIG, full_name + suffix),
        os.path.join(build_dir, "lib", BUILD_CONFIG, full_name + suffix),
        os.path.join(build_dir, "bin", full_name + suffix),
        os.path.join(build_dir, "lib", full_name + suffix),
        os.path.join(build_dir, BUILD_CONFIG, full_name + suffix),
        os.path.join(build_dir, full_name + suffix),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def copy_interop_dll(build_dir, repo_root):
    """Copy built DmqInterop DLL/SO to interop/python/."""
    lib_name = "DmqInterop"
    src = find_library(build_dir, lib_name)
    
    if src:
        dest_dir = os.path.join(repo_root, "interop", "python")
        dest = os.path.join(dest_dir, os.path.basename(src))
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

    cmd = ["cmake", "--build", build_dir, "--config", BUILD_CONFIG, "--parallel", str(BUILD_JOBS)]
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


def pumptron_f4_toolchain_present(f4_build_dir):
    """True if Pumptron's configured STM32F4 firmware tree can still be built:
    its CMakeCache.txt exists and the cross toolchain it recorded is present.
    (The toolchain file sets CMAKE_CXX_COMPILER as a normal variable, so it is
    not cached; CMAKE_CXX_COMPILER_AR is, and lives in the same bin directory.)"""
    cache = os.path.join(f4_build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return False
    with open(cache, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("CMAKE_CXX_COMPILER_AR:"):
                return os.path.isfile(line.split("=", 1)[1].strip())
    return False


def build_nuttx(sample_dir, repo_root, passed, failed, skipped):
    """Build nuttx-sim's NuttX tree (build/nuttx, set up by 03_generate_samples.py)
    with NuttX's make. -j1: building LLVM's libc++ in parallel has run out of
    memory on an 8 GB machine (see the sample's README)."""
    name = full_suite.NUTTX_SAMPLE
    nuttx_dir = os.path.join(sample_dir, "build", "nuttx")
    if not os.path.isfile(os.path.join(nuttx_dir, ".config")):
        skipped.append((name, "not configured — run 03_generate_samples.py first"))
        return
    print(f"[BUILDING] {name}  (NuttX make)")
    env = dict(os.environ, DELEGATEMQ_ROOT=repo_root)
    result = subprocess.run(["make", "-j1"], cwd=nuttx_dir, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode == 0:
        print("   ok")
        passed.append(name)
    else:
        print("   FAILED")
        for line in result.stdout.strip().splitlines()[-15:]:
            print(f"   {line}")
        failed.append(name)


def collect_build_dirs(project_dir, use_clang=False, extra_suffixes=None):
    """
    Return a list of (label, build_dir) tuples for a project.
    Client/server projects yield two entries; standalone yields one.
    When use_clang=True, also includes 'build-clang' variants if they exist.
    extra_suffixes: additional build-dir suffixes to check (e.g. 'build-threadx'
    for Cellutron's ThreadX variant) -- the label is derived from the suffix name.
    """
    suffixes = ["build", "build-clang"] if use_clang else ["build"]
    if extra_suffixes:
        suffixes = suffixes + list(extra_suffixes)

    def variant_label(suffix):
        return "" if suffix == "build" else suffix.replace("build-", "", 1)

    # Node sub-projects: client/server by default, or a MULTI_NODE project's nodes.
    roles = MULTI_NODE.get(os.path.basename(project_dir), ["client", "server"])

    targets = []
    for role in roles:
        for suffix in suffixes:
            path = os.path.join(project_dir, role, suffix)
            if os.path.isdir(path):
                variant = variant_label(suffix)
                targets.append((f"{role} ({variant})" if variant else role, path))
    if targets:
        return targets

    targets = []
    for suffix in suffixes:
        path = os.path.join(project_dir, suffix)
        if os.path.isdir(path):
            targets.append((variant_label(suffix), path))
    return targets


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_samples(use_clang=False, full=False):
    repo_root    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target_dirs = [
        os.path.join(repo_root, "interop"),
        os.path.join(repo_root, "example", "sample-projects"),
        os.path.join(repo_root, "example", "sample-interop"),
        os.path.join(repo_root, "example", "cellutron"),
        os.path.join(repo_root, "example", "pumptron"),
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
        clang_missing = full
    else:
        clang_missing = False

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
    if clang_missing:
        failed.append("clang builds (clang++ not found)")

    for target_dir in target_dirs:
        if not os.path.isdir(target_dir):
            print(f"Warning: directory not found: {target_dir}")
            continue

        project_name = os.path.basename(target_dir)

        # 1. Build any dotnet sub-projects for the target_dir itself (e.g. sample-interop/csharp)
        for subdir_name in (DOTNET_BUILDS.get(project_name, []) if IS_WINDOWS else []):
            subdir = os.path.join(target_dir, subdir_name)
            tag = f"{project_name}/{subdir_name}"
            print(f"[BUILDING] {tag}  (dotnet)")
            success, err = build_dotnet(subdir, subdir_name)
            if success:
                print(f"   ok")
                if project_name not in passed: passed.append(project_name)
            else:
                print(f"   FAILED")
                lines = err.strip().splitlines()
                for line in lines[-15:]:
                    print(f"   {line}")
                if project_name not in failed: failed.append(project_name)

        # 2. Check if the target_dir ITSELF is a configured project
        # Cellutron's controller/safety nodes get an extra 'build-threadx'
        # variant (see 03_generate_samples.py) proving DelegateMQ isolates
        # app code from the RTOS choice.
        extra_suffixes = ["build-threadx"] if project_name == "cellutron" else None
        # --full: test/ has extra unit-test builds (see full_suite.py).
        if full and project_name == "test":
            extra_suffixes = [v["suffix"] for v in full_suite.test_variants()]
            for s in extra_suffixes:
                if not os.path.isdir(os.path.join(target_dir, s)):
                    skipped.append((f"test/{s}", "not configured — run 03_generate_samples.py --full first"))
        targets = collect_build_dirs(target_dir, use_clang=use_clang, extra_suffixes=extra_suffixes)
        # Pumptron's STM32F4 firmware is a separate cross-compiled build tree
        # nested under build/ (configured only when the ARM toolchain exists).
        if project_name == "pumptron":
            f4_dir = os.path.join(target_dir, "build", "f4")
            if os.path.isdir(f4_dir):
                if pumptron_f4_toolchain_present(f4_dir):
                    targets.append(("f4", f4_dir))
                else:
                    print("[SKIPPED] pumptron/f4 -- ARM toolchain from its configure is no longer available")
                    skipped.append(("pumptron/f4", "ARM toolchain not found"))
        if targets:
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
                if project_name not in failed: failed.append(project_name)
            else:
                if project_name not in passed: passed.append(project_name)

            # If we built the target_dir itself, we skip iterating subdirectories.
            # This avoids building components of a multi-project (like cellutron) twice.
            continue

        # 3. Iterate through subdirectories
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
            if (project_name == full_suite.SERIAL_SAMPLE and not IS_WINDOWS
                    and not full_suite.serialport_lib_built(repo_root)):
                skipped.append((project_name, "libserialport not built (02_build_libs.py)"))
                continue

            if project_name == full_suite.NUTTX_SAMPLE:
                build_nuttx(project_dir, repo_root, passed, failed, skipped)
                continue

            bare_metal = project_name == full_suite.BARE_METAL_SAMPLE
            extra = [f"build-{t}" for t in full_suite.BARE_METAL_TARGETS] if bare_metal else None
            if project_name == full_suite.CROSS_ENDIAN_SAMPLE and not IS_WINDOWS:
                extra = [full_suite.CROSS_ENDIAN_BUILD]   # client/server for the big-endian CPU
            targets = collect_build_dirs(project_dir, use_clang=use_clang and not bare_metal,
                                         extra_suffixes=extra)
            if bare_metal and not targets and IS_WINDOWS:
                # No bare-metal toolchain here; the Linux run builds them
                skipped.append((project_name, full_suite.LINUX_RUN_COVERS))
                continue
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
    if full:
        skipped, not_built = full_suite.strict_split(skipped)
        for name, reason in not_built:
            failed.append(f"{name} ({reason})")
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
    import preflight
    if not preflight.check(need_git=False, need_cmake=True):
        sys.exit(1)
    parser = argparse.ArgumentParser(description="Build all DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also build build-clang/ directories configured by 03_generate_samples.py --clang (Linux only)."
    )
    parser.add_argument(
        "--full", action="store_true",
        help="Also build the full-suite unit-test builds in test/; anything not configured fails (see full_suite.py)."
    )
    args = parser.parse_args()
    success = build_samples(use_clang=args.clang, full=args.full)
    sys.exit(0 if success else 1)

# Post-Build Executable Locations (Windows Release):
# 1. Main test executable: 
#    DelegateMQ/test/build/Release/delegate_app.exe
# 2. Tools executables (dmq-spy, dmq-monitor, dmq-thread, dmq-target):
#    DelegateMQ/tools/bin/ (copied there after every tools build)
# 3. Sample project executables:
#    DelegateMQ/example/sample-projects/<project_name>/build/Release/
#    (Note: For client/server samples, they are in the 'client' and 'server' sub-folders)
