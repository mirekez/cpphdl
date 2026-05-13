#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TRIBE_BIN="${TRIBE_BIN:-${ROOT_DIR}/build/tribe_linux/tribe64_linux}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${ROOT_DIR}/tribe/linux/Image}"
KERNEL_ELF="${KERNEL_ELF:-${ROOT_DIR}/tribe/linux/vmlinux}"
DTB="${DTB:-${ROOT_DIR}/tribe/linux/config32.dtb}"
TRIBE_RAM_BYTES="${TRIBE_RAM_BYTES:-33554432}"
TRIBE_IO_BYTES="${TRIBE_IO_BYTES:-1048576}"

if [[ ! -x "${TRIBE_BIN}" ]]; then
    mkdir -p "$(dirname "${TRIBE_BIN}")"
    clang++ "${ROOT_DIR}/tribe/main.cpp" \
        -std=c++26 -O2 -g -mavx2 -fno-strict-aliasing \
        -Wno-unknown-warning-option -Wno-deprecated-missing-comma-variadic-parameter \
        -I"${ROOT_DIR}/include" \
        -I"${ROOT_DIR}/tribe" \
        -I"${ROOT_DIR}/tribe/common" \
        -I"${ROOT_DIR}/tribe/spec" \
        -I"${ROOT_DIR}/tribe/cache" \
        -I"${ROOT_DIR}/tribe/devices" \
        -I"${ROOT_DIR}/examples/axi" \
        -DL2_AXI_WIDTH=64 \
        -DTRIBE_RAM_BYTES_CONFIG="${TRIBE_RAM_BYTES}" \
        -DTRIBE_IO_REGION_SIZE_CONFIG="${TRIBE_IO_BYTES}" \
        -o "${TRIBE_BIN}" \
        -lstdc++exp
fi

if [[ ! -f "${KERNEL_IMAGE}" ]]; then
    echo "missing Linux Image: ${KERNEL_IMAGE}" >&2
    exit 1
fi
if [[ ! -f "${KERNEL_ELF}" ]]; then
    echo "missing Linux vmlinux: ${KERNEL_ELF}" >&2
    exit 1
fi
if [[ ! -f "${DTB}" ]]; then
    echo "missing DTB: ${DTB}" >&2
    exit 1
fi

cd "${ROOT_DIR}"
"${TRIBE_BIN}" \
    --noveril \
    --program "${KERNEL_ELF}" \
    --elf-phys-base 0x80000000 \
    --start-mem-addr 0x80000000 \
    --ram-size $((TRIBE_RAM_BYTES / 4)) \
    --dtb "${DTB}" \
    --boot-hartid 0 \
    --boot-dtb-addr 0x81f00000 \
    --boot-priv s \
    --linux-earlycon-mapbase \
    --tohost 1 \
    --cycles "${TRIBE_LINUX_CYCLES:-20000}"
