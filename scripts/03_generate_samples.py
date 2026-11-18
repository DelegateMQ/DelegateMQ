"""
Script: 03_generate_samples.py
Description:
    Configuration runner for all DelegateMQ sample projects.
    This script recursively finds every example project and generates the
    Visual Studio solutions (or Makefiles) required to browse or compile them.

    IMPORTANT: It does NOT compile the code. It only prepares the build directory.

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

Key Features:
    1. Smart Discovery: Recursively scans for 'CMakeLists.txt'.
    2. Exclusion Logic: Automatically skips shared library folders ('common', 'include', 'src')
       and samples that need tools these scripts don't run yet (Zephyr).
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
    9. QEMU targets: configures example/sample-projects/bare-metal/qemu and
       os/freertos-bare-metal once per target (build-<target>, see
       full_suite.QEMU_TARGET_SAMPLES) whose cross toolchain and QEMU are
       installed. os/freertos-bare-metal's Win32 simulator build is extra.
    10. Full suite (--full): also configures the extra unit-test builds in
       test/ listed in full_suite.TEST_VARIANTS (sanitizers, the
       DMQ_ALLOCATOR/DMQ_ASSERTS build, stress tests). On Linux a missing
       clang++ is then an error, not a skip.

Usage:
    Run this script THIRD to generate project files for your IDE.
    > python scripts/03_generate_samples.py
    > python scripts/03_generate_samples.py --clang
    > python scripts/03_generate_samples.py --full --clang
"""

import argparse
import glob
import os
import platform
import shutil
import subprocess
import sys

import full_suite

IS_WINDOWS = platform.system() == "Windows"

# (name, reason) of every configure that failed; reported at the end and
# turned into a non-zero exit code so build_all.py shows this step FAILED.
FAILED = []

