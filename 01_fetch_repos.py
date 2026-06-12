"""
Script: 01_fetch_repos.py
Description: 
    Provisioning script for the DelegateMQ development environment.
    It handles the following tasks:
    1. Clones all required dependencies with specific version pinning to ensure stability.
    2. Initializes and updates Git submodules where necessary (e.g., FreeRTOS).
    3. Applies automated fixes to common build issues:
        - Renames 'zeromq/INSTALL' to 'INSTALL.txt' to prevent Windows file/folder conflicts.

    See Examples Setup within DETAILS.md before running script.

Dependencies Pinned:
    - ZeroMQ:        v4.3.5       (Stable C++ standard)
    - NNG:           v1.8.0       (Known compatibility)
    - MQTT:          v1.3.13      (Paho C stable release)
    - libserialport:              (Sigrok stable release)
    - RapidJSON:     v1.1.0       (Official release)
    - Spdlog:        v1.12.0      (Stable logging)
    - Cereal:        v1.3.2       (Serialization stable)
    - Bitsery:       v5.2.3       (Serialization stable)
    - MsgPack:       cpp-8.0.0    (Required for C++ headers)
    - FreeRTOS:      202212.00    (LTS Release)

Usage:
    Run this script FIRST to download source code.
    > python 01_fetch_repos.py
"""

import os
import subprocess
import shutil

# Repositories with specific stable tags/branches
repos = {
    # ZeroMQ: v4.3.5 is the industry standard for stability. 
    # (v4.3.6 exists but 4.3.5 is widely trusted for C++ wrappers)
    "zeromq": ("https://github.com/zeromq/libzmq.git", "v4.3.5", False),

    # NNG: Pinned to v1.8.0 (Known good with DelegateMQ)
    "nng": ("https://github.com/nanomsg/nng.git", "v1.8.0", False),

    # MQTT (Paho): v1.3.13 is the robust, long-term stable release.
    "mqtt": ("https://github.com/eclipse/paho.mqtt.c.git", "v1.3.13", False),

    # libserialport: v0.1.1 is the official stable tag from the sigrok project.
    "libserialport": ("https://github.com/sigrokproject/libserialport.git", "", False),

    # RapidJSON: Pinned to v1.1.0 (Official release). 
    # Note: This is old (2016) but is the only official tag.
    "rapidjson": ("https://github.com/Tencent/rapidjson.git", "v1.1.0", False),

    # Spdlog: v1.12.0 is highly stable and widely used.
    "spdlog": ("https://github.com/gabime/spdlog.git", "v1.12.0", False),

    # Cereal: v1.3.2 is the current stable release.
    "cereal": ("https://github.com/USCiLab/cereal.git", "v1.3.2", False),

    # Bitsery: v5.2.3 is a stable modern C++ release.
    "bitsery": ("https://github.com/fraillt/bitsery.git", "v5.2.3", False),

    # FTXUI: v5.0.0 for modern C++ terminal UI components.
    "ftxui": ("https://github.com/ArthurSonzogni/FTXUI.git", "v5.0.0", False),

    # MsgPack: 'cpp-8.0.0' is required for the C++ headers structure
    "msgpack-c": ("https://github.com/msgpack/msgpack-c.git", "cpp-8.0.0", False),

    # FreeRTOS: Kernel only (much smaller than distribution)
    "FreeRTOS": ("https://github.com/FreeRTOS/FreeRTOS-Kernel.git", "V11.1.0", False)
}

def fix_zeromq_conflict(workspace_dir):
    """Renames 'INSTALL' to 'INSTALL.txt' in zeromq to prevent build failure."""
    install_file = os.path.join(workspace_dir, "zeromq", "INSTALL")
    if os.path.isfile(install_file):
        try:
            os.rename(install_file, os.path.join(workspace_dir, "zeromq", "INSTALL.txt"))
            print("[FIX] Renamed zeromq/INSTALL to INSTALL.txt")
        except Exception as e:
            print(f"[WARN] Could not rename zeromq/INSTALL: {e}")

def run():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_dir = os.path.abspath(os.path.join(script_dir, ".."))
    print(f"Target Workspace: {workspace_dir}\n")

    # Suppress 'detached HEAD' advice to keep output clean
    subprocess.run(["git", "config", "advice.detachedHead", "false"], check=False)

    # 1. Clone Repos
    for folder, (url, tag, use_submodules) in repos.items():
        target_path = os.path.join(workspace_dir, folder)
        if not os.path.exists(target_path):
            print(f"[CLONING] {folder} (Tag: {tag if tag else 'HEAD'})...")
            # Use -c advice.detachedHead=false to silence the warning during clone
            # Use --depth 1 for shallow clone to save space/time
            cmd = ["git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1"]
            if use_submodules: cmd.append("--recurse-submodules")
            if tag: cmd.extend(["--branch", tag])
            cmd.extend([url, target_path])
            try:
                # Capture stderr to suppress normal output; printed only on failure
                subprocess.run(cmd, check=True, stderr=subprocess.PIPE)
            except subprocess.CalledProcessError as e:
                print(f"   [ERROR] Failed to clone {folder}")
                if e.stderr:
                    print(e.stderr.decode(errors="replace"))
        else:
            print(f"[SKIP] {folder} exists.")

    # 2. Apply Fixes
    fix_zeromq_conflict(workspace_dir)
    print("\nWorkspace setup complete!")

if __name__ == "__main__":
    run()