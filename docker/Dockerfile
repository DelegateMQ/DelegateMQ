# DelegateMQ full test suite: the Linux tools `build_all.py --full` needs.
#
# Toolchains and system packages live in the image. The third-party libraries
# in external/ do not: build_all.py fetches and builds them with
# scripts/01_fetch_repos.py and 02_build_libs.py into a directory run.sh
# mounts at /deps, so their versions stay defined in one place.
#
# Build and run through docker/run.sh (see docs/BUILD.md, "Full test suite").

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# gcc, clang with its sanitizer runtimes (libclang-rt, only a recommended
# package of clang), CMake and Ninja; git for 01_fetch_repos.py; Python with
# the cross-language sample clients' modules.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
        libclang-rt-18-dev \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
        python3 \
        python3-msgpack \
        python3-zmq \
    && rm -rf /var/lib/apt/lists/*

# run.sh runs the container as the calling user so files it writes in the
# checkout stay theirs; that user has no home directory in the image.
ENV HOME=/tmp \
    PYTHONUNBUFFERED=1 \
    DMQ_DEPS_DIR=/deps

WORKDIR /work
