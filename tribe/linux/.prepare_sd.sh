#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINUX_DIR="${LINUX_DIR:-${ROOT_DIR}/tribe/linux}"
BUILD_DIR="${TRIBE_SD_BUILD_DIR:-${LINUX_DIR}/sd-build}"
ROOTFS_DIR="${TRIBE_SD_ROOTFS_DIR:-${BUILD_DIR}/rootfs}"
SD_IMG="${TRIBE_SD_IMG:-${LINUX_DIR}/sd.img}"
SD_IMAGE_MB="${TRIBE_SD_IMAGE_MB:-64}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"
CROSS_COMPILE="${CROSS_COMPILE:-${RISCV_HOME}/bin/riscv32-unknown-linux-gnu-}"
CC="${CC:-${CROSS_COMPILE}gcc}"
STRIP="${STRIP:-${CROSS_COMPILE}strip}"
TRIBE_SD_TEST_SRC="${TRIBE_SD_TEST_SRC:-${ROOT_DIR}/tribe/code/sd_test.c}"
STRESS_NG_REPO="${STRESS_NG_REPO:-https://github.com/ColinIanKing/stress-ng.git}"
STRESS_REPO="${STRESS_REPO:-https://github.com/resurrecting-open-source-projects/stress.git}"
STRESS_NG_REF="${STRESS_NG_REF:-master}"
STRESS_REF="${STRESS_REF:-master}"
TRIBE_SD_OFFLINE="${TRIBE_SD_OFFLINE:-0}"
TRIBE_SD_SKIP_STRESS_NG="${TRIBE_SD_SKIP_STRESS_NG:-0}"
TRIBE_SD_SKIP_STRESS="${TRIBE_SD_SKIP_STRESS:-0}"

usage()
{
    cat <<EOF
Usage: $0 [--pack-only] [--clean]

Builds RISC-V 32-bit Linux stress tools and packs them into a FAT32 SD image.

Environment:
  RISCV_HOME=/home/me/riscv
  CROSS_COMPILE=\$RISCV_HOME/bin/riscv32-unknown-linux-gnu-
  TRIBE_SD_IMG=$SD_IMG
  TRIBE_SD_IMAGE_MB=$SD_IMAGE_MB
  TRIBE_SD_BUILD_DIR=$BUILD_DIR
  STRESS_NG_REPO=$STRESS_NG_REPO
  STRESS_REPO=$STRESS_REPO
  TRIBE_SD_OFFLINE=0
  TRIBE_SD_SKIP_STRESS_NG=0
  TRIBE_SD_SKIP_STRESS=0
EOF
}

PACK_ONLY=0
CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --pack-only)
            PACK_ONLY=1
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

if [[ "$CLEAN" == "1" ]]; then
    rm -rf "${BUILD_DIR}" "${SD_IMG}"
fi

mkdir -p "${BUILD_DIR}" "${ROOTFS_DIR}"

clone_or_update()
{
    local repo="$1"
    local ref="$2"
    local dir="$3"

    if [[ ! -d "${dir}/.git" ]]; then
        if [[ "${TRIBE_SD_OFFLINE}" == "1" ]]; then
            echo "missing clone in offline mode: ${dir}" >&2
            exit 1
        fi
        git clone "${repo}" "${dir}"
    fi
    if [[ "${TRIBE_SD_OFFLINE}" == "1" ]]; then
        return
    fi
    git -C "${dir}" fetch --tags origin
    git -C "${dir}" checkout "${ref}"
    git -C "${dir}" pull --ff-only origin "${ref}" || true
}

install_tool()
{
    local src="$1"
    local dst="$2"

    if [[ ! -x "${src}" ]]; then
        echo "missing built tool: ${src}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${dst}")"
    cp "${src}" "${dst}"
    "${STRIP}" "${dst}" 2>/dev/null || true
}

restore_cached_tool()
{
    local dst="$1"
    shift
    local src

    if [[ -x "${dst}" ]]; then
        return 0
    fi
    for src in "$@"; do
        if [[ -x "${src}" ]]; then
            install_tool "${src}" "${dst}"
            return 0
        fi
    done
    return 1
}

build_stress_ng()
{
    local src="${BUILD_DIR}/stress-ng"

    clone_or_update "${STRESS_NG_REPO}" "${STRESS_NG_REF}" "${src}"
    make -C "${src}" clean >/dev/null 2>&1 || true
    make -C "${src}" -j"${JOBS}" \
        CC="${CC}" \
        CFLAGS="-march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static" \
        LDFLAGS="-static" \
        STATIC=1 \
        stress-ng
    install_tool "${src}/stress-ng" "${ROOTFS_DIR}/STRESSNG"
}

