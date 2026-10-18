"""
Script: run_all.py
Description:
    Runs the example-ecosystem scripts in scripts/ in order:
        01_fetch_repos.py       Clone third-party dependencies into external/
        02_build_libs.py        Build them as static libraries
        03_generate_samples.py  Configure every sample project
        04_build_samples.py     Build every sample project
        05_run_samples.py       Run every sample and report pass/fail

    Stops if 01 or 02 fails, since every later step depends on them. Steps
    03-05 report per-sample results, so a failure there doesn't stop the
    remaining steps; the exit code is non-zero if any step failed.

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

Usage:
    > python run_all.py
    > python run_all.py --clang --clean
    > python run_all.py --from 3                       # resume at 03
    > python run_all.py --deps-dir /path/to/shared/external
"""

import argparse
import os
import subprocess
import sys

SCRIPTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scripts")

# (step, script, stop the sequence if it fails)
STEPS = [
    (1, "01_fetch_repos.py",      True),
    (2, "02_build_libs.py",       True),
    (3, "03_generate_samples.py", False),
    (4, "04_build_samples.py",    False),
    (5, "05_run_samples.py",      False),
]

def step_args(step, args):
    """Pass each script only the options it accepts."""
    extra = []
    if step in (1, 2) and args.deps_dir:
        extra += ["--deps-dir", args.deps_dir]
    if step in (3, 4, 5) and args.clang:
        extra.append("--clang")
    if step == 3 and args.clean:
        extra.append("--clean")
    if step == 5:
        extra.append("--yes")
    return extra

def main():
    parser = argparse.ArgumentParser(description="Run the DelegateMQ example-ecosystem scripts 01-05 in order.")
    parser.add_argument("--from", dest="start", type=int, default=1, choices=range(1, 6),
                        metavar="N", help="first step to run (1-5, default 1)")
    parser.add_argument("--to", dest="end", type=int, default=5, choices=range(1, 6),
                        metavar="N", help="last step to run (1-5, default 5)")
    parser.add_argument("--clang", action="store_true", help="also configure/build/run build-clang/ variants (Linux only)")
    parser.add_argument("--clean", action="store_true", help="remove existing sample build directories before configuring")
    parser.add_argument("--deps-dir", help="third-party directory (default: DMQ_DEPS_DIR env var, else <repo>/external)")
    args = parser.parse_args()

    results = []
    for step, script, fatal in STEPS:
        if not args.start <= step <= args.end:
            continue
        cmd = [sys.executable, os.path.join(SCRIPTS_DIR, script)] + step_args(step, args)
        print(f"\n{'#' * 60}\n#  {script} {' '.join(cmd[2:])}\n{'#' * 60}\n", flush=True)
        ok = subprocess.run(cmd).returncode == 0
        results.append((script, ok))
        if not ok and fatal:
            print(f"\n{script} failed; stopping (later steps depend on it).")
            break

    print(f"\n{'=' * 60}")
    for script, ok in results:
        print(f"  {'ok    ' if ok else 'FAILED'}  {script}")
    print("=" * 60)
    return all(ok for _, ok in results)

if __name__ == "__main__":
    sys.exit(0 if main() else 1)
