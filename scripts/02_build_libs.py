"""
Script: 02_build_libs.py
Description:
    Automated build orchestrator for DelegateMQ dependencies.
    This script compiles the raw source code cloned by '01_fetch_repos.py' into 
    static libraries usable by the sample projects.

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

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
    - libserialport:    No CMake build. Windows: Debug x64 DLL, built with MSBuild
                        from its own Visual Studio project. Linux: static library
                        from its autotools build (needs autoconf, automake,
                        libtool). Used by serialport-serializer and
                        stm32-freertos/client. A failure here is a warning only:
                        other samples don't need it.

Usage:
    Run this script SECOND to compile the static libraries.
    > python scripts/02_build_libs.py
    > python scripts/02_build_libs.py --deps-dir /path/to/shared/external

    Builds in <repo>/external by default (--deps-dir or the DMQ_DEPS_DIR
    environment variable override it; see 01_fetch_repos.py).
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys

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
        # ADDED: -DPAHO_ENABLE_TESTING=FALSE -- Paho's test suite is POSIX-only
        # (test4.c, test6.c, etc. include <sys/time.h>, which doesn't exist on
        # MSVC/Windows) and isn't needed to produce the libs DelegateMQ links.
        "flags": [
            "-DPAHO_BUILD_SHARED=TRUE",
            "-DPAHO_BUILD_STATIC=TRUE",
            "-DPAHO_BUILD_SAMPLES=FALSE",
            "-DPAHO_ENABLE_TESTING=FALSE",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
        ]
    }
]

def fix_zeromq_conflict(deps_dir):
    """
    AUTOMATIC FIX: 
    Renames 'zeromq/INSTALL' (file) to 'zeromq/INSTALL.txt' 
    so CMake can create the 'zeromq/INSTALL' (directory).
    """
    zmq_path = os.path.join(deps_dir, "zeromq")
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

# Visual Studio major version -> MSVC platform toolset. libserialport.vcxproj
# asks for v142 (VS 2019); newer Visual Studio installs only have their own.
VS_TOOLSETS = {16: "v142", 17: "v143", 18: "v145"}

def find_msbuild():
    """Return (MSBuild.exe path, toolset or None) for the newest Visual Studio, or (None, None)."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None, None
    def query(*args):
        out = subprocess.run([vswhere, "-latest", "-products", "*",
                              "-requires", "Microsoft.Component.MSBuild", *args],
                             capture_output=True, text=True).stdout.strip()
        return out.splitlines()[0] if out else ""
    msbuild = query("-find", r"MSBuild\**\Bin\MSBuild.exe")
    version = query("-property", "installationVersion")
    toolset = VS_TOOLSETS.get(int(version.split(".")[0])) if version else None
    return (msbuild or None), toolset

def build_libserialport_windows(deps_dir):
    """Build libserialport's Debug x64 DLL, where External.cmake looks for it.
    Returns True on success. Failure only affects the serial-port samples."""
    source_dir = os.path.join(deps_dir, "libserialport")
    project = os.path.join(source_dir, "libserialport.vcxproj")
    if not os.path.isfile(project):
        print(f"[WARNING] Missing {project}; skipping libserialport.")
        return False

    print("--- Building libserialport (MSBuild, Debug x64) ---")
    msbuild, toolset = find_msbuild()
    if not msbuild:
        print("    [WARNING] MSBuild not found (needs Visual Studio with C++ tools).\n")
        return False

    cmd = [msbuild, project, "-p:Configuration=Debug", "-p:Platform=x64",
           "-m", "-nologo", "-v:minimal"]
    if toolset:
        cmd.append(f"-p:PlatformToolset={toolset}")
    try:
        subprocess.run(cmd, cwd=source_dir, check=True)
        print("    Success!\n")
        return True
    except subprocess.CalledProcessError:
        print("    [WARNING] libserialport failed to build; serialport-serializer "
              "and stm32-freertos/client won't configure.\n")
        return False