build_stress()
{
    local src="${BUILD_DIR}/stress"

    clone_or_update "${STRESS_REPO}" "${STRESS_REF}" "${src}"
    if [[ ! -x "${src}/configure" && -x "${src}/autogen.sh" ]]; then
        (cd "${src}" && ./autogen.sh)
    fi
    if [[ -x "${src}/configure" ]]; then
        (
            cd "${src}"
            make clean >/dev/null 2>&1 || true
            ./configure --host=riscv32-unknown-linux-gnu CC="${CC}" \
                CFLAGS="-march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static" \
                LDFLAGS="-static"
            make -j"${JOBS}"
        )
    elif [[ -f "${src}/Makefile" ]]; then
        make -C "${src}" clean >/dev/null 2>&1 || true
        make -C "${src}" -j"${JOBS}" CC="${CC}" \
            CFLAGS="-march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static" \
            LDFLAGS="-static"
    else
        "${CC}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static \
            -o "${src}/stress" "${src}/src/stress.c"
    fi
    install_tool "${src}/src/stress" "${ROOTFS_DIR}/STRESS" 2>/dev/null ||
        install_tool "${src}/stress" "${ROOTFS_DIR}/STRESS"
}

build_sd_test()
{
    local out="${BUILD_DIR}/sd_test"

    if [[ ! -f "${TRIBE_SD_TEST_SRC}" ]]; then
        echo "missing SD test source: ${TRIBE_SD_TEST_SRC}" >&2
        exit 1
    fi
    "${CC}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static -nostdlib \
        -ffreestanding -fno-builtin \
        -DTRIBE_SD_TEST_TINY \
        -o "${out}" "${TRIBE_SD_TEST_SRC}"
    install_tool "${out}" "${ROOTFS_DIR}/SDTEST"
}

write_readme()
{
    cat > "${ROOTFS_DIR}/README.TXT" <<'EOF'
Tribe RISC-V Linux stress SD image

Tools:
  STRESSNG
  STRESS
  SDTEST

Example after mounting the card:
  /mnt/STRESS --cpu 1 --io 1 --vm 1 --timeout 10
  /mnt/STRESSNG --cpu 1 --matrix 1 --timeout 10 --metrics-brief

Raw SD driver test:
  cp /mnt/SDTEST /tmp/SDTEST
  umount /mnt
  /tmp/SDTEST /dev/tribesd1 --passes 1

SDTEST is destructive: it overwrites the target block device with PRBS data.
EOF
}

