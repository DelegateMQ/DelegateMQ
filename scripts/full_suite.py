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

import platform

IS_WINDOWS = platform.system() == "Windows"
IS_LINUX = platform.system() == "Linux"

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
]

# Seconds one unit-test run may take before it is killed and failed. The
# sanitizer builds (TSan especially) run several times slower than a plain one.
TEST_TIMEOUT = 1800


def test_variants():
    """The TEST_VARIANTS that apply on this platform."""
    return [v for v in TEST_VARIANTS if IS_LINUX or not v["linux"]]


# Samples --full doesn't cover yet: name -> what it needs.
PENDING = {
    "atfe-armv7m-bare-metal": "not yet in --full: needs ATfE + QEMU",
    "bare-metal-arm":         "not yet in --full: needs QEMU",
    "bare-metal-riscv":       "not yet in --full: needs xPack RISC-V GCC + QEMU",
    "mqtt-rapidjson":         "not yet in --full: needs an MQTT broker",
    "serialport-serializer":  "not yet in --full: needs libserialport + a virtual serial pair",
    "qt-linux":               "not yet in --full: needs Qt 6",
    "zephyr-linux":           "not yet in --full: needs Zephyr (west)",
    "zephyr-udp-serializer":  "not yet in --full: needs Zephyr (west)",
    "databus-zephyr":         "not yet in --full: needs Zephyr (west)",
    "cmsis-rtos2-linux":      "not yet in --full: needs Zephyr (west)",
    "nuttx-sim":              "not yet in --full: needs NuttX",
    "stm32-freertos":         "hardware: needs an STM32 board",
}

# Folders with nothing to build or run at that step: name -> why.
NOT_APPLICABLE = {
    "system-architecture-python": "Python client of system-architecture; nothing to build",
    "keil-bare-metal":            "Keil uVision project; needs Keil MDK, not automated",
}

# Skip reasons that stay skips under --full: the other platform covers them.
PLATFORM_REASONS = ("Windows only", "Linux only")


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
    kept, failures = [], []
    for name, reason in skipped:
        base = name.split(" ")[0].split("/")[0]
        if reason in PLATFORM_REASONS:
            kept.append((name, reason))
        elif base in PENDING:
            kept.append((name, PENDING[base]))
        elif base in NOT_APPLICABLE:
            kept.append((name, NOT_APPLICABLE[base]))
        else:
            failures.append((name, reason))
    return kept, failures
