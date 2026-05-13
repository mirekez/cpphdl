#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINUX_DIR="${LINUX_DIR:-${ROOT_DIR}/tribe/linux}"
TRIBE_BIN="${TRIBE_BIN:-${ROOT_DIR}/build/tribe_linux/tribe64_linux}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${LINUX_DIR}/Image}"
KERNEL_ELF="${KERNEL_ELF:-${LINUX_DIR}/vmlinux}"
VMLINUX_ARCHIVE="${VMLINUX_ARCHIVE:-${LINUX_DIR}/vmlinux.tgz}"
DTB="${DTB:-${LINUX_DIR}/config32.dtb}"
DTS="${DTS:-${LINUX_DIR}/config32.dts}"
TRIBE_RAM_BYTES="${TRIBE_RAM_BYTES:-33554432}"
TRIBE_IO_BYTES="${TRIBE_IO_BYTES:-1048576}"

find_objcopy()
{
    local riscv_home="${RISCV_HOME:-/home/me/riscv}"
    local candidate

    for candidate in \
        "${OBJCOPY:-}" \
        "${riscv_home}/bin/riscv32-unknown-elf-objcopy" \
        "${riscv_home}/bin/riscv64-unknown-elf-objcopy" \
        riscv32-unknown-elf-objcopy \
        riscv64-unknown-elf-objcopy \
        llvm-objcopy \
        objcopy
    do
        if [[ -n "${candidate}" ]] && command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done

    echo "missing objcopy; set OBJCOPY or RISCV_HOME" >&2
    return 1
}

find_dtc()
{
    local candidate

    for candidate in \
        "${DTC:-}" \
        dtc \
        /home/me/3/riscv32_linux_from_scratch/build/linux-rv32/scripts/dtc/dtc
    do
        if [[ -n "${candidate}" ]] && command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done

    echo "missing dtc; set DTC or install device-tree-compiler" >&2
    return 1
}

prepare_linux_inputs()
{
    local objcopy_bin
    local dtc_bin

    if [[ ! -f "${KERNEL_ELF}" || "${VMLINUX_ARCHIVE}" -nt "${KERNEL_ELF}" ]]; then
        if [[ ! -f "${VMLINUX_ARCHIVE}" ]]; then
            echo "missing Linux vmlinux archive: ${VMLINUX_ARCHIVE}" >&2
            exit 1
        fi
        mkdir -p "$(dirname "${KERNEL_ELF}")"
        tar -xzf "${VMLINUX_ARCHIVE}" -C "$(dirname "${KERNEL_ELF}")"
    fi

    if [[ ! -f "${KERNEL_IMAGE}" || "${KERNEL_ELF}" -nt "${KERNEL_IMAGE}" ]]; then
        objcopy_bin="$(find_objcopy)"
        "${objcopy_bin}" -O binary "${KERNEL_ELF}" "${KERNEL_IMAGE}"
    fi

    if [[ ! -f "${DTB}" || "${DTS}" -nt "${DTB}" ]]; then
        if [[ ! -f "${DTS}" ]]; then
            echo "missing DTS: ${DTS}" >&2
            exit 1
        fi
        dtc_bin="$(find_dtc)"
        "${dtc_bin}" -I dts -O dtb -o "${DTB}" "${DTS}"
    fi
}

prepare_linux_inputs

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
