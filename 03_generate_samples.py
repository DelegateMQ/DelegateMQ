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
    7. Cellutron ThreadX variant: On Linux, also configures a 'build-threadx'
       directory (-DCELLUTRON_RTOS=THREADX) alongside the default FreeRTOS
       'build' directory, proving DelegateMQ isolates app code from the RTOS.
    8. Pumptron: configures the desktop build (GUI + FreeRTOS-simulator
       controller) from example/pumptron only, and -- when Ninja,
       arm-none-eabi-gcc and the STM32Cube FW_F4 package are available --
       the STM32F4 firmware cross-build into example/pumptron/build/f4.

Usage:
    Run this script THIRD to generate project files for your IDE.
    > python 03_generate_samples.py
    > python 03_generate_samples.py --clang
"""

import argparse
import glob
import os
import platform
import shutil
import subprocess

IS_WINDOWS = platform.system() == "Windows"

def find_stm32_f4_repo():
    """Locate the STM32Cube FW_F4 package (HAL/CMSIS/BSP) for Pumptron's firmware."""
    candidates = []
    if os.environ.get("STM32_REPO"):
        candidates.append(os.environ["STM32_REPO"])
    home = os.path.expanduser("~")
    candidates += sorted(glob.glob(os.path.join(home, "STM32Cube", "Repository", "STM32Cube_FW_F4_V*")), reverse=True)
    for c in candidates:
        if os.path.isdir(os.path.join(c, "Drivers", "STM32F4xx_HAL_Driver")):
            return c
    return None


def find_arm_gcc():
    """arm-none-eabi-gcc from ARM_TOOLCHAIN_DIR, STM32CubeIDE's bundled GCC, or PATH."""
    exe = "arm-none-eabi-gcc.exe" if IS_WINDOWS else "arm-none-eabi-gcc"
    if os.environ.get("ARM_TOOLCHAIN_DIR"):
        p = os.path.join(os.environ["ARM_TOOLCHAIN_DIR"], exe)
        if os.path.isfile(p):
            return p
    patterns = [
        "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin/" + exe,
        "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/" + exe,
    ]
    for pattern in patterns:
        hits = sorted(glob.glob(pattern), reverse=True)
        if hits:
            return hits[0]
    return shutil.which("arm-none-eabi-gcc")