pack_fat32()
{
    python3 - "${ROOTFS_DIR}" "${SD_IMG}" "${SD_IMAGE_MB}" <<'PY'
import math
import os
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
image = pathlib.Path(sys.argv[2])
image_mb = int(sys.argv[3], 0)
sector_size = 512
part_start = 2048
total_sectors = image_mb * 1024 * 1024 // sector_size
if image_mb < 64 or total_sectors <= part_start + 4096:
    raise SystemExit("TRIBE_SD_IMAGE_MB is too small")
part_sectors = total_sectors - part_start
reserved = 32
fat_count = 2
sectors_per_cluster = 1 if image_mb <= 128 else 8
root_cluster = 2

files = []
for path in sorted(root.iterdir()):
    if not path.is_file():
        continue
    name = path.name.upper()
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    if not base or len(base) > 8 or len(ext) > 3:
        raise SystemExit(f"file name is not FAT 8.3 compatible: {path.name}")
    data = path.read_bytes()
    files.append((base, ext, data))

spf = 1
while True:
    data_sectors = part_sectors - reserved - fat_count * spf
    clusters = data_sectors // sectors_per_cluster
    needed_spf = math.ceil((clusters + 2) * 4 / sector_size)
    if needed_spf == spf:
        break
    spf = needed_spf
if clusters < 65525:
    raise SystemExit("image is too small for FAT32")

fat = bytearray(spf * sector_size)
struct.pack_into("<I", fat, 0, 0x0ffffff8)
struct.pack_into("<I", fat, 4, 0xffffffff)
struct.pack_into("<I", fat, root_cluster * 4, 0x0fffffff)
cluster_data = {}
next_free = root_cluster + 1
entries = []
for base, ext, data in files:
    cluster_count = max(1, math.ceil(len(data) / (sectors_per_cluster * sector_size)))
    first = next_free
    for index in range(cluster_count):
        cluster = next_free
        next_free += 1
        value = 0x0fffffff if index == cluster_count - 1 else next_free
        struct.pack_into("<I", fat, cluster * 4, value)
        start = index * sectors_per_cluster * sector_size
        cluster_data[cluster] = data[start:start + sectors_per_cluster * sector_size]
    entries.append((base, ext, first, len(data)))

root_dir = bytearray(sectors_per_cluster * sector_size)
root_dir[0:11] = b"TRIBE SD   "
root_dir[11] = 0x08
for idx, (base, ext, first, size) in enumerate(entries, start=1):
    off = idx * 32
    if off + 32 > len(root_dir):
        raise SystemExit("too many root directory entries for this simple FAT32 packer")
    root_dir[off:off + 8] = base.encode("ascii").ljust(8, b" ")
    root_dir[off + 8:off + 11] = ext.encode("ascii").ljust(3, b" ")
    root_dir[off + 11] = 0x20
    struct.pack_into("<H", root_dir, off + 20, (first >> 16) & 0xffff)
    struct.pack_into("<H", root_dir, off + 26, first & 0xffff)
    struct.pack_into("<I", root_dir, off + 28, size)
cluster_data[root_cluster] = root_dir

boot = bytearray(sector_size)
boot[0:3] = b"\xeb\x3c\x90"
boot[3:11] = b"CPPHDL  "
struct.pack_into("<H", boot, 11, sector_size)
boot[13] = sectors_per_cluster
struct.pack_into("<H", boot, 14, reserved)
boot[16] = fat_count
struct.pack_into("<H", boot, 17, 0)
struct.pack_into("<H", boot, 19, part_sectors if part_sectors < 65536 else 0)
boot[21] = 0xf8
struct.pack_into("<H", boot, 22, 0)
struct.pack_into("<H", boot, 24, 63)
struct.pack_into("<H", boot, 26, 255)
struct.pack_into("<I", boot, 28, part_start)
struct.pack_into("<I", boot, 32, part_sectors if part_sectors >= 65536 else 0)
struct.pack_into("<I", boot, 36, spf)
struct.pack_into("<H", boot, 40, 0)
struct.pack_into("<H", boot, 42, 0)
struct.pack_into("<I", boot, 44, root_cluster)
struct.pack_into("<H", boot, 48, 1)
struct.pack_into("<H", boot, 50, 6)
boot[64] = 0x80
boot[66] = 0x29
struct.pack_into("<I", boot, 67, 0x43504844)
boot[71:82] = b"TRIBE SD   "
boot[82:90] = b"FAT32   "
boot[510:512] = b"\x55\xaa"

fsinfo = bytearray(sector_size)
struct.pack_into("<I", fsinfo, 0, 0x41615252)
struct.pack_into("<I", fsinfo, 484, 0x61417272)
struct.pack_into("<I", fsinfo, 488, 0xffffffff)
struct.pack_into("<I", fsinfo, 492, next_free)
struct.pack_into("<I", fsinfo, 508, 0xaa550000)

mbr = bytearray(sector_size)
entry = 446
mbr[entry] = 0x00
mbr[entry + 4] = 0x0c
struct.pack_into("<I", mbr, entry + 8, part_start)
struct.pack_into("<I", mbr, entry + 12, part_sectors)
mbr[510:512] = b"\x55\xaa"

image.parent.mkdir(parents=True, exist_ok=True)
with image.open("wb") as out:
    out.truncate(total_sectors * sector_size)
    out.seek(0)
    out.write(mbr)
    part_base = part_start * sector_size
    out.seek(part_base)
    out.write(boot)
    out.seek(part_base + sector_size)
    out.write(fsinfo)
    out.seek(part_base + 6 * sector_size)
    out.write(boot)
    out.seek(part_base + 7 * sector_size)
    out.write(fsinfo)
    fat0 = part_base + reserved * sector_size
    for fat_index in range(fat_count):
        out.seek(fat0 + fat_index * spf * sector_size)
        out.write(fat)
    data_off = fat0 + fat_count * spf * sector_size
    for cluster, data in cluster_data.items():
        off = data_off + (cluster - 2) * sectors_per_cluster * sector_size
        out.seek(off)
        out.write(data)

print(f"Created {image} ({image_mb} MiB, FAT32 partition starts at sector {part_start})")
for base, ext, _first, size in entries:
    print(f"  {base + ('.' + ext if ext else '')}: {size} bytes")
PY
}

if [[ ! -x "${CC}" ]]; then
    echo "missing RISC-V Linux compiler: ${CC}" >&2
    exit 1
fi

if [[ "${PACK_ONLY}" != "1" ]]; then
    rm -rf "${ROOTFS_DIR}"
    mkdir -p "${ROOTFS_DIR}"
    if [[ "${TRIBE_SD_SKIP_STRESS_NG}" != "1" ]]; then
        build_stress_ng
    else
        restore_cached_tool "${ROOTFS_DIR}/STRESSNG" "${BUILD_DIR}/stress-ng/stress-ng" || true
    fi
    if [[ "${TRIBE_SD_SKIP_STRESS}" != "1" ]]; then
        build_stress
    else
        restore_cached_tool "${ROOTFS_DIR}/STRESS" "${BUILD_DIR}/stress/src/stress" "${BUILD_DIR}/stress/stress" || true
    fi
else
    restore_cached_tool "${ROOTFS_DIR}/STRESSNG" "${BUILD_DIR}/stress-ng/stress-ng" || true
    restore_cached_tool "${ROOTFS_DIR}/STRESS" "${BUILD_DIR}/stress/src/stress" "${BUILD_DIR}/stress/stress" || true
fi

build_sd_test

write_readme
pack_fat32

echo
echo "SD image ready: ${SD_IMG}"
echo "Run Linux with:"
echo "  TRIBE_LINUX_SD_IMAGE=${SD_IMG} ./run_linux_probe.sh"
