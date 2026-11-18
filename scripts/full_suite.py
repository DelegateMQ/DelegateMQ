"""
Module: full_suite.py
Description:
    What `build_all.py --full` adds to a normal run. Shared by
    03_generate_samples.py, 04_build_samples.py and 05_run_samples.py so the
    three steps agree on what exists:

    - TEST_VARIANTS: extra builds of test/ (the unit-test app, delegate_app),
      each in its own build directory next to test/build. 03 configures them,
      04 builds them and 05 runs them.
    - PENDING: samples --full doesn't build or run yet, with the reason. They
      are reported as skipped. Under --full anything else that would be
      skipped (a missing tool, a sample that wasn't configured or built) is a
      failure, so a clean --full run means everything listed actually ran.

    Samples that only exist on the other platform (Windows-only on Linux and
    the reverse) are skipped as usual: the other platform's run covers them.
"""

import os
import platform
import shutil

IS_WINDOWS = platform.system() == "Windows"
IS_LINUX = platform.system() == "Linux"

# example/sample-projects/bare-metal/qemu: one build directory per target
# (build-<target>, configured with -DBM_TARGET=<target>), each run on QEMU.
# The program ends QEMU through semihosting with status 0 only if every check
# passed. tools: what configuring and running the target needs; "$VAR/..."
# is a path under that environment variable (or the default given), anything
# else an executable on the PATH.
BARE_METAL_SAMPLE = "bare-metal/qemu"
QEMU_SEMIHOSTING = ["-nographic", "-monitor", "none", "-serial", "none",
                    "-semihosting-config", "enable=on,target=native"]
BARE_METAL_TARGETS = {
    "arm-m4": {
        "tools": ["arm-none-eabi-gcc", "qemu-system-arm"],
        "qemu":  ["qemu-system-arm", "-M", "mps2-an386", "-cpu", "cortex-m4"],
    },
    "arm-m0": {
        "tools": ["arm-none-eabi-gcc", "qemu-system-arm"],
        "qemu":  ["qemu-system-arm", "-M", "microbit"],
    },
    "arm-m3-atfe": {
        "tools": [("ATFE_PATH", "C:/ATfE-22.1.0-Windows-x86_64", "bin/clang"), "qemu-system-arm"],
        "qemu":  ["qemu-system-arm", "-M", "mps2-an385", "-cpu", "cortex-m3"],
    },
    "riscv32": {
        "tools": [("RISCV_TOOLCHAIN_PATH", "/opt/xpack-riscv-none-elf-gcc", "bin/riscv-none-elf-gcc"),
                  "qemu-system-riscv32"],
        "qemu":  ["qemu-system-riscv32", "-M", "virt", "-bios", "none"],
    },
}
BARE_METAL_TIMEOUT = 60
BARE_METAL_SUCCESS = "ALL TESTS PASSED"

# example/sample-projects/os/freertos-bare-metal: its default build is the
# FreeRTOS Win32 simulator (Windows only); its targets/<name>/ run the same
# tests on a real Cortex-M under QEMU, on both platforms, and add a test of
# FreeRTOSCriticalSection from a hardware timer interrupt. Configured, built
# and run like the bare-metal/qemu targets.
FREERTOS_QEMU_SAMPLE = "os/freertos-bare-metal"
FREERTOS_QEMU_TARGETS = {
    "arm-m3": {
        "tools": ["arm-none-eabi-gcc", "qemu-system-arm"],
        "qemu":  ["qemu-system-arm", "-M", "mps2-an385", "-cpu", "cortex-m3"],
    },
}

# Samples with QEMU targets (build-<target> each, -DBM_TARGET=<target>)
QEMU_TARGET_SAMPLES = {
    BARE_METAL_SAMPLE: BARE_METAL_TARGETS,
    FREERTOS_QEMU_SAMPLE: FREERTOS_QEMU_TARGETS,
}

# Skip reason for a bare-metal target on Windows whose toolchain or QEMU isn't
# installed: the Linux run (docker/run.sh, which has them all) covers it.
LINUX_RUN_COVERS = "covered by the Linux run (Docker)"


