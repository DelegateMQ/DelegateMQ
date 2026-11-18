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

# Where the bare-metal/qemu sample's CMake finds the downloaded toolchains
ENV ATFE_PATH=/opt/atfe \
    RISCV_TOOLCHAIN_PATH=/opt/xpack-riscv-none-elf-gcc

# Samples needing a library or service: autotools to build libserialport
# (02_build_libs.py) and socat for the virtual serial port pair
# (transport/serialport), Qt 6 Core (os/qt), the mosquitto MQTT broker
# 05_run_samples.py starts for transport/mqtt
RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        libtool \
        socat \
        qt6-base-dev \
        mosquitto \
    && rm -rf /var/lib/apt/lists/*

# Zephyr for the native_sim samples (os/zephyr, transport/zephyr-udp,
# databus/zephyr, os/cmsis-rtos2), at the version they were verified with.
# native_sim compiles with the host gcc (ZEPHYR_TOOLCHAIN_VARIANT=host), so no
# Zephyr SDK. Zephyr is a shallow clone, and west fetches only the modules
# native_sim needs (the filters each sample's README gives), also shallow.
# The venv comes first on PATH, so it also sees the system Python packages
# (python3-msgpack, python3-zmq for the cross-language sample clients).
ARG ZEPHYR_VERSION=v4.1.0
RUN apt-get update && apt-get install -y --no-install-recommends \
        device-tree-compiler \
        python3-venv \
        python3-dev \
        gperf \
        file \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv --system-site-packages /opt/zephyr-venv \
    && /opt/zephyr-venv/bin/pip install --no-cache-dir west \
    && mkdir /opt/zephyrproject \
    && git clone --depth 1 --branch ${ZEPHYR_VERSION} https://github.com/zephyrproject-rtos/zephyr.git /opt/zephyrproject/zephyr \
    && cd /opt/zephyrproject \
    && /opt/zephyr-venv/bin/west init -l zephyr \
    && /opt/zephyr-venv/bin/west config manifest.group-filter -- -hal,-optional,-babblesim,-crypto,-debug,-fs,-tee,-tools,-bootloader,-testing \
    && /opt/zephyr-venv/bin/west config manifest.project-filter -- -nrf_hw_models \
    && /opt/zephyr-venv/bin/west update --narrow -o=--depth=1 \
    && /opt/zephyr-venv/bin/pip install --no-cache-dir -r zephyr/scripts/requirements-base.txt

ENV ZEPHYR_BASE=/opt/zephyrproject/zephyr \
    ZEPHYR_TOOLCHAIN_VARIANT=host \
    PATH=/opt/zephyr-venv/bin:$PATH

# NuttX for the os/nuttx sample (NuttX's own sim board, a host process), at
# the release it was verified with, plus its host build tools. The sample's
# runner copies /opt/nuttx-src into its build directory, since NuttX builds
# in its source tree.
ARG NUTTX_RELEASE=releases/12.4
RUN apt-get update && apt-get install -y --no-install-recommends \
        bison \
        flex \
        gettext \
        texinfo \
        libncurses-dev \
        xxd \
        genromfs \
        kconfig-frontends \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /opt/nuttx-src \
    && git clone --depth 1 --branch ${NUTTX_RELEASE} https://github.com/apache/nuttx.git /opt/nuttx-src/nuttx \
    && git clone --depth 1 --branch ${NUTTX_RELEASE} https://github.com/apache/nuttx-apps.git /opt/nuttx-src/apps

ENV NUTTX_SRC=/opt/nuttx-src

# Unit tests on other CPUs (full_suite.TEST_VARIANTS "cross"): cross compilers
# for big-endian s390x and 32-bit Arm (armhf), run under QEMU user mode
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++-s390x-linux-gnu \
        g++-arm-linux-gnueabihf \
        qemu-user \
    && rm -rf /var/lib/apt/lists/*

# Zephyr on a Cortex-M board under QEMU (full_suite.ZEPHYR_QEMU_TARGETS:
# os/cmsis-rtos2 on mps2/an385): the cmsis HAL module, which Zephyr's Arm
# support needs, and the Zephyr SDK's Arm toolchain (minimal SDK plus
# arm-zephyr-eabi only). Not Ubuntu's arm-none-eabi: its newlib is built
# without the retargetable locking Zephyr requires for threads. The native_sim
# builds keep ZEPHYR_TOOLCHAIN_VARIANT=host; the Arm build selects "zephyr".
ARG ZEPHYR_SDK_VERSION=0.17.0
ARG ZEPHYR_SDK_MINIMAL_SHA256=0514d2c684dfb5f6327374bfed0b3dcf727ff1500195d26b3730f98252fed095
ARG ZEPHYR_SDK_ARM_SHA256=c3992a788a0896ecc87f7fb3a3be3f234ec0509220bd4e9dbd99b22edd55e97f
RUN cd /opt/zephyrproject \
    && west config manifest.project-filter -- -nrf_hw_models,+cmsis \
    && west update --narrow -o=--depth=1 cmsis \
    && cd /tmp \
    && SDK_URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}" \
    && curl -fsSLO "${SDK_URL}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" \
    && echo "${ZEPHYR_SDK_MINIMAL_SHA256}  zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" | sha256sum -c - \
    && curl -fsSLO "${SDK_URL}/toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz" \
    && echo "${ZEPHYR_SDK_ARM_SHA256}  toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz" | sha256sum -c - \
    && tar -xJf "zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" -C /opt \
    && tar -xJf toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz -C "/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}" \
    && rm "zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}

# run.sh runs the container as the calling user so files it writes in the
# checkout stay theirs; that user has no home directory in the image.
ENV HOME=/tmp \
    PYTHONUNBUFFERED=1 \
    DMQ_DEPS_DIR=/deps

WORKDIR /work
