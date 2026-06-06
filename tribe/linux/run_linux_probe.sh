#!/usr/bin/env bash
set -euo pipefail
ulimit -s unlimited 2>/dev/null || true

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINUX_DIR="${LINUX_DIR:-${ROOT_DIR}/tribe/linux}"
TRIBE_BIN="${TRIBE_BIN:-${ROOT_DIR}/build/tribe_linux/tribe64_linux}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${LINUX_DIR}/Image}"
KERNEL_ELF="${KERNEL_ELF:-${LINUX_DIR}/vmlinux}"
VMLINUX_ARCHIVE="${VMLINUX_ARCHIVE:-${LINUX_DIR}/vmlinux.tgz}"
DTB="${DTB:-${LINUX_DIR}/config32.dtb}"
DTS="${DTS:-${LINUX_DIR}/config32.dts}"
INITRAMFS_GZ="${INITRAMFS_GZ:-${LINUX_DIR}/initramfs.cpio.gz}"
INITRAMFS="${INITRAMFS:-${LINUX_DIR}/initramfs.cpio}"
INITRAMFS_ADDR="${INITRAMFS_ADDR:-0x81c00000}"
DTB_WITH_INITRD="${DTB_WITH_INITRD:-${LINUX_DIR}/config32.initramfs.dtb}"
DTS_WITH_INITRD="${DTS_WITH_INITRD:-${LINUX_DIR}/config32.initramfs.dts}"
TRIBE_RAM_BYTES="${TRIBE_RAM_BYTES:-33554432}"
TRIBE_IO_BYTES="${TRIBE_IO_BYTES:-4194304}"
# Keep Linux timer IRQ load low enough for the slow RTL simulator to leave
# hardirq context and service PLIC/UART interrupts during interactive use.
TRIBE_CLINT_TICK_DIV="${TRIBE_CLINT_TICK_DIV:-256}"
TRIBE_LINUX_INTERACTIVE="${TRIBE_LINUX_INTERACTIVE:-1}"
TRIBE_LINUX_TAIL_UART="${TRIBE_LINUX_TAIL_UART:-0}"
TRIBE_LINUX_BAUD="${TRIBE_LINUX_BAUD:-1000000}"
TRIBE_LINUX_BOOTARGS="${TRIBE_LINUX_BOOTARGS:-console=ttyS0,${TRIBE_LINUX_BAUD} earlycon=uart,mmio,0x82000000 unaligned_scalar_speed=slow}"
TRIBE_LINUX_SD_IMAGE="${TRIBE_LINUX_SD_IMAGE:-}"
TRIBE_LINUX_ETH_TAP_SOCKET="${TRIBE_LINUX_ETH_TAP_SOCKET:-}"
TRIBE_CPU_CLOCK_HZ="${TRIBE_CPU_CLOCK_HZ:-50000000}"
TRIBE_TIMEBASE_HZ="${TRIBE_TIMEBASE_HZ:-$((TRIBE_CPU_CLOCK_HZ / TRIBE_CLINT_TICK_DIV))}"