# example/sample-projects/transport/serialport: a sender and a receiver on two
# serial ports connected to each other. On Linux the runner makes the pair with
# socat (two linked pseudo-terminals, a virtual null-modem cable); Windows has
# no such pair without a virtual COM-port driver, so the Linux run covers it.
SERIAL_SAMPLE = "transport/serialport"
SERIAL_PTYS = ("/tmp/ttyDMQ0", "/tmp/ttyDMQ1")
SERIAL_SUCCESS = "Data Message:"


# example/sample-projects/transport/mqtt: pub and sub apps talking through an
# MQTT broker. The runner starts mosquitto on its own port for the run (so it
# can't collide with a broker already on 1883) and passes it to both apps.
MQTT_SAMPLE = "transport/mqtt"
MQTT_PORT = 18830
MQTT_SUCCESS = "Data Message"


# example/sample-projects/transport/serializer: one source set built once per
# transport/serializer combination, in build-<transport>-<serializer>
# (-DSAMPLE_TRANSPORT, -DSAMPLE_SERIALIZER), plus build-<...>-clang on Linux
# with --clang. Each build is one process (sender and receiver both in it);
# it must exit 0 (it exits 1 if no message arrived intact) and print
# TRANSPORT_SUCCESS. These replaced the linux-/win32-*-serializer, zeromq-*
# and nnq-bitsery samples, one combination each.
TRANSPORT_SAMPLE = "transport/serializer"
TRANSPORT_COMBOS = [
    ("udp", "builtin"), ("tcp", "builtin"), ("pipe", "builtin"), ("uds", "builtin"),
    ("lwip", "builtin"), ("lwip-netconn", "builtin"),
    ("zeromq", "builtin"), ("zeromq", "bitsery"), ("zeromq", "cereal"),
    ("zeromq", "msgpack"), ("zeromq", "rapidjson"), ("nng", "bitsery"),
]
TRANSPORT_WINDOWS_ONLY = {"pipe"}   # Windows named pipe
TRANSPORT_LINUX_ONLY = {"uds", "lwip", "lwip-netconn"}   # Unix domain socket; lwIP's Unix port
TRANSPORT_SUCCESS = "Data Message:"



# Folders under example/sample-projects that group related samples: each sample
# in one is named "<group>/<sample>" (e.g. "databus/relay"), and that name is
# what the tables in these scripts use.
SAMPLE_GROUPS = {"os", "bare-metal", "boards", "transport", "databus",
                 "system-architecture", "integration"}


def sample_name(path):
    """The name the scripts use for the sample folder at path: "<group>/<sample>"
    inside a SAMPLE_GROUPS folder, else the folder name."""
    path = os.path.normpath(path)
    parent = os.path.dirname(path)
    if (os.path.basename(parent) in SAMPLE_GROUPS
            and os.path.basename(os.path.dirname(parent)) == "sample-projects"):
        return f"{os.path.basename(parent)}/{os.path.basename(path)}"
    return os.path.basename(path)


def list_samples(root):
    """(name, path) of each folder in root, sorted, with a SAMPLE_GROUPS folder
    replaced by the samples in it."""
    samples = []
    for entry in sorted(os.listdir(root)):
        path = os.path.join(root, entry)
        if not os.path.isdir(path):
            continue
        if entry in SAMPLE_GROUPS and os.path.basename(os.path.normpath(root)) == "sample-projects":
            samples += [(f"{entry}/{child}", os.path.join(path, child))
                        for child in sorted(os.listdir(path))
                        if os.path.isdir(os.path.join(path, child))]
        else:
            samples.append((entry, path))
    return samples


def transport_configs():
    """(name, transport, serializer) of the combinations this platform builds and
    runs; the build directory is build-<name>."""
    return [(f"{t}-{s}", t, s) for t, s in TRANSPORT_COMBOS
            if t not in (TRANSPORT_LINUX_ONLY if IS_WINDOWS else TRANSPORT_WINDOWS_ONLY)]


