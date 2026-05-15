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
INITRAMFS_SOURCE="${INITRAMFS_SOURCE:-/home/me/3/riscv32_linux_from_scratch/build/initramfs.cpio.gz}"
INITRAMFS_GZ="${INITRAMFS_GZ:-${LINUX_DIR}/initramfs.cpio.gz}"
INITRAMFS_BASE="${INITRAMFS_BASE:-${LINUX_DIR}/initramfs.base.cpio}"
INITRAMFS="${INITRAMFS:-${LINUX_DIR}/initramfs.cpio}"
INITRAMFS_ADDR="${INITRAMFS_ADDR:-0x81c00000}"
DTB_WITH_INITRD="${DTB_WITH_INITRD:-${LINUX_DIR}/config32.initramfs.dtb}"
DTS_WITH_INITRD="${DTS_WITH_INITRD:-${LINUX_DIR}/config32.initramfs.dts}"
TRIBE_RAM_BYTES="${TRIBE_RAM_BYTES:-33554432}"
TRIBE_IO_BYTES="${TRIBE_IO_BYTES:-1048576}"
TRIBE_LINUX_EARLYCON_MAPBASE="${TRIBE_LINUX_EARLYCON_MAPBASE:-0}"
TRIBE_LINUX_BUSYBOX_PROBE="${TRIBE_LINUX_BUSYBOX_PROBE:-1}"
TRIBE_LINUX_INTERACTIVE="${TRIBE_LINUX_INTERACTIVE:-1}"
TRIBE_LINUX_TAIL_UART="${TRIBE_LINUX_TAIL_UART:-0}"

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

    if [[ ! -f "${INITRAMFS_GZ}" || "${INITRAMFS_SOURCE}" -nt "${INITRAMFS_GZ}" ]]; then
        if [[ ! -f "${INITRAMFS_SOURCE}" ]]; then
            echo "missing initramfs: ${INITRAMFS_SOURCE}" >&2
            exit 1
        fi
        cp "${INITRAMFS_SOURCE}" "${INITRAMFS_GZ}"
    fi

    if [[ ! -f "${INITRAMFS_BASE}" || "${INITRAMFS_GZ}" -nt "${INITRAMFS_BASE}" ]]; then
        gzip -dc "${INITRAMFS_GZ}" > "${INITRAMFS_BASE}"
    fi
    if ! is_newc_cpio "${INITRAMFS_BASE}"; then
        if is_newc_cpio "${INITRAMFS}"; then
            cp "${INITRAMFS}" "${INITRAMFS_BASE}"
        else
            echo "initramfs must be an uncompressed newc cpio archive: ${INITRAMFS_BASE}" >&2
            exit 1
        fi
    fi

    if [[ "${TRIBE_LINUX_BUSYBOX_PROBE}" == "1" ]]; then
        local init_probe
        init_probe="$(mktemp)"
        cat > "${init_probe}" <<'SH'
#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

echo
echo "RISC-V initramfs started"
echo "BusyBox probe follows"
/bin/busybox | /bin/busybox head -n 1
echo "BusyBox shell ready"

exec /bin/sh -i </dev/console >/dev/console 2>&1
SH
        python3 - "${INITRAMFS_BASE}" "${INITRAMFS}" "${init_probe}" <<'PY'
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
init = pathlib.Path(sys.argv[3]).read_bytes()
data = src.read_bytes()
pos = 0
out = bytearray()
replaced = False

def align4(value):
    return (value + 3) & ~3

while pos < len(data):
    header = bytearray(data[pos:pos + 110])
    if len(header) != 110 or header[:6] != b"070701":
        raise SystemExit(f"bad newc header at offset {pos}")
    fields = [int(header[6 + i * 8:14 + i * 8], 16) for i in range(13)]
    filesize = fields[6]
    namesize = fields[11]
    name_start = pos + 110
    name_end = name_start + namesize
    name = data[name_start:name_end - 1].decode("utf-8")
    body_start = align4(name_end)
    body_end = body_start + filesize
    next_pos = align4(body_end)
    body = data[body_start:body_end]
    if name in ("init", "./init"):
        fields[6] = len(init)
        body = init
        replaced = True
    out.extend(b"070701")
    for value in fields:
        out.extend(f"{value:08x}".encode("ascii"))
    out.extend(data[name_start:name_end])
    out.extend(b"\0" * (align4(len(out)) - len(out)))
    out.extend(body)
    out.extend(b"\0" * (align4(len(out)) - len(out)))
    pos = next_pos
    if name == "TRAILER!!!":
        break

