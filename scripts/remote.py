"""
Module: remote.py
Description:
    The Linux half of `build_all.py --full --linux-host <user@host>`: copies
    this working tree to a Linux machine, runs docker/run.sh --full there, and
    fetches its results file. Needs git and an ssh client here, and git plus
    Docker (user in the `docker` group) on the Linux machine, with key-based
    ssh login.

    Copying the working tree: a temporary commit of it (tracked changes,
    deletions and new files that aren't gitignored) is built through a
    separate, temporary git index, so the real index and files are untouched,
    and pushed to refs/dmq-sync/head in a clone on the Linux machine, which
    then checks it out. Only changed objects travel, unpushed commits work,
    and git's line-ending rules give the Linux side LF files. Build and
    third-party directories there are gitignored, so they survive between
    runs and builds stay incremental.
"""

import os
import shlex
import shutil
import subprocess
import tempfile
import threading

SYNC_REF = "refs/dmq-sync/head"
CONTAINER = "delegatemq-full-remote"

# Lines of the Linux log echoed here while it runs (all of it goes to the log
# file): step banners, failures and the result lines.
ECHO_PREFIXES = ("#  0", "RESULTS", "   FAILED", "CONFIGURE FAILED", "  ok ", "  FAILED ")


def _git(repo, *args, env=None, input_bytes=None):
    """Run git in repo and return stdout (stripped); raise on failure."""
    r = subprocess.run(["git", "-C", repo, *args], env=env, input=input_bytes,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout.decode(errors="replace").strip()


def _ssh(host, command, **kw):
    """Run a command on host through ssh (key login only, no prompts)."""
    return subprocess.run(["ssh", "-o", "BatchMode=yes", host, command], **kw)


def check(host, remote_dir):
    """Return a list of problems that would stop a remote run (empty if none)."""
    errors = []
    if not shutil.which("ssh"):
        errors.append("--linux-host: no ssh client on PATH")
        return errors
    probe = ("command -v git >/dev/null || echo 'git not installed'; "
             "command -v docker >/dev/null || echo 'docker not installed'; "
             "docker info >/dev/null 2>&1 || echo 'docker not usable by this user (in the docker group?)'")
    r = _ssh(host, probe, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode == 255:
        errors.append(f"--linux-host: cannot ssh to {host} with a key: {r.stderr.strip()}")
    else:
        errors += [f"--linux-host {host}: {line}" for line in r.stdout.splitlines() if line.strip()]
    return errors


def snapshot_commit(repo):
    """Commit the working tree as it is, through a temporary index; return the commit id."""
    head = _git(repo, "rev-parse", "HEAD")
    index = os.path.join(_git(repo, "rev-parse", "--absolute-git-dir"), "index")
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=os.path.join(tmp, "index"))
        if os.path.isfile(index):
            shutil.copy2(index, env["GIT_INDEX_FILE"])   # saves re-hashing unchanged files
        _git(repo, "add", "-A", env=env)
        tree = _git(repo, "write-tree", env=env)
    return _git(repo, "commit-tree", tree, "-p", head, "-m", "build_all.py --linux-host snapshot",
                input_bytes=b"")


def sync(repo, host, remote_dir):
    """Make remote_dir on host a checkout of this working tree; return the snapshot commit id."""
    origin = _git(repo, "remote", "get-url", "origin")
    q = shlex.quote(remote_dir)
    r = _ssh(host, f"test -d {q}/.git || git clone -q {shlex.quote(origin)} {q}",
             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"cloning on {host} failed: {r.stderr.strip()}")

    commit = snapshot_commit(repo)
    _git(repo, "push", "-q", "--force", f"{host}:{remote_dir}", f"{commit}:{SYNC_REF}")
    r = _ssh(host, f"cd {q} && git checkout -q -f --detach {SYNC_REF}",
             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"checkout on {host} failed: {r.stderr.strip()}")
    return commit


class RemoteRun:
    """docker/run.sh --full on the Linux machine, in a background thread.

    The full output goes to log_path; ECHO_PREFIXES lines are also printed
    here with a [linux] prefix. After join(), ok is True if the run exited 0.
    """

    def __init__(self, host, remote_dir, build_all_args, log_path):
        self.host, self.remote_dir, self.log_path = host, remote_dir, log_path
        q = shlex.quote(remote_dir)
        args = " ".join(shlex.quote(a) for a in build_all_args)
        # Remove the previous results first, so a run that stops early can't
        # leave an old file to be read as this run's. sh: the executable bit
        # isn't kept when the working tree comes from Windows.
        self.command = (f"cd {q} && rm -f full-suite-results/linux.json && "
                        f"DMQ_CONTAINER={CONTAINER} sh docker/run.sh --full {args}")
        self.ok = False
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()

    def join(self):
        self.thread.join()

    def _run(self):
        os.makedirs(os.path.dirname(self.log_path), exist_ok=True)
        with open(self.log_path, "w", encoding="utf-8") as log:
            proc = subprocess.Popen(["ssh", "-o", "BatchMode=yes", self.host, self.command],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    encoding="utf-8", errors="replace")
            for line in proc.stdout:
                log.write(line)
                log.flush()
                if line.startswith(ECHO_PREFIXES):
                    print(f"[linux] {line.rstrip()}", flush=True)
            self.ok = proc.wait() == 0

    def stop(self):
        """Stop the container (after Ctrl+C here, it would otherwise keep running)."""
        _ssh(self.host, f"docker rm -f {CONTAINER} >/dev/null 2>&1",
             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def fetch_results(host, remote_dir, dest):
    """Copy the Linux run's results file to dest; return False if it doesn't exist."""
    r = _ssh(host, f"cat {shlex.quote(remote_dir)}/full-suite-results/linux.json",
             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if r.returncode != 0 or not r.stdout:
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(r.stdout)
    return True