# Patch for libserialport 0.1.2 (the latest release; upstream master has no
# later changes), applied to the fetched source before the Linux build.
# sp_open() reads the modem-control lines (DTR/RTS/CTS...) with TIOCMGET and
# sets RTS and DTR with TIOCMBIS/TIOCMBIC, and fails if those ioctls do.
# Pseudo-terminals have no modem lines, so they fail with ENOTTY there, and
# libserialport can't open the socat pty pair 05_run_samples.py connects
# serialport-serializer's sender and receiver with. The patch treats
# ENOTTY/EINVAL from them as "no modem lines". Real UARTs support these
# ioctls, so they behave as before.
# Entries: (file, old text, new text, number of places old text must occur).
LIBSERIALPORT_PATCH_MARK = "DelegateMQ patch"
_NO_MODEM_LINES = "errno != ENOTTY && errno != EINVAL"
LIBSERIALPORT_PATCHES = [
    ("serialport.c",
     "\tif (ioctl(port->fd, TIOCMGET, &data->controlbits) < 0)\n"
     "\t\tRETURN_FAIL(\"TIOCMGET ioctl failed\");\n",
     "\t/* DelegateMQ patch: no modem lines (e.g. a pseudo-terminal) is not an error */\n"
     "\tif (ioctl(port->fd, TIOCMGET, &data->controlbits) < 0) {\n"
     f"\t\tif ({_NO_MODEM_LINES})\n"
     "\t\t\tRETURN_FAIL(\"TIOCMGET ioctl failed\");\n"
     "\t\tdata->controlbits = 0;\n"
     "\t}\n", 1),
    # Each is the body of an `if (ioctl(... TIOCMBIS : TIOCMBIC ...) < 0)`
    ("serialport.c",
     "RETURN_FAIL(\"Setting RTS signal level failed\");",
     f"{{ /* DelegateMQ patch */ if ({_NO_MODEM_LINES}) RETURN_FAIL(\"Setting RTS signal level failed\"); }}", 2),
    ("serialport.c",
     "RETURN_FAIL(\"Setting DTR signal level failed\");",
     f"{{ /* DelegateMQ patch */ if ({_NO_MODEM_LINES}) RETURN_FAIL(\"Setting DTR signal level failed\"); }}", 2),
]

def patch_libserialport(source_dir):
    """Apply LIBSERIALPORT_PATCHES to the fetched source, once. Returns True if
    the source is patched (now or already)."""
    for name, old, new, count in LIBSERIALPORT_PATCHES:
        path = os.path.join(source_dir, name)
        with open(path, newline="") as f:
            text = f.read()
        if "\r\n" in text:   # a checkout with CRLF line endings
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if text.count(new) == count:
            continue   # already applied
        if text.count(old) != count:
            print(f"    [WARNING] {name}: code to patch not found {count}x (libserialport version changed?)")
            return False
        with open(path, "w", newline="") as f:
            f.write(text.replace(old, new))
    return True

