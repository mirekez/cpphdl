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

patch_nbcon_default_prio()
{
    python3 - "${KERNEL_SRC}/kernel/printk/nbcon.c" <<'PY'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
pattern = re.compile(
    r"enum nbcon_prio nbcon_get_default_prio\(void\)\n"
    r"\{\n"
    r".*?"
    r"\n\}\n",
    re.S,
)
replacement = """enum nbcon_prio nbcon_get_default_prio(void)
{
\treturn NBCON_PRIO_NORMAL;
}
"""
updated, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit("could not patch nbcon_get_default_prio")
if updated != text:
    path.write_text(updated)
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
patch_nbcon_default_prio
prepare_config

if [[ "${NO_BUILD}" != "1" ]]; then
    kernel_make -j"${JOBS}" Image vmlinux
    copy_outputs
else
    echo "prepared Linux tree without building: ${KERNEL_SRC}"
fi