def report_failure(name, stderr):
    """Print the first CMake error line (plus one line of context) and record it."""
    reason = ""
    lines = (stderr or "").splitlines()
    for i, line in enumerate(lines):
        if "CMake Error" in line or "Could not find" in line or "FATAL_ERROR" in line:
            reason = line.strip()
            print(f"   Reason: {reason}")
            if i + 1 < len(lines) and lines[i + 1].strip():
                print(f"          {lines[i + 1].strip()}")
                reason += " " + lines[i + 1].strip()
            break
    FAILED.append((name, reason))

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
    """Configure Pumptron's STM32F4 firmware (Ninja, Release) into its f4 build, if the
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
        # Drop a stale firmware tree from an earlier configure so
        # 04_build_samples.py (which builds the f4 build if present) skips it too.
        stale = full_suite.build_dir(pumptron_dir, "f4")
        if os.path.isdir(stale):
            shutil.rmtree(stale, ignore_errors=True)
        return

    print(f"[CONFIGURING] {label}")
    cmd = ["cmake", "-S", os.path.join("controller", "platform", "f4"),
           "-B", full_suite.build_dir(pumptron_dir, "f4"), "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release", f"-DSTM32_REPO={stm32_repo}"]
    try:
        subprocess.run(cmd, cwd=pumptron_dir, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        print("   Success!")
    except subprocess.CalledProcessError as e:
        print("   FAILED (STM32F4 firmware)")
        report_failure(label, e.stderr)


def configure_test_variants(test_dir, clean):
    """Configure the --full unit-test builds (full_suite.TEST_VARIANTS) in test/."""
    clang_exe = shutil.which("clang++") if not IS_WINDOWS else None
    for v in full_suite.test_variants():
        label = f"test ({v['suffix'].replace('build-', '', 1)})"
        path = full_suite.build_dir(test_dir, v["suffix"])
        if clean and os.path.exists(path):
            shutil.rmtree(path, ignore_errors=True)
        cmd = ["cmake", "-B", path]
        if v["clang"] and not IS_WINDOWS:
            if not clang_exe:
                print(f"[CONFIGURING] {label}\n   FAILED: clang++ not found in PATH")
                FAILED.append((label, "clang++ not found in PATH"))
                continue
            cmd.append(f"-DCMAKE_CXX_COMPILER={clang_exe}")
        cross = v.get("cross")
        if cross:
            missing = full_suite.cross_missing(cross)
            if missing:
                print(f"[CONFIGURING] {label}\n   FAILED: missing {', '.join(missing)}")
                FAILED.append((label, "missing " + ", ".join(missing)))
                continue
            cmd += full_suite.cross_cmake_args(cross)
        cmd += v["cmake"] + ["."]
        print(f"[CONFIGURING] {label}")
        try:
            subprocess.run(cmd, cwd=test_dir, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            print("   Success!")
        except subprocess.CalledProcessError as e:
            print(f"   FAILED")
            report_failure(label, e.stderr)


def configure_bare_metal(sample_dir, clean, full):
    """Configure build-<target> for each QEMU target of the sample
    (full_suite.QEMU_TARGET_SAMPLES) whose toolchain and QEMU are installed.
    Under --full a missing one is an error on Linux; on Windows the Linux run
    (Docker, every toolchain) covers it."""
    sample = full_suite.sample_name(sample_dir)
    for target in full_suite.QEMU_TARGET_SAMPLES[sample]:
        label = f"{sample}/{target}"
        missing = full_suite.bare_metal_missing(target, sample)
        if missing:
            print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
            if full and not IS_WINDOWS:
                FAILED.append((label, "--full: missing " + ", ".join(missing)))
            continue
        build_dir = full_suite.build_dir(sample_dir, f"build-{target}")
        if clean and os.path.exists(build_dir):
            shutil.rmtree(build_dir, ignore_errors=True)
        cmd = ["cmake", "-B", build_dir, f"-DBM_TARGET={target}", "."]
        if shutil.which("ninja"):
            cmd[3:3] = ["-G", "Ninja"]
        print(f"[CONFIGURING] {label}")
        try:
            subprocess.run(cmd, cwd=sample_dir, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            print("   Success!")
        except subprocess.CalledProcessError as e:
            print("   FAILED")
            report_failure(label, e.stderr)


def configure_transport(sample_dir, clean, clang_exe):
    """transport/serializer: configure build-<transport>-<serializer> for each
    combination this platform runs (full_suite.transport_configs()), and the
    -clang variant of each when clang_exe is given."""
    sample = full_suite.sample_name(sample_dir)
    variants = [("", [])]
    if clang_exe:
        variants.append(("-clang", [f"-DCMAKE_CXX_COMPILER={clang_exe}"]))
    for name, transport, serializer in full_suite.transport_configs():
        for suffix, extra in variants:
            build_dir = full_suite.build_dir(sample_dir, f"build-{name}{suffix}")
            label = f"{sample}/{name}" + (" (clang)" if suffix else "")
            if clean and os.path.exists(build_dir):
                shutil.rmtree(build_dir, ignore_errors=True)
            cmd = (["cmake", "-B", build_dir, f"-DSAMPLE_TRANSPORT={transport}",
                    f"-DSAMPLE_SERIALIZER={serializer}"] + extra + ["."])
            print(f"[CONFIGURING] {label}")
            if run_configure(cmd, sample_dir, label):
                print("   Success!")


def run_configure(cmd, cwd, label, env=None):
    """Run one configure command; print and record the outcome. Returns True on success."""
    try:
        subprocess.run(cmd, cwd=cwd, check=True, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return True
    except subprocess.CalledProcessError as e:
        print("   FAILED")
        report_failure(label, (e.stderr or "") + (e.stdout or ""))
        return False


def configure_host(project_dir, label, clean):
    """Configure an ordinary CMake project into its default build."""
    build_dir = full_suite.build_dir(project_dir)
    if clean and os.path.exists(build_dir):
        shutil.rmtree(build_dir, ignore_errors=True)
    print(f"[CONFIGURING] {label}")
    if run_configure(["cmake", "-B", build_dir, "."], project_dir, label):
        print("   Success!")


def configure_zephyr(project_dir, label, clean, full):
    """Configure a Zephyr app for native_sim into its default build with plain
    CMake (-DBOARD), against ZEPHYR_BASE; Zephyr's cache goes inside the build."""
    missing = full_suite.zephyr_missing()
    if missing:
        print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
        if full:
            FAILED.append((label, "--full: missing " + ", ".join(missing)))
        return
    build_dir = full_suite.build_dir(project_dir)
    if clean and os.path.exists(build_dir):
        shutil.rmtree(build_dir, ignore_errors=True)
    cmd = ["cmake", "-B", build_dir, f"-DBOARD={full_suite.ZEPHYR_BOARD}",
           f"-DUSER_CACHE_DIR={os.path.join(build_dir, 'zephyr-cache')}", "."]
    if shutil.which("ninja"):
        cmd[3:3] = ["-G", "Ninja"]
    print(f"[CONFIGURING] {label} (Zephyr {full_suite.ZEPHYR_BOARD})")
    if run_configure(cmd, project_dir, label):
        print("   Success!")


