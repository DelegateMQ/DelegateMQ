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

# Bare-metal samples: QEMU (Arm mps2 boards, RISC-V virt), Arm GNU toolchain
# with newlib and libstdc++ (Ubuntu packages), Arm Toolchain for Embedded
# (ATfE, clang/picolibc/libc++) and the xPack RISC-V Embedded GCC (Ubuntu's
# RISC-V package has no C/C++ libraries). Downloads are pinned and checked
# against the checksums their releases publish.
ARG ATFE_VERSION=22.1.0
ARG ATFE_SHA256=e2e9e637bba097ba6e4bae6982883fe705ffd7e8c3a7dc876964835ef1c7a724
ARG XPACK_RISCV_VERSION=15.2.0-1
ARG XPACK_RISCV_SHA256=aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758
RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-arm \
        qemu-system-misc \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        curl \
        xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && cd /tmp \
    && curl -fsSLO "https://github.com/arm/arm-toolchain/releases/download/release-${ATFE_VERSION}-ATfE/ATfE-${ATFE_VERSION}-Linux-x86_64.tar.xz" \
    && echo "${ATFE_SHA256}  ATfE-${ATFE_VERSION}-Linux-x86_64.tar.xz" | sha256sum -c - \
    && mkdir /opt/atfe \
    && tar -xJf "ATfE-${ATFE_VERSION}-Linux-x86_64.tar.xz" -C /opt/atfe --strip-components=1 \
    && rm "ATfE-${ATFE_VERSION}-Linux-x86_64.tar.xz" \
    && curl -fsSLO "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK_RISCV_VERSION}/xpack-riscv-none-elf-gcc-${XPACK_RISCV_VERSION}-linux-x64.tar.gz" \
    && echo "${XPACK_RISCV_SHA256}  xpack-riscv-none-elf-gcc-${XPACK_RISCV_VERSION}-linux-x64.tar.gz" | sha256sum -c - \
    && mkdir /opt/xpack-riscv-none-elf-gcc \
    && tar -xzf "xpack-riscv-none-elf-gcc-${XPACK_RISCV_VERSION}-linux-x64.tar.gz" -C /opt/xpack-riscv-none-elf-gcc --strip-components=1 \
    && rm "xpack-riscv-none-elf-gcc-${XPACK_RISCV_VERSION}-linux-x64.tar.gz"

# Where the bare-metal sample's CMake finds the downloaded toolchains
ENV ATFE_PATH=/opt/atfe \
    RISCV_TOOLCHAIN_PATH=/opt/xpack-riscv-none-elf-gcc

# Samples needing a library or service: autotools to build libserialport
# (02_build_libs.py) and socat for the virtual serial port pair
# (serialport-serializer), Qt 6 Core (qt-linux), the mosquitto MQTT broker
# 05_run_samples.py starts for mqtt-rapidjson
RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        libtool \
        socat \
        qt6-base-dev \
        mosquitto \
    && rm -rf /var/lib/apt/lists/*

# run.sh runs the container as the calling user so files it writes in the
# checkout stay theirs; that user has no home directory in the image.
ENV HOME=/tmp \
    PYTHONUNBUFFERED=1 \
    DMQ_DEPS_DIR=/deps

WORKDIR /work
