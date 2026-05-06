"""
Script: 03_generate_samples.py
Description:
    Configuration runner for all DelegateMQ sample projects.
    This script recursively finds every example project and generates the
    Visual Studio solutions (or Makefiles) required to browse or compile them.

    IMPORTANT: It does NOT compile the code. It only prepares the build directory.

    See Examples Setup within DETAILS.md before running script.

Key Features:
    1. Smart Discovery: Recursively scans for 'CMakeLists.txt'.
    2. Exclusion Logic: Automatically skips shared library folders ('common', 'include', 'src')
       and specific platforms like 'bare-metal-arm'.
    3. Clean Generation: Removes old 'build' directories to ensure a fresh configuration.
    4. Project Generation: Runs 'cmake -B build' to create .sln files ready for IDE usage.
    5. Special Handling:
       - FreeRTOS projects are automatically configured for 'Win32' architecture.
       - Standard projects use the default generator (x64 on modern Windows).
    6. Optional Clang builds (--clang): On Linux, also configures a 'build-clang'
       directory using clang++ for projects that support it. Requires clang++ in PATH.

Usage:
    Run this script THIRD to generate project files for your IDE.
    > python 03_generate_samples.py
    > python 03_generate_samples.py --clang
"""

import argparse
import os
import platform
import shutil
import subprocess

IS_WINDOWS = platform.system() == "Windows"

def build_samples(use_clang=False):
    clang_exe = shutil.which("clang++") if (use_clang and not IS_WINDOWS) else None
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
    elif use_clang and not clang_exe:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
    elif use_clang and clang_exe:
        print(f"NOTE: --clang enabled. Using {clang_exe} for additional 'build-clang' directories.\n")

    repo_root = os.path.dirname(os.path.abspath(__file__))
    target_dirs = [
        os.path.join(repo_root, "interop"),
        os.path.join(repo_root, "example", "sample-projects"),
        os.path.join(repo_root, "example", "sample-interop"),
        os.path.join(repo_root, "example", "cellutron"),
        os.path.join(repo_root, "test"),
        os.path.join(repo_root, "tools")
    ]

    for target_dir in target_dirs:
        if not os.path.exists(target_dir):
            print(f"Warning: Could not find directory: {target_dir}")
            continue

        print(f"Cleaning and Generating ALL projects in: {target_dir}\n")

        for dirpath, dirnames, files in os.walk(target_dir):
            # 1. CLEANUP: Avoid recursing into build/install folders
            if "build" in dirnames: dirnames.remove("build")
            if "install" in dirnames: dirnames.remove("install")
            
            # SKIP: Explicitly skip directories to prevent processing as standalone apps
            skip_dirs = ["bare-metal-arm", "unit-tests", "atfe-armv7m-bare-metal"]
            for sd in skip_dirs:
                if sd in dirnames: dirnames.remove(sd)

            if "CMakeLists.txt" in files:
                project_name = os.path.basename(dirpath)
                parent_name  = os.path.basename(os.path.dirname(dirpath))

                # --- SKIP COMMON/SHARED FOLDERS ---
                if project_name in ["common", "include", "src", "bare-metal-arm", "atfe-armv7m-bare-metal"]:
                    # These are sub-libraries or skipped platforms, not standalone apps
                    continue

                # --- SKIP WINDOWS-ONLY PROJECTS ON LINUX ---
                # databus-freertos requires the FreeRTOS Win32 simulator (cmake -A Win32),
                # which is only available on Windows.  Attempting to configure it on Linux
                # produces a noisy cmake error with no useful output.
                WINDOWS_ONLY_PROJECTS = {"databus-freertos"}
                if not IS_WINDOWS and (project_name in WINDOWS_ONLY_PROJECTS or
                                       parent_name in WINDOWS_ONLY_PROJECTS):
                    continue

                # Build a display label that includes the parent for client/server sub-dirs
                if project_name in ("client", "server"):
                    display_name = f"{parent_name}/{project_name}"
                else:
                    display_name = project_name

                # 2. CLEAN: Delete existing build folder
                build_path = os.path.join(dirpath, "build")
                if os.path.exists(build_path):
                    try: shutil.rmtree(build_path)
                    except: pass

                # 3. CONFIGURE
                # Win32 is required for the FreeRTOS Windows simulator port.
                # Match standalone projects (e.g. "freertos-bare-metal") by project_name,
                # and client/server sub-projects by parent_name + role.  The client side of
                # a mixed-platform project (e.g. databus-freertos/client) must NOT use Win32.
                needs_win32 = (
                    "freertos" in project_name.lower() or
                    ("freertos" in parent_name.lower() and project_name == "server")
                )
                
                cmd = ["cmake", "-B", "build"]
                if needs_win32:
                    cmd += ["-A", "Win32"]

                # Special handling for DmqInterop DLL: Default to UDP transport
                if project_name == "native" and parent_name == "interop":
                    transport = "DMQ_TRANSPORT_WIN32_UDP" if IS_WINDOWS else "DMQ_TRANSPORT_LINUX_UDP"
                    cmd.append(f"-DDMQ_TRANSPORT={transport}")

                cmd.append(".")

                print(f"[CONFIGURING] {display_name} {'(Win32)' if needs_win32 else ''}")

                try:
                    subprocess.run(
                        cmd, cwd=dirpath, check=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                    )
                    print("   Success!")
                    
                except subprocess.CalledProcessError as e:
                    print(f"   FAILED: {project_name}")

                    # --- SPECIAL HANDLING FOR SERIALPORT ---
                    if "serialport-serializer" in project_name:
                        print(f"   >> NOTICE: This project requires manual dependency setup.")
                        print(f"   >> Please read: {os.path.join(dirpath, 'README.md')}")
                        print(f"   >> You likely need to build 'libserialport' manually first.\n")

                    if e.stderr:
                        lines = e.stderr.split('\n')
                        for i, line in enumerate(lines):
                            if "CMake Error" in line or "Could not find" in line or "FATAL_ERROR" in line:
                                print(f"   Reason: {line.strip()}")
                                # Print a bit of context
                                if i+1 < len(lines): print(f"          {lines[i+1].strip()}")
                                break

                # --- CLANG CONFIGURE (optional, Linux only) ---
                if clang_exe and not needs_win32:
                    build_clang_path = os.path.join(dirpath, "build-clang")
                    if os.path.exists(build_clang_path):
                        try: shutil.rmtree(build_clang_path)
                        except: pass
                    print(f"[CONFIGURING] {display_name} (clang)")
                    cmd_clang = ["cmake", "-B", "build-clang",
                                 f"-DCMAKE_CXX_COMPILER={clang_exe}", "."]
                    try:
                        subprocess.run(
                            cmd_clang, cwd=dirpath, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                        )
                        print("   Success!")
                    except subprocess.CalledProcessError:
                        print(f"   FAILED (clang)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Configure all DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also configure a build-clang/ directory using clang++ (Linux only)."
    )
    args = parser.parse_args()
    build_samples(use_clang=args.clang)