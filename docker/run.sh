#!/bin/sh
# Run build_all.py in the DelegateMQ full-suite container (docker/Dockerfile).
#
#   docker/run.sh --full              full test suite
#   docker/run.sh --full --from 3     resume at step 03
#   docker/run.sh --full --clean      reconfigure every sample from scratch
#
# Arguments go to build_all.py. The image is (re)built first; Docker's cache
# makes that a no-op unless the Dockerfile changed.
#
# The checkout is mounted at /work, so build directories land in it as usual,
# owned by the calling user. Note that a build directory configured here
# records /work paths: run 03 with --clean before building it outside the
# container. Third-party libraries (external/) go to DMQ_DEPS_DIR, by default
# ~/.cache/delegatemq/external, mounted at /deps and kept between runs.
#
# Environment:
#   DMQ_IMAGE      image name (default delegatemq-full)
#   DMQ_DEPS_DIR   host directory for third-party libraries
#   DMQ_CONTAINER  container name (default delegatemq-full-run); a container
#                  left running under that name by an interrupted run is
#                  removed first

set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${DMQ_IMAGE:-delegatemq-full}
DEPS=${DMQ_DEPS_DIR:-$HOME/.cache/delegatemq/external}
CONTAINER=${DMQ_CONTAINER:-delegatemq-full-run}

mkdir -p "$DEPS"
docker build -q -t "$IMAGE" "$REPO/docker" >/dev/null
docker rm -f "$CONTAINER" >/dev/null 2>&1 || true

# -t only with a terminal, so output also streams through ssh and pipes
TTY=
[ -t 1 ] && TTY=-t

# seccomp=unconfined: ThreadSanitizer re-runs itself with address-space
# randomization off (personality(ADDR_NO_RANDOMIZE)) on kernels with high
# mmap randomization (Ubuntu 24.04), and Docker's default seccomp profile
# blocks that call. The container only runs this checkout's builds and tests.
exec docker run --rm --init $TTY --name "$CONTAINER" \
    --security-opt seccomp=unconfined \
    --user "$(id -u):$(id -g)" \
    -v "$REPO:/work" \
    -v "$DEPS:/deps" \
    "$IMAGE" python3 build_all.py "$@"