def configure_zephyr_qemu(project_dir, name, clean, full):
    """Configure a Zephyr app for its Cortex-M board (full_suite.ZEPHYR_QEMU_TARGETS)
    into its own build directory, with the Zephyr SDK's Arm toolchain."""
    spec = full_suite.ZEPHYR_QEMU_TARGETS[name]
    label = f"{name}/{spec['board'].replace('/', '-')}"
    missing = full_suite.zephyr_qemu_missing()
    if missing:
        print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
        if full:
            FAILED.append((label, "--full: missing " + ", ".join(missing)))
        return
    build_dir = full_suite.build_dir(project_dir, spec["build"])
    if clean and os.path.exists(build_dir):
        shutil.rmtree(build_dir, ignore_errors=True)
    cmd = ["cmake", "-B", build_dir, f"-DBOARD={spec['board']}",
           "-DZEPHYR_TOOLCHAIN_VARIANT=zephyr",
           f"-DUSER_CACHE_DIR={os.path.join(build_dir, 'zephyr-cache')}", "."]
    if shutil.which("ninja"):
        cmd[3:3] = ["-G", "Ninja"]
    print(f"[CONFIGURING] {label} (Zephyr {spec['board']}, QEMU)")
    if run_configure(cmd, project_dir, label):
        print("   Success!")


def configure_nuttx(sample_dir, repo_root, clean, full):
    """Set up os/nuttx's NuttX build: copy NUTTX_SRC/nuttx to build/nuttx, link
    the sample's apps/ beside it as dmq_apps (NuttX wants -a relative to nuttx/),
    then configure sim:cxxtest with the sample's entry point and libc++, as its
    README describes. An already configured tree is kept, so builds are
    incremental (--clean starts over)."""
    label = full_suite.NUTTX_SAMPLE
    missing = full_suite.nuttx_missing()
    if missing:
        print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
        if full:
            FAILED.append((label, "--full: missing " + ", ".join(missing)))
        return
    build_root = full_suite.build_dir(sample_dir)
    nuttx_dir = os.path.join(build_root, "nuttx")
    if clean and os.path.exists(build_root):
        shutil.rmtree(build_root, ignore_errors=True)
    if os.path.isfile(os.path.join(nuttx_dir, ".config")):
        print(f"[CONFIGURING] {label} (NuttX sim)\n   Already configured.")
        return

    print(f"[CONFIGURING] {label} (NuttX sim)")
    if os.path.exists(build_root):
        shutil.rmtree(build_root, ignore_errors=True)   # a half-done earlier attempt
    os.makedirs(build_root)
    shutil.copytree(os.path.join(os.environ["NUTTX_SRC"], "nuttx"), nuttx_dir, symlinks=True)
    os.symlink(os.path.join(sample_dir, "apps"), os.path.join(build_root, "dmq_apps"))
    # The sample's apps/Makefile runs during configure too and needs DELEGATEMQ_ROOT
    env = dict(os.environ, DELEGATEMQ_ROOT=repo_root)
    for cmd in (["./tools/configure.sh", "-a", "../dmq_apps", "sim:cxxtest"],
                ["kconfig-tweak", "--file", ".config", "--set-str", "CONFIG_INIT_ENTRYPOINT", "dmq_test_main"],
                ["kconfig-tweak", "--file", ".config", "--disable", "CONFIG_UCLIBCXX", "--enable", "CONFIG_LIBCXX"],
                ["kconfig-tweak", "--file", ".config", "--set-str", "CONFIG_CXX_STANDARD", "gnu++20"],
                ["make", "olddefconfig"]):
        if not run_configure(cmd, nuttx_dir, label, env=env):
            # Leave no .config behind, so the next run configures again
            config = os.path.join(nuttx_dir, ".config")
            if os.path.exists(config):
                os.remove(config)
            return
    print("   Success!")


