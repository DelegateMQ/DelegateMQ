"""
Script: build_all.py
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

    --full runs the full test suite (see scripts/full_suite.py): every
    sample this platform can run, plus the unit-test app in all its builds
    (sanitizers, DMQ_ALLOCATOR/DMQ_ASSERTS, stress). A missing tool or an
    unbuilt sample is a failure instead of a skip. On Linux it implies
    --clang. Results are also written to full-suite-results/<platform>.json.
    docker/run.sh runs it in a container with every Linux tool installed.

    --linux-host user@host runs both halves of the full suite from one
    command (implies --full): this working tree is copied to the Linux
    machine (scripts/remote.py), docker/run.sh --full runs there while this
    machine runs its own --full, and one combined table reports both. The
    Linux output goes to full-suite-results/linux.log; its step banners and
    failures are echoed here with a [linux] prefix. Needs key-based ssh
    login and Docker there (see "Full Test Suite" in docs/BUILD.md).

    See "Example Ecosystem (Sandbox)" in docs/BUILD.md before running script.

Usage:
    > python build_all.py
    > python build_all.py --clang --clean
    > python build_all.py --from 3                       # resume at 03
    > python build_all.py --deps-dir /path/to/shared/external
    > python build_all.py --full
    > python build_all.py --linux-host user@linux-box
"""

import argparse
import json
import os
import platform
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
SCRIPTS_DIR = os.path.join(REPO_ROOT, "scripts")
RESULTS_DIR = os.path.join(REPO_ROOT, "full-suite-results")

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
    if step in (3, 4, 5) and args.full:
        extra.append("--full")
    if step == 3 and args.clean:
        extra.append("--clean")
    if step == 5:
        extra.append("--yes")
        if args.full:
            name = platform.system().lower() + ".json"
            extra += ["--results", os.path.join(RESULTS_DIR, name)]
    return extra

def remote_args(args):
    """build_all.py options passed on to the --linux-host run."""
    extra = ["--from", str(args.start), "--to", str(args.end)]
    if args.clean:
        extra.append("--clean")
    return extra

def load_results(name):
    """A platform's --full results (full-suite-results/<name>.json), or None if missing."""
    path = os.path.join(RESULTS_DIR, name + ".json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)

def run_steps(args):
    """Run steps --from..--to on this machine; return [(script, ok)]."""
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
    return results

def main():
    parser = argparse.ArgumentParser(description="Run the DelegateMQ example-ecosystem scripts 01-05 in order.")
    parser.add_argument("--from", dest="start", type=int, default=1, choices=range(1, 6),
                        metavar="N", help="first step to run (1-5, default 1)")
    parser.add_argument("--to", dest="end", type=int, default=5, choices=range(1, 6),
                        metavar="N", help="last step to run (1-5, default 5)")
    parser.add_argument("--clang", action="store_true", help="also configure/build/run build-clang/ variants (Linux only)")
    parser.add_argument("--clean", action="store_true", help="remove existing sample build directories before configuring")
    parser.add_argument("--deps-dir", help="third-party directory (default: DMQ_DEPS_DIR env var, else <repo>/external)")
    parser.add_argument("--full", action="store_true",
                        help="full test suite: unit-test builds too, and a missing tool or unbuilt sample "
                             "fails instead of being skipped (implies --clang on Linux)")
    parser.add_argument("--linux-host", metavar="USER@HOST",
                        help="also run the Linux half of the full suite on this machine, in Docker, "
                             "on a copy of this working tree (implies --full)")
    parser.add_argument("--linux-dir", default="dmq-full/DelegateMQ", metavar="DIR",
                        help="checkout used on the --linux-host machine, relative to its home "
                             "(default dmq-full/DelegateMQ; cloned on first use)")
    args = parser.parse_args()
    if args.linux_host:
        if platform.system() == "Linux":
            parser.error("--linux-host runs the Linux half elsewhere; on Linux run docker/run.sh --full instead")
        args.full = True
    if args.full and platform.system() == "Linux":
        args.clang = True

    # Check for git/CMake first: without them a step fails with a Python
    # traceback or a CMake error buried in a third-party library's output.
    sys.path.insert(0, SCRIPTS_DIR)
    import preflight
    ok = preflight.check(need_git=args.start <= 1, need_cmake=args.start <= 4, full=args.full)
    if args.linux_host:
        import remote
        problems = remote.check(args.linux_host, args.linux_dir)
        for p in problems:
            print(f"ERROR: {p}")
        ok = ok and not problems
    if not ok:
        print("Prerequisites missing; nothing was run.")
        return False
    os.environ["DMQ_PREFLIGHT_DONE"] = "1"   # the step scripts skip their own check

    # Results from an earlier run must not stand in for this run's
    if args.full and os.path.isdir(RESULTS_DIR):
        for name in os.listdir(RESULTS_DIR):
            if name.endswith(".json"):
                os.remove(os.path.join(RESULTS_DIR, name))

    linux = None
    if args.linux_host:
        print(f"Copying this working tree to {args.linux_host}:{args.linux_dir} ...", flush=True)
        try:
            commit = remote.sync(REPO_ROOT, args.linux_host, args.linux_dir)
        except RuntimeError as e:
            print(f"ERROR: {e}")
            return False
        print(f"   snapshot {commit[:10]} checked out there; Linux run started", flush=True)
        linux = remote.RemoteRun(args.linux_host, args.linux_dir, remote_args(args),
                                 os.path.join(RESULTS_DIR, "linux.log"))
        linux.start()

    try:
        results = run_steps(args)
        if linux:
            if linux.thread.is_alive():
                print(f"\nWaiting for the Linux run on {args.linux_host} ...", flush=True)
            linux.join()
    except KeyboardInterrupt:
        if linux:
            print("\nInterrupted: stopping the Linux container ...")
            linux.stop()
        raise

    print(f"\n{'=' * 60}")
    for script, ok in results:
        print(f"  {'ok    ' if ok else 'FAILED'}  {script}")
    if linux:
        print(f"  {'ok    ' if linux.ok else 'FAILED'}  Linux run on {args.linux_host} "
              f"(log: full-suite-results/linux.log)")
    print("=" * 60)
    all_ok = all(ok for _, ok in results)

    if linux:
        import full_suite
        remote.fetch_results(args.linux_host, args.linux_dir, os.path.join(RESULTS_DIR, "linux.json"))
        here = platform.system()
        combined = {here: load_results(here.lower()), "Linux": load_results("linux")}
        all_ok = full_suite.combined_report(combined) and all_ok and linux.ok
    return all_ok

if __name__ == "__main__":
    sys.exit(0 if main() else 1)