def configure_pumptron_f4(pumptron_dir):
    """Configure Pumptron's STM32F4 firmware (Ninja, Release) into build/f4, if the
    cross toolchain and STM32Cube FW_F4 package are available. The toolchain file
    finds the compiler itself; these checks only decide whether to try."""
    label = "pumptron (STM32F4 firmware)"
    missing = []
    if not shutil.which("ninja"):
        missing.append("ninja")
    if not find_arm_gcc():
        missing.append("arm-none-eabi-gcc (STM32CubeIDE or Arm GNU Toolchain)")
    stm32_repo = find_stm32_f4_repo()
    if not stm32_repo:
        missing.append("STM32Cube FW_F4 package (set STM32_REPO)")
    if missing:
        print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
        return

    print(f"[CONFIGURING] {label}")
    cmd = ["cmake", "-S", os.path.join("controller", "platform", "f4"),
           "-B", os.path.join("build", "f4"), "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release", f"-DSTM32_REPO={stm32_repo}"]
    try:
        subprocess.run(cmd, cwd=pumptron_dir, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        print("   Success!")
    except subprocess.CalledProcessError as e:
        print("   FAILED (STM32F4 firmware)")
        for line in (e.stderr or "").splitlines():
            if "CMake Error" in line or "FATAL_ERROR" in line:
                print(f"   Reason: {line.strip()}")
                break


def build_samples(use_clang=False, clean=False):
    clang_exe = shutil.which("clang++") if (use_clang and not IS_WINDOWS) else None
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
    elif use_clang and not clang_exe:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
    elif use_clang and clang_exe:
        print(f"NOTE: --clang enabled. Using {clang_exe} for additional 'build-clang' directories.\n")

    if clean:
        print("NOTE: --clean enabled. Old build directories will be removed.\n")

    repo_root = os.path.dirname(os.path.abspath(__file__))
    target_dirs = [
        os.path.join(repo_root, "interop"),
        os.path.join(repo_root, "example", "sample-projects"),
        os.path.join(repo_root, "example", "sample-interop"),
        os.path.join(repo_root, "example", "cellutron"),
        os.path.join(repo_root, "example", "pumptron"),
        os.path.join(repo_root, "test"),
        os.path.join(repo_root, "tools")
    ]

    for target_dir in target_dirs:
        if not os.path.exists(target_dir):
            print(f"Warning: Could not find directory: {target_dir}")
            continue

        print(f"Generating projects in: {target_dir}\n")

        for dirpath, dirnames, files in os.walk(target_dir):
            # 1. CLEANUP: Avoid recursing into build/install folders
            if "build" in dirnames: dirnames.remove("build")
            if "install" in dirnames: dirnames.remove("install")
            
            # SKIP: Explicitly skip directories to prevent processing as standalone apps
            # zephyr-linux/zephyr-udp-serializer/databus-zephyr/cmsis-rtos2-linux need
            # `west build` + a west workspace, not `cmake -B build`
            skip_dirs = ["bare-metal-arm", "unit-tests", "atfe-armv7m-bare-metal", "stm32-freertos", "zephyr-linux", "zephyr-udp-serializer", "databus-zephyr", "cmsis-rtos2-linux"]
            for sd in skip_dirs:
                if sd in dirnames: dirnames.remove(sd)

            # Pumptron is configured once from its top-level CMakeLists.txt
            # (gui + simulator controller). Don't descend: its sub-projects are
            # built by that top level, and controller/platform/f4 is a
            # cross-compiled firmware configured separately below.
            if os.path.basename(dirpath) == "pumptron":
                dirnames.clear()

            if "CMakeLists.txt" in files:
                project_name = os.path.basename(dirpath)
                parent_name  = os.path.basename(os.path.dirname(dirpath))

                # --- SKIP COMMON/SHARED FOLDERS ---
                if project_name in ["common", "include", "src", "bare-metal-arm", "atfe-armv7m-bare-metal", "bare-metal-riscv"]:
                    # These are sub-libraries or skipped platforms, not standalone apps
                    continue

                # --- SKIP WINDOWS-ONLY PROJECTS ON LINUX ---
                # databus-freertos requires the FreeRTOS Win32 simulator (cmake -A Win32),
                # which is only available on Windows.  Attempting to configure it on Linux
                # produces a noisy cmake error with no useful output.
                WINDOWS_ONLY_PROJECTS = {"databus-freertos", "stm32-freertos"}
                if not IS_WINDOWS and (project_name in WINDOWS_ONLY_PROJECTS or
                                       parent_name in WINDOWS_ONLY_PROJECTS):
                    continue

                # --- SKIP LINUX-ONLY PROJECTS ON WINDOWS ---
                # freertos-linux and threadx-linux are native Linux/GNU simulator
                # ports; their own CMakeLists.txt refuses to configure on Windows
                # (FATAL_ERROR). Skip them here instead of letting that surface as
                # a noisy FAILED entry on every Windows run.
                LINUX_ONLY_PROJECTS = {"freertos-linux", "threadx-linux"}
                if IS_WINDOWS and (project_name in LINUX_ONLY_PROJECTS or
                                   parent_name in LINUX_ONLY_PROJECTS):
                    continue

                # Build a display label that includes the parent for client/server sub-dirs
                if project_name in ("client", "server"):
                    display_name = f"{parent_name}/{project_name}"
                else:
                    display_name = project_name

                # 2. CLEAN: Delete existing build folder if --clean set
                build_path = os.path.join(dirpath, "build")
                if clean and os.path.exists(build_path):
                    try: shutil.rmtree(build_path)
                    except: pass

                # 3. CONFIGURE
                # Win32 is required for the FreeRTOS Windows simulator port.
                # Match standalone projects (e.g. "freertos-bare-metal") by project_name,
                # and client/server sub-projects by parent_name + role.  The client side of
                # a mixed-platform project (e.g. databus-freertos/client) must NOT use Win32.
                # Exclude "*-linux" projects (e.g. "freertos-linux"): they use the FreeRTOS
                # POSIX simulator port, which is native x86_64 Linux, not Win32.
                needs_win32 = (
                    ("freertos" in project_name.lower() and not project_name.lower().endswith("-linux")) or
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

                # --- PUMPTRON STM32F4 FIRMWARE CONFIGURE ---
                if project_name == "pumptron":
                    configure_pumptron_f4(dirpath)

                # --- CELLUTRON THREADX CONFIGURE (Linux only) ---
                # Cellutron's controller/safety nodes support a CELLUTRON_RTOS
                # switch (FREERTOS default / THREADX) proving DelegateMQ isolates
                # app code from the RTOS choice. Configure a second build-threadx/
                # directory alongside the default build/ so both are exercised.
                # ThreadX's Linux/GNU simulation port requires native Linux
                # (UNIX AND NOT APPLE) -- see src/delegate-mq/External.cmake.
                if project_name == "cellutron" and platform.system() == "Linux":
                    build_threadx_path = os.path.join(dirpath, "build-threadx")
                    if clean and os.path.exists(build_threadx_path):
                        try: shutil.rmtree(build_threadx_path)
                        except: pass
                    print(f"[CONFIGURING] {display_name} (ThreadX)")
                    cmd_threadx = ["cmake", "-B", "build-threadx", "-DCELLUTRON_RTOS=THREADX", "."]
                    try:
                        subprocess.run(
                            cmd_threadx, cwd=dirpath, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                        )
                        print("   Success!")
                    except subprocess.CalledProcessError as e:
                        print(f"   FAILED (ThreadX)")
                        if e.stderr:
                            lines = e.stderr.split('\n')
                            for i, line in enumerate(lines):
                                if "CMake Error" in line or "Could not find" in line or "FATAL_ERROR" in line:
                                    print(f"   Reason: {line.strip()}")
                                    if i+1 < len(lines): print(f"          {lines[i+1].strip()}")
                                    break

                # --- CLANG CONFIGURE (optional, Linux only) ---
                if clang_exe and not needs_win32:
                    build_clang_path = os.path.join(dirpath, "build-clang")
                    if clean and os.path.exists(build_clang_path):
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

                # If we found a CMakeLists.txt, we treat this as a standalone project
                # and stop recursing into its subdirectories.
                dirnames.clear()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Configure all DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also configure a build-clang/ directory using clang++ (Linux only)."
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Remove existing build directories before configuring."
    )
    args = parser.parse_args()
    build_samples(use_clang=args.clang, clean=args.clean)