if [[ -n "${TRIBE_UART_INPUT_FILE:-}" ]]; then
    TRIBE_UART_INPUT="$(cat "${TRIBE_UART_INPUT_FILE}")"$'\n'
    export TRIBE_UART_INPUT
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sd-image)
            if [[ $# -lt 2 ]]; then
                echo "--sd-image requires a file path" >&2
                exit 1
            fi
            TRIBE_LINUX_SD_IMAGE="$2"
            shift 2
            ;;
        --eth-tap-socket)
            if [[ $# -lt 2 ]]; then
                echo "--eth-tap-socket requires a socket path" >&2
                exit 1
            fi
            TRIBE_LINUX_ETH_TAP_SOCKET="$2"
            shift 2
            ;;
        *)
            echo "unknown run_linux_probe.sh option: $1" >&2
            exit 1
            ;;
    esac
done

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

find_nm()
{
    local riscv_home="${RISCV_HOME:-/home/me/riscv}"
    local candidate

    for candidate in \
        "${NM:-}" \
        "${riscv_home}/bin/riscv32-unknown-linux-gnu-nm" \
        "${riscv_home}/bin/riscv32-unknown-elf-nm" \
        riscv32-unknown-linux-gnu-nm \
        riscv32-unknown-elf-nm \
        llvm-nm \
        nm
    do
        if [[ -n "${candidate}" ]] && command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done

    echo "missing nm; set NM or RISCV_HOME" >&2
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

is_newc_cpio()
{
    [[ -f "$1" ]] && [[ "$(head -c 6 "$1" 2>/dev/null || true)" == "070701" ]]
}

prepare_linux_inputs()
{
    local objcopy_bin
    local dtc_bin
    local initramfs_end

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

    if [[ ! -f "${INITRAMFS_GZ}" ]]; then
        echo "missing initramfs archive: ${INITRAMFS_GZ}" >&2
        exit 1
    fi
    if [[ ! -f "${INITRAMFS}" || "${INITRAMFS_GZ}" -nt "${INITRAMFS}" ]]; then
        gzip -dc "${INITRAMFS_GZ}" > "${INITRAMFS}"
    fi
    if ! is_newc_cpio "${INITRAMFS}"; then
        echo "initramfs must be an uncompressed newc cpio archive: ${INITRAMFS}" >&2
        exit 1
    fi

    initramfs_end="$(python3 - "${INITRAMFS_ADDR}" "${INITRAMFS}" <<'PY'
import os
import sys

start = int(sys.argv[1], 0)
size = os.path.getsize(sys.argv[2])
print(hex(start + size))
PY
)"

    if true; then
        python3 - "${DTS}" "${DTS_WITH_INITRD}" "${INITRAMFS_ADDR}" "${initramfs_end}" "${TRIBE_LINUX_BOOTARGS}" "${TRIBE_LINUX_BAUD}" "${TRIBE_TIMEBASE_HZ}" "${TRIBE_CPU_CLOCK_HZ}" <<'PY'
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
start = int(sys.argv[3], 0)
end = int(sys.argv[4], 0)
bootargs = sys.argv[5]
baud = int(sys.argv[6], 0)
timebase = int(sys.argv[7], 0)
cpu_clock = int(sys.argv[8], 0)
text = src.read_text(encoding="utf-8")
insert = (
    f"\t\tbootargs = \"{bootargs}\";\n"
    f"\t\tstdout-path = \"/soc/serial@82000000:{baud}n8\";\n"
    f"\t\tlinux,initrd-start = <0x{start:08x}>;\n"
    f"\t\tlinux,initrd-end = <0x{end:08x}>;\n"
)
lines = text.splitlines(keepends=True)
out = []
in_chosen = False
in_uart = False
inserted = False
for line in lines:
    stripped = line.strip()
    if stripped == "chosen {":
        in_chosen = True
        out.append(line)
        continue
    if "serial@82000000" in stripped and stripped.endswith("{"):
        in_uart = True
    elif in_chosen and stripped == "};" and not inserted:
        out.append(insert)
        out.append(line)
        in_chosen = False
        inserted = True
        continue
    elif in_chosen and (stripped.startswith("linux,initrd-") or
                        stripped.startswith("bootargs =") or
                        stripped.startswith("stdout-path =")):
        continue
    elif stripped.startswith("timebase-frequency ="):
        out.append(f"\t\ttimebase-frequency = <{timebase}>;\n")
        continue
    elif stripped.startswith("clock-frequency =") and not in_uart:
        out.append(f"\t\t\tclock-frequency = <{cpu_clock}>;\n")
        continue
    elif in_uart and stripped.startswith("current-speed ="):
        out.append(f"\t\t\tcurrent-speed = <{baud}>;\n")
        continue
    elif in_uart and stripped == "};":
        in_uart = False
    out.append(line)
if not inserted:
    raise SystemExit("failed to find /chosen in DTS")
dst.write_text("".join(out), encoding="utf-8")
PY
        dtc_bin="$(find_dtc)"
        "${dtc_bin}" -I dts -O dtb -o "${DTB_WITH_INITRD}" "${DTS_WITH_INITRD}"
    fi
}

if [[ "${TRIBE_LINUX_INPUTS_PREPARED:-0}" != "1" ]]; then
    prepare_linux_inputs
    export TRIBE_LINUX_INPUTS_PREPARED=1
    export TRIBE_LINUX_SD_IMAGE
    exec "${BASH}" "${BASH_SOURCE[0]}"
fi

newer_tribe_header=""
if [[ -x "${TRIBE_BIN}" ]]; then
    newer_tribe_header="$(find "${ROOT_DIR}/tribe" -maxdepth 3 -name '*.h' -newer "${TRIBE_BIN}" -print -quit)"
fi
TRIBE_BIN_CONFIG="${TRIBE_BIN}.config"
TRIBE_COMPILE_CONFIG="TRIBE_RAM_BYTES=${TRIBE_RAM_BYTES} TRIBE_IO_BYTES=${TRIBE_IO_BYTES} L2_AXI_WIDTH=64 TRIBE_CLINT_TICK_DIV=${TRIBE_CLINT_TICK_DIV}"
if [[ ! -x "${TRIBE_BIN}" || "${ROOT_DIR}/tribe/main.cpp" -nt "${TRIBE_BIN}" || -n "${newer_tribe_header}" || ! -f "${TRIBE_BIN_CONFIG}" || "$(cat "${TRIBE_BIN_CONFIG}")" != "${TRIBE_COMPILE_CONFIG}" ]]; then
    mkdir -p "$(dirname "${TRIBE_BIN}")"
    TRIBE_BIN_TMP="${TRIBE_BIN}.new.$$"
    rm -f "${TRIBE_BIN_TMP}"
    read -r -a CXX_CMD <<< "${CXX:-clang++}"
    "${CXX_CMD[@]}" "${ROOT_DIR}/tribe/main.cpp" \
        -std=c++26 -O3 -g ${CPPHDL_HOST_OPT_FLAGS:-} -fno-strict-aliasing \
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
        -DTRIBE_CLINT_TICK_DIV_CONFIG="${TRIBE_CLINT_TICK_DIV}" \
        -o "${TRIBE_BIN_TMP}" \
        -lstdc++exp
    mv -f "${TRIBE_BIN_TMP}" "${TRIBE_BIN}"
    printf '%s\n' "${TRIBE_COMPILE_CONFIG}" > "${TRIBE_BIN_CONFIG}"
fi

TRIBE_CHECKPOINT_ARGS=()
if [[ -n "${TRIBE_CHECKPOINT_LOAD:-}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--checkpoint-load "${TRIBE_CHECKPOINT_LOAD}")
fi
if [[ -n "${TRIBE_CHECKPOINT_SAVE:-}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--checkpoint-save "${TRIBE_CHECKPOINT_SAVE}")
fi
if [[ -n "${TRIBE_CHECKPOINT_SAVE_CYCLE:-}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--checkpoint-save-cycle "${TRIBE_CHECKPOINT_SAVE_CYCLE}")
fi
if [[ -n "${TRIBE_CHECKPOINT_SAVE_AFTER:-}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--checkpoint-save-after "${TRIBE_CHECKPOINT_SAVE_AFTER}")
fi
if [[ "${TRIBE_APPEND_OUTPUT:-0}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--append-output)
fi
if [[ "${TRIBE_LINUX_INTERACTIVE}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--uart-stdin)
fi
if [[ "${TRIBE_LINUX_MIRROR_UART:-0}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--mirror-uart)
fi
if [[ -n "${TRIBE_LINUX_SD_IMAGE}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--sd-image "${TRIBE_LINUX_SD_IMAGE}")
fi
if [[ -n "${TRIBE_LINUX_ETH_TAP_SOCKET}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--eth-tap-socket "${TRIBE_LINUX_ETH_TAP_SOCKET}")
fi
if [[ -n "${TRIBE_EXPECTED_OUTPUT_CONTAINS:-}" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--expected-output-contains "${TRIBE_EXPECTED_OUTPUT_CONTAINS}")
fi

if [[ "${TRIBE_LINUX_TRACE_PC_SYMBOLS:-0}" == "1" ]]; then
    export TRIBE_TRACE_PC_PERIOD="${TRIBE_TRACE_PC_PERIOD:-10000}"
    PC_SYMBOLS_FILE="${LINUX_DIR}/vmlinux.nm"
    if [[ ! -f "${PC_SYMBOLS_FILE}" || "${KERNEL_ELF}" -nt "${PC_SYMBOLS_FILE}" ]]; then
        "$(find_nm)" -n "${KERNEL_ELF}" > "${PC_SYMBOLS_FILE}"
    fi
    export TRIBE_TRACE_PC_SYMBOLS_FILE="${PC_SYMBOLS_FILE}"
fi

TRIBE_RUN_ARGS=(
    "${TRIBE_BIN}"
    --noveril
    --program "${KERNEL_ELF}"
    --elf-phys-base 0x80000000
    --start-mem-addr 0x80000000
    --ram-size "$((TRIBE_RAM_BYTES / 4))"
    --dtb "${DTB_WITH_INITRD}"
    --boot-hartid 0
    --boot-dtb-addr 0x81f00000
    --boot-priv s
    --initramfs "${INITRAMFS}"
    --initramfs-addr "${INITRAMFS_ADDR}"
    --tohost 1
    --cycles "${TRIBE_LINUX_CYCLES:-0}"
    "${TRIBE_CHECKPOINT_ARGS[@]}"
)
if [[ "${TRIBE_LINUX_TAIL_UART}" == "1" ]]; then
    (
        cd "${LINUX_DIR}"
        : > out.txt
        tail -n +1 -f out.txt &
        tail_pid=$!
        trap 'kill "${tail_pid}" 2>/dev/null || true' EXIT
        "${TRIBE_RUN_ARGS[@]}"
    )
else
    cd "${LINUX_DIR}"
    exec "${TRIBE_RUN_ARGS[@]}"
fi