# Zephyr samples, built for native_sim (a host process) with the host gcc. A
# Zephyr app is a folder with prj.conf; it builds with plain CMake (-DBOARD)
# against ZEPHYR_BASE. The app is build/zephyr/zephyr.exe, which takes
# native_sim's own options, not the duration the other samples get.
# Value: text the run must print.
ZEPHYR_BOARD = "native_sim/native/64"
ZEPHYR_SAMPLES = {
    "os/zephyr":          "ALL TESTS PASSED",
    "transport/zephyr-udp": "Data Message:",
    "os/cmsis-rtos2":     "ALL TESTS PASSED",
}
# Zephyr samples also built for a Cortex-M board and run under QEMU, with the
# Zephyr SDK's Arm toolchain: os/cmsis-rtos2 verifies CmsisRtos2CriticalSection
# there (real CMSIS-Core, a real interrupt). Configured into its own build
# directory; the app ends QEMU through semihosting with status 0 only if every
# check passed.
ZEPHYR_QEMU_TARGETS = {
    "os/cmsis-rtos2": {
        "board": "mps2/an385",
        "build": "build-mps2-an385",
        "qemu":  ["qemu-system-arm", "-M", "mps2-an385", "-cpu", "cortex-m3", "-nographic",
                  "-monitor", "none", "-semihosting-config", "enable=on,target=native"],
    },
}
# databus/zephyr: Zephyr server (server/) and host client (client/), separate
# processes over the host loopback. The server runs until stopped, so it gets
# native_sim's --stop_at.
ZEPHYR_DATABUS_SAMPLE = "databus/zephyr"
ZEPHYR_DATABUS_REQUIRED = {"client": "[Client] sensor/temp", "server": "Received CmdMsg"}

# os/nuttx: NuttX's sim board (a host process). NuttX builds in its source
# tree, so the scripts copy NUTTX_SRC/nuttx to os/nuttx/build/nuttx, link the
# sample's apps/ beside it, and configure and build it there (see its README).
# The simulated OS keeps running after the tests, so the run is stopped once
# the success text appears.
NUTTX_SAMPLE = "os/nuttx"
NUTTX_SUCCESS = "ALL TESTS PASSED"
NUTTX_TIMEOUT = 120


def zephyr_missing():
    """What building the Zephyr samples needs that isn't here (empty if none)."""
    missing = []
    base = os.environ.get("ZEPHYR_BASE")
    if not base or not os.path.isfile(os.path.join(base, "VERSION")):
        missing.append("a Zephyr tree (set ZEPHYR_BASE)")
    if not shutil.which("dtc"):
        missing.append("dtc (device-tree-compiler)")
    return missing


def zephyr_qemu_missing():
    """What the Cortex-M Zephyr builds (ZEPHYR_QEMU_TARGETS) need beyond
    zephyr_missing(): the cmsis HAL module, the Zephyr SDK's Arm toolchain and
    QEMU (empty if none)."""
    missing = zephyr_missing()
    base = os.environ.get("ZEPHYR_BASE", "")
    if base and not os.path.isdir(os.path.join(base, "..", "modules", "hal", "cmsis")):
        missing.append("Zephyr's cmsis module (west update cmsis)")
    sdk = os.environ.get("ZEPHYR_SDK_INSTALL_DIR", "")
    if not os.path.isfile(os.path.join(sdk, "arm-zephyr-eabi", "bin", "arm-zephyr-eabi-gcc")):
        missing.append("the Zephyr SDK's arm-zephyr-eabi toolchain (set ZEPHYR_SDK_INSTALL_DIR)")
    if not shutil.which("qemu-system-arm"):
        missing.append("qemu-system-arm")
    return missing


def nuttx_missing():
    """What building os/nuttx needs that isn't here (empty if none)."""
    missing = []
    src = os.environ.get("NUTTX_SRC")
    if not src or not os.path.isfile(os.path.join(src, "nuttx", "tools", "configure.sh")):
        missing.append("NuttX and nuttx-apps checkouts (set NUTTX_SRC)")
    if not shutil.which("kconfig-tweak"):
        missing.append("kconfig-tweak (kconfig-frontends)")
    return missing


def serialport_lib_built(repo_root):
    """True if 02_build_libs.py built libserialport here (Linux: .libs/libserialport.a)."""
    deps_dir = os.environ.get("DMQ_DEPS_DIR") or os.path.join(repo_root, "external")
    return os.path.isfile(os.path.join(deps_dir, "libserialport", ".libs", "libserialport.a"))


