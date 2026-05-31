#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINUX_DIR="${LINUX_DIR:-${ROOT_DIR}/tribe/linux}"
KERNEL_SRC="${KERNEL_SRC:-${LINUX_DIR}/linux-build/linux}"
KERNEL_OUT="${KERNEL_OUT:-}"
KERNEL_CONFIG="${KERNEL_CONFIG:-${LINUX_DIR}/config-v6.19-rv32}"
TRIBE_SD_DRIVER="${TRIBE_SD_DRIVER:-${LINUX_DIR}/tribe_sd.c}"
RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"
CROSS_COMPILE="${CROSS_COMPILE:-${RISCV_HOME}/bin/riscv32-unknown-linux-gnu-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TRIBE_CPU_CLOCK_HZ="${TRIBE_CPU_CLOCK_HZ:-50000000}"
TRIBE_CLINT_TICK_DIV="${TRIBE_CLINT_TICK_DIV:-256}"
TRIBE_TIMEBASE_HZ="${TRIBE_TIMEBASE_HZ:-$((TRIBE_CPU_CLOCK_HZ / TRIBE_CLINT_TICK_DIV))}"
TRIBE_LINUX_BAUD="${TRIBE_LINUX_BAUD:-1000000}"
TRIBE_LINUX_BOOTARGS="${TRIBE_LINUX_BOOTARGS:-console=ttyS0,${TRIBE_LINUX_BAUD} earlycon=uart,mmio,0x82000000 unaligned_scalar_speed=slow}"

usage()
{
    cat <<EOF
Usage: $0 [--no-build] [--clean]

Prepare and build the Tribe RV32 Linux kernel from an already-cloned tree.

Defaults:
  KERNEL_SRC=${KERNEL_SRC}
  KERNEL_CONFIG=${KERNEL_CONFIG}
  TRIBE_SD_DRIVER=${TRIBE_SD_DRIVER}
  CROSS_COMPILE=${CROSS_COMPILE}

Outputs copied to ${LINUX_DIR}:
  vmlinux
  vmlinux.tgz
  vmlinux.nm
  Image
  System.map
EOF
}

NO_BUILD=0
CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -d "${KERNEL_SRC}/.git" ]]; then
    echo "missing cloned Linux kernel tree: ${KERNEL_SRC}" >&2
    echo "clone or move Linux there first; this script intentionally does not clone" >&2
    exit 1
fi
if [[ ! -f "${TRIBE_SD_DRIVER}" ]]; then
    echo "missing Tribe SD driver source: ${TRIBE_SD_DRIVER}" >&2
    exit 1
fi
if [[ ! -f "${KERNEL_CONFIG}" ]]; then
    echo "missing kernel config: ${KERNEL_CONFIG}" >&2
    exit 1
fi

MAKE_ARGS=(ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}")
if [[ -n "${KERNEL_OUT}" ]]; then
    mkdir -p "${KERNEL_OUT}"
    MAKE_ARGS+=(O="${KERNEL_OUT}")
    BUILD_DIR="${KERNEL_OUT}"
else
    BUILD_DIR="${KERNEL_SRC}"
fi

kernel_make()
{
    make -C "${KERNEL_SRC}" "${MAKE_ARGS[@]}" "$@"
}