def build_libserialport_linux(deps_dir):
    """Build libserialport as a static library with its autotools build, into
    .libs/ where External.cmake looks for it (static, so the serial samples
    need no runtime library path). Needs autoconf, automake and libtool.
    Returns True on success. Failure only affects the serial-port samples."""
    source_dir = os.path.join(deps_dir, "libserialport")
    if not os.path.isfile(os.path.join(source_dir, "autogen.sh")):
        print(f"[WARNING] Missing {source_dir}/autogen.sh; skipping libserialport.")
        return False
    # NO_PORT_METADATA: skip libserialport's /sys/class/tty lookup of USB IDs
    # and descriptions, which DelegateMQ's SerialTransport doesn't use. With
    # it, sp_get_port_by_name() rejects ports not listed there, such as the
    # pseudo-terminal pair 05_run_samples.py makes with socat. libserialport's
    # own configure.ac sets it on systems without port metadata.
    configure = ["--disable-shared", "--enable-static", "CFLAGS=-O2 -DNO_PORT_METADATA"]
    build_id = " ".join(configure) + f" +{len(LIBSERIALPORT_PATCHES)} patch(es)\n"

    # The stamp records how .libs/ was built; a build with other settings is redone
    stamp = os.path.join(source_dir, ".libs", "dmq-build-flags")
    try:
        with open(stamp) as f:
            built_as = f.read()
    except OSError:
        built_as = None
    if os.path.isfile(os.path.join(source_dir, ".libs", "libserialport.a")) and built_as == build_id:
        print("--- libserialport already built ---\n")
        return True

    print("--- Building libserialport (autotools, static, patched for ptys) ---")
    missing = [t for t in ("autoreconf", "automake", "libtoolize") if not shutil.which(t)]
    if missing:
        print(f"    [WARNING] {', '.join(missing)} not found (packages autoconf, automake, "
              "libtool); serialport-serializer won't build.\n")
        return False
    if not patch_libserialport(source_dir):
        print("    [WARNING] libserialport not patched; serialport-serializer won't run on a pty pair.\n")
        return False
    steps = [["sh", "autogen.sh"], ["sh", "configure"] + configure]
    if os.path.isfile(os.path.join(source_dir, "Makefile")):
        steps.append(["make", "clean"])   # objects from a build with other flags
    steps.append(["make", f"-j{os.cpu_count() or 1}"])
    try:
        for cmd in steps:
            subprocess.run(cmd, cwd=source_dir, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        with open(stamp, "w") as f:
            f.write(build_id)
        print("    Success!\n")
        return True
    except subprocess.CalledProcessError as e:
        print(f"    [WARNING] libserialport failed to build ({' '.join(e.cmd)}); "
              "serialport-serializer won't build.")
        print("    " + "\n    ".join((e.stderr or "").strip().splitlines()[-10:]) + "\n")
        return False

def resolve_deps_dir():
    """Where 01_fetch_repos.py put the repos: --deps-dir, else the DMQ_DEPS_DIR
    env var, else <repo>/external. Same rule as src/delegate-mq/DepsDir.cmake."""
    parser = argparse.ArgumentParser(description="Build DelegateMQ third-party libraries.")
    parser.add_argument("--deps-dir", help="third-party directory (default: DMQ_DEPS_DIR env var, else <repo>/external)")
    args = parser.parse_args()
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    deps_dir = args.deps_dir or os.environ.get("DMQ_DEPS_DIR") or os.path.join(repo_root, "external")
    return os.path.abspath(deps_dir)

def build_deps():
    deps_dir = resolve_deps_dir()
    print(f"Third-party Directory: {deps_dir}\n")

    # 1. Run the Auto-Fix before building
    fix_zeromq_conflict(deps_dir)

    failed = []

    for lib in libs:
        source_dir = os.path.join(deps_dir, lib["path"])
        install_dir = os.path.join(source_dir, "install")
        
        if not os.path.isdir(source_dir):
            print(f"[ERROR] Missing {source_dir}")
            failed.append(lib["name"]); continue

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

        # Windows: --parallel below only controls how many *projects* in the
        # solution build concurrently (/maxcpucount) -- it does NOT parallelize
        # compilation of multiple source files within a single project. None of
        # these third-party libraries set /MP (MSVC's own multi-file parallel
        # compile flag) themselves, so without this every .c/.cpp in each
        # library compiles one at a time regardless of --parallel. This is why
        # the build is fast on Linux (make -j parallelizes at the file level by
        # default) but slow on Windows. Not needed on Linux/macOS: GCC/Clang
        # have no equivalent flag, and make/ninja already parallelize file
        # compilation via --parallel.
        if os.name == "nt":
            cmd_config += [
                "-DCMAKE_C_FLAGS=/MP",
                "-DCMAKE_CXX_FLAGS=/MP",
            ]

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
            failed.append(lib["name"])

    # Not added to `failed`: only the serial-port samples need it.
    if os.name == "nt":
        build_libserialport_windows(deps_dir)
    elif platform.system() == "Linux":
        build_libserialport_linux(deps_dir)

    if failed:
        print(f"FAILED: {', '.join(failed)}")
    return not failed

if __name__ == "__main__":
    import preflight
    if not preflight.check(need_git=False, need_cmake=True):
        sys.exit(1)
    sys.exit(0 if build_deps() else 1)