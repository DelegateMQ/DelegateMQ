"""
Script: 02_build_libs.py
Description:
    Automated build orchestrator for DelegateMQ dependencies.
    This script compiles the raw source code cloned by '01_fetch_repos.py' into 
    static libraries usable by the sample projects.

    See Examples Setup within DETAILS.md before running script.

Key Features:
    1. Cross-Platform Support: 
       - Windows: Handles Multi-Configuration builds (Debug & Release) and enforces 
         correct MSVC Runtime linking (/MDd vs /MD) to prevent LNK2038 mismatches.
       - Linux: Handles Single-Configuration generators by running separate 
         Configure/Build/Install steps for Debug and Release.
    2. ZeroMQ Fix: Automatically detects and renames the 'zeromq/INSTALL' file 
       to prevent directory creation conflicts on Windows.
    3. Static Linking: Forces all libraries (ZeroMQ, NNG, MQTT) to build as 
       Static Libraries (.lib/.a) to simplify application deployment.

Targets Built:
    - ZeroMQ (libzmq):  Static, No Tests, No Docs.
    - NNG:              Static, TLS Disabled (to reduce deps).
    - Eclipse Paho MQTT: Static, Samples Disabled.

Usage:
    Run this script SECOND to compile the static libraries.
    > python 02_build_libs.py
"""

import os
import shutil
import subprocess

# Define the libraries to compile
libs = [
    {
        "name": "nng",
        "path": "nng",
        "flags": ["-DNNG_ENABLE_TLS=OFF", "-DBUILD_SHARED_LIBS=OFF", "-DNNG_TESTS=OFF"]
    },
    {
        "name": "zeromq",
        "path": "zeromq",
        # Force static build, disable tests/docs to speed up
        # ADDED: -DCMAKE_POLICY_VERSION_MINIMUM=3.5 to fix build on modern CMake
        "flags": [
            "-DBUILD_SHARED=OFF", 
            "-DZMQ_BUILD_TESTS=OFF", 
            "-DWITH_DOCS=OFF", 
            "-DWITH_PERF_TOOL=OFF",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" 
        ] 
    },
    {
        "name": "mqtt",
        "path": "mqtt",
        # ADDED: -DCMAKE_POLICY_VERSION_MINIMUM=3.5 to fix build on modern CMake
        "flags": [
            "-DPAHO_BUILD_SHARED=TRUE", 
            "-DPAHO_BUILD_STATIC=TRUE", 
            "-DPAHO_BUILD_SAMPLES=FALSE",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
        ]
    }
]

def fix_zeromq_conflict(workspace_dir):
    """
    AUTOMATIC FIX: 
    Renames 'zeromq/INSTALL' (file) to 'zeromq/INSTALL.txt' 
    so CMake can create the 'zeromq/INSTALL' (directory).
    """
    zmq_path = os.path.join(workspace_dir, "zeromq")
    conflict_file = os.path.join(zmq_path, "INSTALL")
    
    # If the text file exists, rename it
    if os.path.isfile(conflict_file):
        try:
            new_name = os.path.join(zmq_path, "INSTALL.txt")
            # If INSTALL.txt already exists, just delete the conflict file
            if os.path.exists(new_name): 
                os.remove(conflict_file)
            else: 
                os.rename(conflict_file, new_name)
            print(f"[AUTO-FIX] Renamed conflicting file 'zeromq/INSTALL' to 'INSTALL.txt'")
        except Exception as e:
            print(f"[WARNING] Could not rename zeromq/INSTALL: {e}")

def build_deps():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_dir = os.path.abspath(os.path.join(script_dir, ".."))
    print(f"Workspace Root: {workspace_dir}\n")

    # 1. Run the Auto-Fix before building
    fix_zeromq_conflict(workspace_dir)

    for lib in libs:
        source_dir = os.path.join(workspace_dir, lib["path"])
        install_dir = os.path.join(source_dir, "install")
        
        if not os.path.isdir(source_dir):
            print(f"[ERROR] Missing {source_dir}"); continue

        print(f"--- Building {lib['name']} ---")
        
        # 2. Clean Build Directory (Start Fresh)
        build_dir = os.path.join(source_dir, "build")
        if os.path.exists(build_dir): shutil.rmtree(build_dir)
        
        # 3. Configure (Multi-Config for Visual Studio)
        #    We set CMAKE_DEBUG_POSTFIX to "d" so debug libs get named "libzmq-d.lib"
        cmd_config = [
            "cmake", "-B", "build", 
            f"-DCMAKE_INSTALL_PREFIX={install_dir}",
            "-DCMAKE_DEBUG_POSTFIX=d"
        ] + lib.get("flags", [])

        # ZeroMQ Specific: Ensure Runtime Library matches DelegateMQ (DLL vs Static)
        if lib["name"] == "zeromq":
             cmd_config.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        
        try:
            subprocess.run(cmd_config, cwd=source_dir, check=True)
            
            # 4. Build & Install RELEASE
            print(f"    [BUILD] Release...")
            subprocess.run(["cmake", "--build", "build", "--config", "Release", "--parallel"], cwd=source_dir, check=True)
            subprocess.run(["cmake", "--install", "build", "--config", "Release"], cwd=source_dir, check=True)

            # 5. Build & Install DEBUG
            print(f"    [BUILD] Debug...")
            subprocess.run(["cmake", "--build", "build", "--config", "Debug", "--parallel"], cwd=source_dir, check=True)
            subprocess.run(["cmake", "--install", "build", "--config", "Debug"], cwd=source_dir, check=True)

            print(f"    Success!\n")
            
        except subprocess.CalledProcessError:
            print(f"    [FAILED] {lib['name']}\n")

if __name__ == "__main__":
    build_deps()