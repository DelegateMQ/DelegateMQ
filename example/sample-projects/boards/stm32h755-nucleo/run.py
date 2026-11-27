#!/usr/bin/env python3
"""Flash both cores of the NUCLEO-H755ZI-Q and check the run.

    python run.py                 flash build/h755_cm7.elf and build/h755_cm4.elf,
                                  reset, print the console until the suite ends
    python run.py --no-flash      reset and read only
    python run.py --port COM7     console port (default: the ST-LINK's VCP)

Exit status: the suite's (0 = all tests passed), 1 for no result in time.

Needs STM32_Programmer_CLI (STM32CubeProgrammer, STM32CubeCLT or STM32CubeIDE:
found on the PATH, in their default install folders, or through
STM32_PROGRAMMER_CLI) and pyserial (pip install pyserial; PlatformIO's Python
has it).
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ST_VID = 0x0483
BANNER = "NUCLEO-H755ZI-Q:"    # start of BspName(), printed first at startup


def find_programmer():
    env = os.environ.get("STM32_PROGRAMMER_CLI")
    if env and os.path.isfile(env):
        return env
    exe = "STM32_Programmer_CLI.exe" if os.name == "nt" else "STM32_Programmer_CLI"
    on_path = shutil.which(exe)
    if on_path:
        return on_path
    patterns = [
        r"C:\ST\STM32CubeCLT*\STM32CubeProgrammer\bin",
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin",
        r"C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins\*cubeprogrammer.win32*\tools\bin",
        "/opt/st/stm32cubeclt*/STM32CubeProgrammer/bin",
        os.path.expanduser("~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"),
        "/opt/st/stm32cubeide*/plugins/*cubeprogrammer.linux*/tools/bin",
    ]
    for pattern in patterns:
        for d in sorted(glob.glob(pattern), reverse=True):
            candidate = os.path.join(d, exe)
            if os.path.isfile(candidate):
                return candidate
    sys.exit("STM32_Programmer_CLI not found: install STM32CubeProgrammer or "
             "STM32CubeCLT, or set STM32_PROGRAMMER_CLI")


def programmer(cli, *args):
    cmd = [cli, "-c", "port=SWD", "mode=UR", *args]
    print("+", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-3000:])
        print(result.stderr[-3000:])
        sys.exit(f"STM32_Programmer_CLI failed ({result.returncode})")
    return result.stdout


def check_boot_option_bytes(cli):
    """Both cores must boot (BCM7 = BCM4 = 1, as shipped): the Cortex-M7 waits
    for the Cortex-M4 to stop, then releases it."""
    out = programmer(cli, "-ob", "displ")
    for name in ("BCM7", "BCM4"):
        m = re.search(rf"\b{name}\s*:\s*0x([0-9A-Fa-f]+)", out)
        if m and int(m.group(1), 16) != 1:
            sys.exit(f"Option byte {name} is 0: this sample needs both cores to boot. "
                     f"Set it with: STM32_Programmer_CLI -c port=SWD -ob {name}=1")


def find_vcp():
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == ST_VID]
    if not ports:
        sys.exit("No ST-LINK virtual COM port found; pass --port")
    return ports[0].device


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(HERE, "build"))
    ap.add_argument("--port", help="console port (default: the ST-LINK VCP)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=180.0, help="seconds to wait for the result")
    ap.add_argument("--no-flash", action="store_true", help="reset and read only")
    args = ap.parse_args()

    import serial   # pyserial

    cli = find_programmer()
    port = args.port or find_vcp()
    console = serial.Serial(port, args.baud, timeout=0.2)
    print(f"Console on {port} at {args.baud}")

    check_boot_option_bytes(cli)
    # The old firmware runs again after each programmer session: drop its
    # output just before the reset, and (below) trust only lines that come
    # after the new run's startup banner
    if args.no_flash:
        console.reset_input_buffer()
        programmer(cli, "-hardRst")
    else:
        images = [os.path.join(args.build_dir, f"h755_{core}.elf") for core in ("cm7", "cm4")]
        for image in images:
            if not os.path.isfile(image):
                sys.exit(f"{image} not found: build first (cmake --build build)")
        # -v verifies the -w before it
        writes = [arg for image in images for arg in ("-w", image, "-v")]
        console.reset_input_buffer()
        programmer(cli, *writes, "-hardRst")

    status = None
    started = False     # seen this run's banner (BspName())
    line = b""
    end = time.time() + args.timeout
    while time.time() < end and status is None:
        for ch in console.read(256):
            ch = bytes([ch])
            if ch == b"\n":
                text = line.decode("ascii", "replace").rstrip("\r")
                print(text, flush=True)
                if text.startswith(BANNER):
                    started = True
                m = re.match(r"BSP EXIT (-?\d+)", text)
                if m and started:
                    status = int(m.group(1))
                line = b""
            else:
                line += ch
    console.close()

    if status is None:
        print(f"\nNo result within {args.timeout:.0f} s")
        return 1
    print("\nPASS" if status == 0 else f"\nFAIL (status {status})")
    return status


if __name__ == "__main__":
    sys.exit(main())