def bare_metal_missing(target, sample=BARE_METAL_SAMPLE):
    """Tools a QEMU target of sample (QEMU_TARGET_SAMPLES) needs that aren't
    installed (empty if none)."""
    missing = []
    for tool in QEMU_TARGET_SAMPLES[sample][target]["tools"]:
        if isinstance(tool, tuple):
            var, default, rel = tool
            path = os.path.join(os.environ.get(var, default), rel) + (".exe" if IS_WINDOWS else "")
            if not os.path.isfile(path):
                missing.append(f"{path} (set {var})")
        elif not shutil.which(tool):
            missing.append(tool)
    return missing

# Other CPUs, cross-compiled and run under QEMU user mode (qemu-<cpu> -L
# /usr/<triple>, an ordinary host process whose system calls, sockets
# included, go to the host kernel).
S390X = {"processor": "s390x", "triple": "s390x-linux-gnu", "qemu": "qemu-s390x"}  # 64-bit big-endian
ARMHF = {"processor": "arm", "triple": "arm-linux-gnueabihf", "qemu": "qemu-arm"}  # 32-bit Arm


def cross_cmake_args(cross):
    """CMake arguments that cross-compile for a S390X/ARMHF-style target."""
    return ["-DCMAKE_SYSTEM_NAME=Linux", f"-DCMAKE_SYSTEM_PROCESSOR={cross['processor']}",
            f"-DCMAKE_C_COMPILER={cross['triple']}-gcc", f"-DCMAKE_CXX_COMPILER={cross['triple']}-g++"]


def cross_command(cross, exe):
    """Command line that runs a cross-compiled exe under QEMU user mode."""
    return [cross["qemu"], "-L", f"/usr/{cross['triple']}", exe]


# Builds of test/ beyond test/build (and test/build-clang with --clang).
#   suffix:   build directory under test/
#   cmake:    extra configure arguments
#   clang:    configure with clang++ (Linux), as the matching CI job does
#   env:      environment for the run
#   linux:    Linux only (sanitizers)
#   success:  text the run must print; None when the app prints no marker
#             (the stress tests just return 0)
TEST_VARIANTS = [
    {"suffix": "build-embedded", "clang": True, "linux": False,
     "cmake": ["-DDMQ_ALLOCATOR=ON", "-DDMQ_ASSERTS=ON"],
     "env": {}, "success": "Success!"},
    {"suffix": "build-stress", "clang": False, "linux": False,
     "cmake": ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_FLAGS=-DSTRESS_TESTS"],
     "env": {}, "success": None},
    {"suffix": "build-asan", "clang": True, "linux": True,
     "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DSANITIZE=address"],
     "env": {"ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1"}, "success": "Success!"},
    {"suffix": "build-tsan", "clang": True, "linux": True,
     "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DSANITIZE=thread"],
     "env": {"TSAN_OPTIONS": "halt_on_error=1"}, "success": "Success!"},
    {"suffix": "build-ubsan", "clang": True, "linux": True,
     "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DSANITIZE=undefined"],
     "env": {"UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}, "success": "Success!"},
    # Other CPUs (see S390X/ARMHF above): big-endian 64-bit (s390x) and 32-bit
    # Arm (armhf), for byte-order, pointer-size and alignment assumptions. Each
    # run takes about as long as a native one: the tests are paced by their
    # own sleeps.
    {"suffix": "build-s390x", "clang": False, "linux": True,
     "cmake": ["-DCMAKE_BUILD_TYPE=Release"], "env": {}, "success": "Success!", "cross": S390X},
    {"suffix": "build-armhf", "clang": False, "linux": True,
     "cmake": ["-DCMAKE_BUILD_TYPE=Release"], "env": {}, "success": "Success!", "cross": ARMHF},
]


def cross_missing(cross):
    """Cross compiler and QEMU a S390X/ARMHF-style target needs that aren't installed."""
    if not cross:
        return []
    return [t for t in (f"{cross['triple']}-g++", cross["qemu"]) if not shutil.which(t)]


# Cross-endian check: tests on one CPU can't catch a byte-order mismatch
# between peers. system-architecture/no-deps (built-in serializer, Linux UDP,
# no third-party libraries) is also built for s390x (big-endian) into
# client/ and server/build-s390x, and run with one side native x86
# (little-endian) and the other under qemu-s390x, both ways. Its IDs are
# small known numbers (1..4); a byte-swapped 1 would read as 16777216.
CROSS_ENDIAN_SAMPLE = "system-architecture/no-deps"
CROSS_ENDIAN_BUILD = "build-s390x"
CROSS_ENDIAN_CPU = S390X
CROSS_ENDIAN_REQUIRED = {"client": ["Actuator ID: 1,", "Sensor ID: 1,"],
                         "server": ["Actuator 1 position"]}