write_tribe_dts()
{
    python3 - "${LINUX_DIR}/config32.dts" "${TRIBE_LINUX_BOOTARGS}" "${TRIBE_LINUX_BAUD}" "${TRIBE_TIMEBASE_HZ}" "${TRIBE_CPU_CLOCK_HZ}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
bootargs = sys.argv[2]
baud = int(sys.argv[3], 0)
timebase = int(sys.argv[4], 0)
cpu_clock = int(sys.argv[5], 0)

path.write_text(f"""/dts-v1/;

/ {{
\tmodel = "CppHDL Tribe";
\t#address-cells = <1>;
\t#size-cells = <1>;
\tcompatible = "cpphdl,tribe";

\taliases {{
\t\tserial0 = &uart0;
\t}};

\tchosen {{
\t\tbootargs = "{bootargs}";
\t\tstdout-path = "/soc/serial@82000000:{baud}n8";
\t}};

\tcpus {{
\t\t#address-cells = <1>;
\t\t#size-cells = <0>;
\t\ttimebase-frequency = <{timebase}>;

\t\tcpu@0 {{
\t\t\tdevice_type = "cpu";
\t\t\treg = <0>;
\t\t\tstatus = "okay";
\t\t\tcompatible = "riscv";
\t\t\triscv,isa = "rv32imac";
\t\t\triscv,isa-base = "rv32i";
\t\t\triscv,isa-extensions = "i", "m", "a", "c";
\t\t\tmmu-type = "riscv,sv32";
\t\t\tclock-frequency = <{cpu_clock}>;

\t\t\tcpu_intc: interrupt-controller {{
\t\t\t\t#interrupt-cells = <1>;
\t\t\t\tinterrupt-controller;
\t\t\t\tcompatible = "riscv,cpu-intc";
\t\t\t}};
\t\t}};
\t}};

\tmemory@80000000 {{
\t\tdevice_type = "memory";
\t\treg = <0x80000000 0x02000000>;
\t}};

\tsoc {{
\t\t#address-cells = <1>;
\t\t#size-cells = <1>;
\t\tcompatible = "simple-bus";
\t\tranges;

\t\tuart0: serial@82000000 {{
\t\t\tcompatible = "ns16550a";
\t\t\treg = <0x82000000 0x100>;
\t\t\tclock-frequency = <{cpu_clock}>;
\t\t\tcurrent-speed = <{baud}>;
\t\t\treg-shift = <0>;
\t\t\treg-io-width = <1>;
\t\t\tfifo-size = <4096>;
\t\t\tinterrupt-parent = <&plic0>;
\t\t\tinterrupts = <1>;
\t\t\tstatus = "okay";
\t\t}};

\t\tclint0: clint@82000100 {{
\t\t\tcompatible = "sifive,clint0", "riscv,clint0";
\t\t\treg = <0x82000100 0x0000c000>;
\t\t\tinterrupts-extended = <&cpu_intc 3 &cpu_intc 7>;
\t\t}};

\t\tsd0: sd@8200d100 {{
\t\t\tcompatible = "cpphdl,tribe-sd";
\t\t\treg = <0x8200d100 0x100>;
\t\t\tinterrupt-parent = <&plic0>;
\t\t\tinterrupts = <2>;
\t\t\ttribe,capacity-sectors = <131072>;
\t\t\tstatus = "okay";
\t\t}};

\t\tplic0: interrupt-controller@82010000 {{
\t\t\t#interrupt-cells = <1>;
\t\t\tinterrupt-controller;
\t\t\tcompatible = "riscv,plic0";
\t\t\treg = <0x82010000 0x00210000>;
\t\t\triscv,ndev = <31>;
\t\t\tinterrupts-extended = <&cpu_intc 9>;
\t\t}};
\t}};
}};
""", encoding="utf-8")
PY

    if command -v "${DTC:-dtc}" >/dev/null 2>&1; then
        "${DTC:-dtc}" -I dts -O dtb -o "${LINUX_DIR}/config32.dtb" "${LINUX_DIR}/config32.dts"
    else
        echo "warning: missing dtc, leaving ${LINUX_DIR}/config32.dtb for run_linux_probe.sh to rebuild" >&2
    fi
}

