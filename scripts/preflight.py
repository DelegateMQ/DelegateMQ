"""
Module: preflight.py
Description:
    Prerequisite check shared by build_all.py and the numbered scripts:
    git and CMake must be on PATH, and CMake new enough for the samples and,
    on Windows, for an installed Visual Studio. Prints what is missing and how
    to fix it, instead of a Python traceback or a CMake error deep in a
    third-party library's output.
"""

import os
import platform
import re
import shutil
import subprocess

# Highest cmake_minimum_required among the projects the scripts configure
# (Pumptron's STM32F4 firmware). The library and most samples need only 3.16.
MIN_CMAKE = (3, 20)

# Visual Studio major version -> (name, first CMake with its generator).
VS_CMAKE = {
    16: ("Visual Studio 2019", (3, 14)),
    17: ("Visual Studio 2022", (3, 21)),
    18: ("Visual Studio 2026", (4, 2)),
}

def _ver(t):
    return ".".join(str(x) for x in t)

def _cmake_version(cmake):
    try:
        out = subprocess.run([cmake, "--version"], capture_output=True, text=True).stdout
    except OSError:
        return None
    m = re.search(r"cmake version (\d+)\.(\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m else None

def _vs_majors():
    """Major versions of installed Visual Studio instances with the C++ tools."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return []
    out = subprocess.run([vswhere, "-all", "-products", "*",
                          "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                          "-property", "installationVersion"],
                         capture_output=True, text=True).stdout
    return sorted({int(v.split(".")[0]) for v in out.split() if v[:1].isdigit()})

def check(need_git=True, need_cmake=True):
    """Print problems and return False if a required tool is missing or too old."""
    if os.environ.get("DMQ_PREFLIGHT_DONE"):
        return True   # build_all.py already checked, for every step it runs
    errors = []

    if need_git and not shutil.which("git"):
        errors.append("git not found on PATH. Install it from https://git-scm.com/downloads")

    if need_cmake:
        cmake = shutil.which("cmake")
        if not cmake:
            errors.append(f"CMake not found on PATH. Install CMake {_ver(MIN_CMAKE)} or newer "
                          "from https://cmake.org/download/")
        else:
            have = _cmake_version(cmake)
            if have is None:
                errors.append("Could not read the CMake version from 'cmake --version'.")
            elif have < MIN_CMAKE:
                errors.append(f"CMake {_ver(have)} is too old; {_ver(MIN_CMAKE)} or newer is "
                              "required. Update it from https://cmake.org/download/")
            elif platform.system() == "Windows":
                majors = _vs_majors()
                known = [m for m in majors if m in VS_CMAKE]
                if not majors:
                    print("WARNING: no Visual Studio with the C++ tools found. CMake's default "
                          "Windows generator needs one (workload: Desktop development with C++).")
                elif known and not any(have >= VS_CMAKE[m][1] for m in known):
                    # CMake uses the newest Visual Studio it has a generator for, so
                    # one supported installation is enough.
                    newest = max(known)
                    name, need = VS_CMAKE[newest]
                    errors.append(f"CMake {_ver(have)} has no generator for {name}; it needs "
                                  f"CMake {_ver(need)} or newer. Update it from https://cmake.org/download/")

    for e in errors:
        print(f"ERROR: {e}")
    if errors:
        print()
    return not errors