CROSS_ENDIAN_FORBIDDEN = "Unknown actuator ID"

# Seconds one unit-test run may take before it is killed and failed. The
# sanitizer builds (TSan especially) run several times slower than a plain one.
TEST_TIMEOUT = 1800


def test_variants():
    """The TEST_VARIANTS that apply on this platform."""
    return [v for v in TEST_VARIANTS if IS_LINUX or not v["linux"]]


# Samples --full doesn't cover yet: name -> what it needs.
PENDING = {
    "boards/stm32-freertos":         "hardware: needs an STM32 board",
}

# Folders with nothing to build or run at that step: name -> why.
NOT_APPLICABLE = {
    "system-architecture/python": "Python client of system-architecture/full; nothing to build",
    "bare-metal/keil":            "Keil uVision project; needs Keil MDK, not automated",
}

# Skip reasons that stay skips under --full: the other platform covers them.
PLATFORM_REASONS = ("Windows only", "Linux only", LINUX_RUN_COVERS)


def combined_report(results):
    """Print one table for several platforms' --full runs; return True if all passed.

    results: platform name -> the dict 05_run_samples.py --results wrote, or
    None when that platform produced no results file (its run stopped early).
    """
    platforms = list(results)
    status = {}      # name -> platform -> "passed" / "FAILED"
    reasons = {}     # name -> skip reason (first seen), for names run nowhere
    for plat, data in results.items():
        if not data:
            continue
        for name in data["passed"]:
            status.setdefault(name, {})[plat] = "passed"
        for name in data["failed"]:
            status.setdefault(name, {})[plat] = "FAILED"
        for name, reason in data["skipped"]:
            reasons.setdefault(name, reason)

    width = max([len(n) for n in status] + [30])
    print("\n" + "=" * 60)
    print(f"  FULL SUITE: {' + '.join(platforms)}")
    print("=" * 60)
    print(f"  {'':<{width}}  " + "  ".join(f"{p:<8}" for p in platforms))
    for name in sorted(status):
        cells = "  ".join(f"{status[name].get(p, '-'):<8}" for p in platforms)
        print(f"  {name:<{width}}  {cells}")

    not_run = sorted((n, r) for n, r in reasons.items() if n not in status)
    if not_run:
        print("\n  Not run on any platform:")
        for name, reason in not_run:
            print(f"    {name:<{width}}  {reason}")

    ok = True
    print()
    for plat, data in results.items():
        if data is None:
            print(f"  {plat:<8} NO RESULTS (the run stopped before step 05; see its log)")
            ok = False
            continue
        p, f = len(data["passed"]), len(data["failed"])
        print(f"  {plat:<8} {p}/{p + f} passed   {f} failed   {len(data['skipped'])} skipped")
        ok = ok and f == 0
    failures = [(n, p) for n in sorted(status) for p in platforms if status[n].get(p) == "FAILED"]
    if failures:
        print("\n  FAILED:")
        for name, plat in failures:
            print(f"    {name}  ({plat})")
    print("=" * 60)
    return ok


def strict_split(skipped):
    """Split (name, reason) skips into (kept, failures) under --full.

    Kept: platform skips and PENDING samples (with the PENDING reason).
    Failures: everything else, e.g. a sample that wasn't built.
    """
    def listed(name, table):
        # The table entry for name: the sample itself, or a part of it
        # ("<sample>/<node>", "<sample> (clang)"). Sample names can contain a
        # slash themselves ("bare-metal/keil"), so match whole entries.
        word = name.split(" ")[0]
        return next((key for key in table if word == key or word.startswith(key + "/")), None)

    kept, failures = [], []
    for name, reason in skipped:
        pending, not_applicable = listed(name, PENDING), listed(name, NOT_APPLICABLE)
        if reason in PLATFORM_REASONS:
            kept.append((name, reason))
        elif pending:
            kept.append((name, PENDING[pending]))
        elif not_applicable:
            kept.append((name, NOT_APPLICABLE[not_applicable]))
        else:
            failures.append((name, reason))
    return kept, failures