def build_samples(use_clang=False, clean=False, full=False):
    clang_exe = shutil.which("clang++") if (use_clang and not IS_WINDOWS) else None
    if use_clang and IS_WINDOWS:
        print("NOTE: --clang is only supported on Linux. Clang builds skipped on Windows.\n")
    elif use_clang and not clang_exe:
        print("WARNING: --clang requested but clang++ not found in PATH. Clang builds skipped.\n")
        if full:
            FAILED.append(("clang builds", "--full: clang++ not found in PATH"))
    elif use_clang and clang_exe:
        print(f"NOTE: --clang enabled. Using {clang_exe} for additional 'build-clang' directories.\n")

    if clean:
        print("NOTE: --clean enabled. Old build directories will be removed.\n")

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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
            skip_dirs = ["unit-tests", "boards/stm32-freertos"]
            dirnames[:] = [d for d in dirnames if d not in skip_dirs
                           and full_suite.sample_name(os.path.join(dirpath, d)) not in skip_dirs]

            # Samples with QEMU targets have one build directory per target
            # (build-<target>). bare-metal has nothing else; os/freertos-bare-metal
            # also has its Win32 simulator build/, configured below on Windows.
            if full_suite.sample_name(dirpath) in full_suite.QEMU_TARGET_SAMPLES:
                configure_bare_metal(dirpath, clean, full)
                dirnames.clear()
                if full_suite.sample_name(dirpath) == full_suite.BARE_METAL_SAMPLE:
                    continue

            # transport/serializer: one build directory per combination
            if full_suite.sample_name(dirpath) == full_suite.TRANSPORT_SAMPLE:
                configure_transport(dirpath, clean, clang_exe)
                dirnames.clear()
                continue

            # Zephyr (native_sim) and NuttX (sim) samples: Linux only, each
            # configured its own way (see full_suite.py).
            name = full_suite.sample_name(dirpath)
            if (name in full_suite.ZEPHYR_SAMPLES or name == full_suite.ZEPHYR_DATABUS_SAMPLE
                    or name == full_suite.NUTTX_SAMPLE):
                dirnames.clear()
                if IS_WINDOWS:
                    continue
                if name == full_suite.NUTTX_SAMPLE:
                    configure_nuttx(dirpath, repo_root, clean, full)
                elif name == full_suite.ZEPHYR_DATABUS_SAMPLE:
                    configure_zephyr(os.path.join(dirpath, "server"), f"{name}/server", clean, full)
                    configure_host(os.path.join(dirpath, "client"), f"{name}/client", clean)
                else:
                    configure_zephyr(dirpath, name, clean, full)
                if name in full_suite.ZEPHYR_QEMU_TARGETS:
                    configure_zephyr_qemu(dirpath, name, clean, full)
                continue

            # Pumptron is configured once from its top-level CMakeLists.txt
            # (gui + simulator controller). Don't descend: its sub-projects are
            # built by that top level, and controller/platform/f4 is a
            # cross-compiled firmware configured separately below.
            if os.path.basename(dirpath) == "pumptron":
                dirnames.clear()

            if "CMakeLists.txt" in files:
                project_name = full_suite.sample_name(dirpath)
                parent_name  = full_suite.sample_name(os.path.dirname(dirpath))

                # --- SKIP COMMON/SHARED FOLDERS ---
                if project_name in ["common", "include", "src"]:
                    # These are sub-libraries or skipped platforms, not standalone apps
                    continue

                # --- SKIP WINDOWS-ONLY PROJECTS ON LINUX ---
                # databus/freertos and os/freertos-bare-metal require the FreeRTOS Win32 simulator (cmake -A Win32),
                # which is only available on Windows.  Attempting to configure it on Linux
                # produces a noisy cmake error with no useful output.
                WINDOWS_ONLY_PROJECTS = {"databus/freertos", "os/freertos-bare-metal", "os/win32", "boards/stm32-freertos"}
                if not IS_WINDOWS and (project_name in WINDOWS_ONLY_PROJECTS or
                                       parent_name in WINDOWS_ONLY_PROJECTS):
                    continue

                # --- SKIP LINUX-ONLY PROJECTS ON WINDOWS ---
                # os/freertos and os/threadx are native Linux/GNU simulator
                # ports, and os/posix uses the DMQ_THREAD_POSIX port; their own
                # CMakeLists.txt refuses to configure on Windows (FATAL_ERROR). Skip
                # them here instead of letting that surface as a noisy FAILED entry
                # on every Windows run.
                LINUX_ONLY_PROJECTS = {"os/freertos", "os/threadx", "os/posix", "os/qt", "transport/netx-udp"}
                if IS_WINDOWS and (project_name in LINUX_ONLY_PROJECTS or
                                   parent_name in LINUX_ONLY_PROJECTS):
                    continue

                # Build a display label that includes the parent for client/server sub-dirs
                # and for the nodes of multi-node projects (e.g. databus/relay/gateway)
                MULTI_NODE_PROJECTS = {"databus/relay", "transport/mqtt"}
                if project_name in ("client", "server") or parent_name in MULTI_NODE_PROJECTS:
                    display_name = f"{parent_name}/{project_name}"
                else:
                    display_name = project_name

                # 2. CLEAN: Delete existing build folder if --clean set
                build_path = full_suite.build_dir(dirpath)
                if clean and os.path.exists(build_path):
                    try: shutil.rmtree(build_path)
                    except: pass

                # 3. CONFIGURE
                # Win32 (32-bit) is required for the FreeRTOS Windows simulator port:
                # os/freertos-bare-metal's default build and databus/freertos's server
                # (its client is an ordinary host app). os/freertos uses the FreeRTOS
                # POSIX simulator, a native Linux build.
                WIN32_SIMULATOR_PROJECTS = {"os/freertos-bare-metal", "databus/freertos/server"}
                needs_win32 = (project_name in WIN32_SIMULATOR_PROJECTS
                               or f"{parent_name}/{project_name}" in WIN32_SIMULATOR_PROJECTS)
                
                cmd = ["cmake", "-B", build_path]
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
                    if "transport/serialport" in project_name:
                        print(f"   >> NOTICE: This project needs libserialport.")
                        print(f"   >> 02_build_libs.py builds it (MSBuild on Windows, autotools on Linux); check that step's output.")
                        print(f"   >> Otherwise see: {os.path.join(dirpath, 'README.md')}\n")

                    report_failure(display_name, e.stderr)

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
                    build_threadx_path = full_suite.build_dir(dirpath, "build-threadx")
                    if clean and os.path.exists(build_threadx_path):
                        try: shutil.rmtree(build_threadx_path)
                        except: pass
                    print(f"[CONFIGURING] {display_name} (ThreadX)")
                    cmd_threadx = ["cmake", "-B", build_threadx_path, "-DCELLUTRON_RTOS=THREADX", "."]
                    try:
                        subprocess.run(
                            cmd_threadx, cwd=dirpath, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                        )
                        print("   Success!")
                    except subprocess.CalledProcessError as e:
                        print(f"   FAILED (ThreadX)")
                        report_failure(f"{display_name} (ThreadX)", e.stderr)

                # --- CROSS-ENDIAN BUILD (Linux) ---
                # This sample's client and server are also built for a
                # big-endian CPU, so 05 can run one side under QEMU against a
                # native peer (see full_suite.CROSS_ENDIAN_SAMPLE).
                if (parent_name == full_suite.CROSS_ENDIAN_SAMPLE and project_name in ("client", "server")
                        and not IS_WINDOWS):
                    label = f"{display_name} ({full_suite.CROSS_ENDIAN_BUILD.replace('build-', '', 1)})"
                    missing = full_suite.cross_missing(full_suite.CROSS_ENDIAN_CPU)
                    if missing:
                        print(f"[SKIPPED] {label} -- missing: {', '.join(missing)}")
                        if full:
                            FAILED.append((label, "--full: missing " + ", ".join(missing)))
                    else:
                        cross_build = full_suite.build_dir(dirpath, full_suite.CROSS_ENDIAN_BUILD)
                        if clean and os.path.exists(cross_build):
                            shutil.rmtree(cross_build, ignore_errors=True)
                        print(f"[CONFIGURING] {label}")
                        cmd_cross = (["cmake", "-B", cross_build]
                                     + full_suite.cross_cmake_args(full_suite.CROSS_ENDIAN_CPU) + ["."])
                        if run_configure(cmd_cross, dirpath, label):
                            print("   Success!")

                # --- CLANG CONFIGURE (optional, Linux only) ---
                if clang_exe and not needs_win32:
                    build_clang_path = full_suite.build_dir(dirpath, "build-clang")
                    if clean and os.path.exists(build_clang_path):
                        try: shutil.rmtree(build_clang_path)
                        except: pass
                    print(f"[CONFIGURING] {display_name} (clang)")
                    cmd_clang = ["cmake", "-B", build_clang_path,
                                 f"-DCMAKE_CXX_COMPILER={clang_exe}", "."]
                    try:
                        subprocess.run(
                            cmd_clang, cwd=dirpath, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                        )
                        print("   Success!")
                    except subprocess.CalledProcessError as e:
                        print(f"   FAILED (clang)")
                        report_failure(f"{display_name} (clang)", e.stderr)

                # --- UNIT-TEST VARIANTS (--full) ---
                if full and dirpath == os.path.join(repo_root, "test"):
                    configure_test_variants(dirpath, clean)

                # If we found a CMakeLists.txt, we treat this as a standalone project
                # and stop recursing into its subdirectories.
                dirnames.clear()

    print("\n" + "=" * 60)
    if FAILED:
        print(f"CONFIGURE FAILED for {len(FAILED)} project(s):")
        for name, reason in FAILED:
            print(f"   {name:<40} {reason}")
        print("\nA missing third-party library usually means 01_fetch_repos.py or")
        print("02_build_libs.py didn't complete; check their output.")
    else:
        print("All projects configured.")
    print("=" * 60)
    return not FAILED

if __name__ == "__main__":
    import preflight
    if not preflight.check(need_git=False, need_cmake=True):
        sys.exit(1)
    parser = argparse.ArgumentParser(description="Configure all DelegateMQ sample projects.")
    parser.add_argument(
        "--clang", action="store_true",
        help="Also configure a build-clang/ directory using clang++ (Linux only)."
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Remove existing build directories before configuring."
    )
    parser.add_argument(
        "--full", action="store_true",
        help="Also configure the full-suite unit-test builds in test/ (see full_suite.py)."
    )
    args = parser.parse_args()
    sys.exit(0 if build_samples(use_clang=args.clang, clean=args.clean, full=args.full) else 1)