patch_tribe_sd_driver()
{
    cp "${TRIBE_SD_DRIVER}" "${KERNEL_SRC}/drivers/block/tribe_sd.c"

    python3 - "${KERNEL_SRC}/drivers/block/Kconfig" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
block = """config BLK_DEV_TRIBE_SD
\tbool "CppHDL Tribe SD block device"
\tdepends on OF
\thelp
\t  Built-in block driver for the custom CppHDL Tribe SD controller.

"""
if "config BLK_DEV_TRIBE_SD" not in text:
    needle = "config BLK_DEV_LOOP\n"
    if needle not in text:
        raise SystemExit("could not find BLK_DEV_LOOP insertion point in drivers/block/Kconfig")
    text = text.replace(needle, block + needle, 1)
    path.write_text(text)
PY

    python3 - "${KERNEL_SRC}/drivers/block/Makefile" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
line = "obj-$(CONFIG_BLK_DEV_TRIBE_SD)\t+= tribe_sd.o\n"
if line not in text:
    needle = "obj-$(CONFIG_BLK_DEV_LOOP)\t+= loop.o\n"
    if needle not in text:
        raise SystemExit("could not find BLK_DEV_LOOP insertion point in drivers/block/Makefile")
    text = text.replace(needle, line + needle, 1)
    path.write_text(text)
PY
}

prepare_config()
{
    local dst_config="${BUILD_DIR}/.config"
    mkdir -p "${BUILD_DIR}"
    cp "${KERNEL_CONFIG}" "${dst_config}"

    if [[ -x "${KERNEL_SRC}/scripts/config" ]]; then
        "${KERNEL_SRC}/scripts/config" --file "${dst_config}" -e BLK_DEV_TRIBE_SD
        "${KERNEL_SRC}/scripts/config" --file "${dst_config}" -e EXT2_FS
    else
        for option in BLK_DEV_TRIBE_SD EXT2_FS; do
            if grep -q "^CONFIG_${option}=" "${dst_config}"; then
                sed -i "s/^CONFIG_${option}=.*/CONFIG_${option}=y/" "${dst_config}"
            elif grep -q "^# CONFIG_${option} is not set" "${dst_config}"; then
                sed -i "s/^# CONFIG_${option} is not set/CONFIG_${option}=y/" "${dst_config}"
            else
                printf '\nCONFIG_%s=y\n' "${option}" >> "${dst_config}"
            fi
        done
    fi

    kernel_make olddefconfig
}

copy_outputs()
{
    local image="${BUILD_DIR}/arch/riscv/boot/Image"
    local vmlinux="${BUILD_DIR}/vmlinux"
    local system_map="${BUILD_DIR}/System.map"
    local nm_bin

    for file in "${image}" "${vmlinux}" "${system_map}"; do
        if [[ ! -f "${file}" ]]; then
            echo "missing built kernel output: ${file}" >&2
            exit 1
        fi
    done

    cp "${image}" "${LINUX_DIR}/Image"
    cp "${vmlinux}" "${LINUX_DIR}/vmlinux"
    cp "${system_map}" "${LINUX_DIR}/System.map"

    nm_bin="${NM:-${CROSS_COMPILE}nm}"
    if command -v "${nm_bin}" >/dev/null 2>&1; then
        "${nm_bin}" -n "${LINUX_DIR}/vmlinux" > "${LINUX_DIR}/vmlinux.nm"
    else
        echo "warning: missing nm '${nm_bin}', skipping ${LINUX_DIR}/vmlinux.nm" >&2
    fi

    tar -C "${LINUX_DIR}" -czf "${LINUX_DIR}/vmlinux.tgz" vmlinux
    echo "updated ${LINUX_DIR}/Image"
    echo "updated ${LINUX_DIR}/System.map"
    echo "updated ${LINUX_DIR}/vmlinux"
    echo "updated ${LINUX_DIR}/vmlinux.tgz"
}

if [[ "${CLEAN}" == "1" ]]; then
    kernel_make clean
fi

patch_tribe_sd_driver
write_tribe_dts
prepare_config

if [[ "${NO_BUILD}" != "1" ]]; then
    kernel_make -j"${JOBS}" Image vmlinux
    copy_outputs
else
    echo "prepared Linux tree without building: ${KERNEL_SRC}"
fi