if not replaced:
    raise SystemExit("failed to replace init in initramfs")
dst.write_bytes(out)
PY
        rm -f "${init_probe}"
    elif [[ ! -f "${INITRAMFS}" || "${INITRAMFS_BASE}" -nt "${INITRAMFS}" ]]; then
        cp "${INITRAMFS_BASE}" "${INITRAMFS}"
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
        python3 - "${DTS}" "${DTS_WITH_INITRD}" "${INITRAMFS_ADDR}" "${initramfs_end}" <<'PY'
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
start = int(sys.argv[3], 0)
end = int(sys.argv[4], 0)
text = src.read_text(encoding="utf-8")
insert = (
    f"\t\tlinux,initrd-start = <0x{start:08x}>;\n"
    f"\t\tlinux,initrd-end = <0x{end:08x}>;\n"
)
lines = text.splitlines(keepends=True)
out = []
in_chosen = False
inserted = False
for line in lines:
    stripped = line.strip()
    if stripped == "chosen {":
        in_chosen = True
    elif in_chosen and stripped == "};" and not inserted:
        out.append(insert)
        inserted = True
        in_chosen = False
    elif in_chosen and stripped.startswith("linux,initrd-"):
        continue
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
    exec "${BASH}" "${BASH_SOURCE[0]}"
fi

newer_tribe_header=""
if [[ -x "${TRIBE_BIN}" ]]; then
    newer_tribe_header="$(find "${ROOT_DIR}/tribe" -maxdepth 3 -name '*.h' -newer "${TRIBE_BIN}" -print -quit)"
fi
if [[ ! -x "${TRIBE_BIN}" || "${ROOT_DIR}/tribe/main.cpp" -nt "${TRIBE_BIN}" || -n "${newer_tribe_header}" ]]; then
    mkdir -p "$(dirname "${TRIBE_BIN}")"
    TRIBE_BIN_TMP="${TRIBE_BIN}.new.$$"
    rm -f "${TRIBE_BIN_TMP}"
    clang++ "${ROOT_DIR}/tribe/main.cpp" \
        -std=c++26 -O3 -g -mavx2 -fno-strict-aliasing \
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
        -o "${TRIBE_BIN_TMP}" \
        -lstdc++exp
    mv -f "${TRIBE_BIN_TMP}" "${TRIBE_BIN}"
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
if [[ "${TRIBE_APPEND_OUTPUT:-0}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--append-output)
fi
if [[ "${TRIBE_LINUX_EARLYCON_MAPBASE}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--linux-earlycon-mapbase)
fi
if [[ "${TRIBE_LINUX_INTERACTIVE}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--uart-stdin)
elif [[ "${TRIBE_LINUX_MIRROR_UART:-0}" == "1" ]]; then
    TRIBE_CHECKPOINT_ARGS+=(--mirror-uart)
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
    --cycles "${TRIBE_LINUX_CYCLES:-300000000}"
    "${TRIBE_CHECKPOINT_ARGS[@]}"
)
TRIBE_RUN_COMMAND="cd $(printf '%q' "${ROOT_DIR}") &&"
for arg in "${TRIBE_RUN_ARGS[@]}"; do
    TRIBE_RUN_COMMAND+=" $(printf '%q' "${arg}")"
done
if [[ "${TRIBE_LINUX_TAIL_UART}" == "1" ]]; then
    (
        cd "${ROOT_DIR}"
        : > out.txt
        tail -n +1 -f out.txt &
        tail_pid=$!
        trap 'kill "${tail_pid}" 2>/dev/null || true' EXIT
        bash -c "${TRIBE_RUN_COMMAND}"
    )
else
    bash -c "${TRIBE_RUN_COMMAND}"
fi
