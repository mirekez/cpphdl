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
TRIBE_CLOCK_TEST_SRC="${TRIBE_CLOCK_TEST_SRC:-${ROOT_DIR}/tribe/code/clock_test.c}"
STRESS_NG_REPO="${STRESS_NG_REPO:-https://github.com/ColinIanKing/stress-ng.git}"
STRESS_REPO="${STRESS_REPO:-https://github.com/resurrecting-open-source-projects/stress.git}"
STRACE_REPO="${STRACE_REPO:-https://github.com/strace/strace.git}"
STRESS_NG_REF="${STRESS_NG_REF:-master}"
STRESS_REF="${STRESS_REF:-master}"
STRACE_REF="${STRACE_REF:-master}"
TRIBE_SD_OFFLINE="${TRIBE_SD_OFFLINE:-0}"
TRIBE_SD_SKIP_STRESS_NG="${TRIBE_SD_SKIP_STRESS_NG:-0}"
TRIBE_SD_SKIP_STRESS="${TRIBE_SD_SKIP_STRESS:-0}"
TRIBE_SD_SKIP_STRACE="${TRIBE_SD_SKIP_STRACE:-0}"

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
  STRACE_REPO=$STRACE_REPO
  TRIBE_SD_OFFLINE=0
  TRIBE_SD_SKIP_STRESS_NG=0
  TRIBE_SD_SKIP_STRESS=0
  TRIBE_SD_SKIP_STRACE=0
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

patch_strace_riscv32()
{
    local src="$1"

    python3 - "${src}/configure.ac" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
if "riscv32*)" in text:
    raise SystemExit(0)

needle = "riscv64*)\n\tarch=riscv64\n\tkarch=riscv\n\tAC_DEFINE([RISCV64], 1, [Define for the RISC-V 64-bit architecture])\n\t;;"
replacement = "riscv32*)\n\tarch=riscv64\n\tkarch=riscv\n\tAC_DEFINE([RISCV64], 1, [Define for the RISC-V backend])\n\t;;\n" + needle
if needle not in text:
    raise SystemExit("could not find strace riscv64 configure case")
path.write_text(text.replace(needle, replacement))
print("patched strace configure.ac for riscv32")
PY

    if [[ ! -f "${src}/bundled/linux/include/uapi/linux/time_types.h" ]]; then
        local time_types="${RISCV_HOME}/riscv-gnu-toolchain/linux-headers/include/linux/time_types.h"
        if [[ -f "${time_types}" ]]; then
            cp "${time_types}" "${src}/bundled/linux/include/uapi/linux/time_types.h"
        fi
    fi
    if [[ -f "${src}/bundled/linux/include/uapi/linux/time_types.h" ]] &&
       ! grep -q "strace riscv32 compat" "${src}/bundled/linux/include/uapi/linux/time_types.h"; then
        python3 - "${src}/bundled/linux/include/uapi/linux/time_types.h" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
marker = "#include <linux/types.h>\n"
insert = (
    marker +
    "\n/* strace riscv32 compat: old bundled headers may lack this typedef. */\n"
    "#ifndef __kernel_old_time_t\n"
    "typedef __kernel_long_t __kernel_old_time_t;\n"
    "#endif\n"
)
path.write_text(text.replace(marker, insert, 1))
PY
    fi

    python3 - "${src}/src/linux/64/syscallent.h" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
if not path.exists():
    raise SystemExit(0)

text = path.read_text()
replacements = {
    "SEN(io_getevents_time64)": "SEN(io_getevents_time32)",
    "SEN(nanosleep_time64)": "SEN(nanosleep_time32)",
    "SEN(adjtimex64)": "SEN(adjtimex32)",
}
updated = text
for old, new in replacements.items():
    updated = updated.replace(old, new)
if updated != text:
    path.write_text(updated)
    print("patched strace riscv32 syscall table time64 aliases")
PY

    python3 - "${src}/src/strace.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
if "strace riscv32 getopt compat" in text:
    raise SystemExit(0)

needle = "\tlopt_idx = -1;\n\twhile ((c = getopt_long(argc, argv, optstring, longopts, &lopt_idx)) != EOF) {"
replacement = (
    "\t/* strace riscv32 getopt compat: keep the original command line so\n"
    "\t * we can recover PROG if the target libc getopt_long consumes all\n"
    "\t * non-option arguments while using the riscv64 backend on rv32. */\n"
    "\tint riscv32_orig_argc = argc;\n"
    "\tchar **riscv32_orig_argv = argv;\n\n" +
    needle
)
if needle not in text:
    raise SystemExit("could not find strace getopt loop")
text = text.replace(needle, replacement, 1)

needle = "\targv += optind;\n\targc -= optind;\n\n\tif (argc < 0 || (!nprocs && !argc)) {"
replacement = (
    "\targv += optind;\n"
    "\targc -= optind;\n"
    "\tif (!nprocs && argc <= 0) {\n"
    "\t\tfor (int riscv32_i = 1; riscv32_i < riscv32_orig_argc; ++riscv32_i) {\n"
    "\t\t\tif (!riscv32_orig_argv[riscv32_i])\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\tif (riscv32_orig_argv[riscv32_i][0] != '-' ||\n"
    "\t\t\t    riscv32_orig_argv[riscv32_i][1] == '\\0') {\n"
    "\t\t\t\targv = &riscv32_orig_argv[riscv32_i];\n"
    "\t\t\t\targc = riscv32_orig_argc - riscv32_i;\n"
    "\t\t\t\tbreak;\n"
    "\t\t\t}\n"
    "\t\t\tif (riscv32_orig_argv[riscv32_i][0] == '-' &&\n"
    "\t\t\t    riscv32_orig_argv[riscv32_i][1] == '-' &&\n"
    "\t\t\t    riscv32_orig_argv[riscv32_i][2] == '\\0' &&\n"
    "\t\t\t    riscv32_i + 1 < riscv32_orig_argc) {\n"
    "\t\t\t\targv = &riscv32_orig_argv[riscv32_i + 1];\n"
    "\t\t\t\targc = riscv32_orig_argc - riscv32_i - 1;\n"
    "\t\t\t\tbreak;\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t}\n\n"
    "\tif (argc < 0 || (!nprocs && !argc)) {"
)
if needle not in text:
    raise SystemExit("could not find strace argv/argc adjustment")
path.write_text(text.replace(needle, replacement, 1))
print("patched strace riscv32 getopt fallback")
PY
}

build_stress_ng()
{
    local src="${BUILD_DIR}/stress-ng"

    clone_or_update "${STRESS_NG_REPO}" "${STRESS_NG_REF}" "${src}"
    patch_stress_ng_tribe "${src}"
    make -C "${src}" clean >/dev/null 2>&1 || true
    make -C "${src}" -j"${JOBS}" \
        CC="${CC}" \
        CFLAGS="-march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static" \
        LDFLAGS="-static" \
        STATIC=1 \
        stress-ng
    install_tool "${src}/stress-ng" "${ROOTFS_DIR}/STRESSNG.BIN"
    install_stress_ng_wrapper
}

install_stress_ng_wrapper()
{
cat > "${ROOTFS_DIR}/STRESSNG" <<'EOF'
#!/bin/sh
set -e

if [ -d /mnt ]; then
    tribe_tmp=/mnt/TMP
else
    tribe_tmp="$(dirname "$0")/TMP"
fi

set -- "$@" ""
tribe_args=
while [ "$#" -gt 1 ]; do
    case "$1" in
        --temp-path)
            shift 2
            ;;
        --temp-path=*)
            shift
            ;;
        *)
            tribe_args="${tribe_args} '$1'"
            shift
            ;;
    esac
done

if [ -x /mnt/STRESSNG.BIN ]; then
    eval "exec /mnt/STRESSNG.BIN --temp-path '$tribe_tmp' ${tribe_args}"
fi

eval "exec '$(dirname "$0")/STRESSNG.BIN' --temp-path '$tribe_tmp' ${tribe_args}"
EOF
    chmod +x "${ROOTFS_DIR}/STRESSNG"
}

patch_stress_ng_tribe()
{
    local src="$1"

    python3 - "${src}/stress-ng.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
if "tribe stress-ng getopt fallback" in text:
    raise SystemExit(0)

marker = "int stress_opts_parse(int argc, char **argv, const bool jobmode)\n{"
helper = r'''
/* tribe stress-ng getopt fallback: rv32 static getopt_long currently accepts
 * global options such as --timeout, but can leave stressor options unlisted.
 * Repair only the workers explicitly requested by the command line. */
static bool stress_tribe_stressor_already_listed(const stress_stressor_t *stressor)
{
	stress_list_item_t *item;

	for (item = stress_stressor_list.head; item; item = item->next) {
		if (item->stressor == stressor)
			return true;
	}
	return false;
}

static void stress_tribe_stressor_fallback_add(const char *name, const char *value)
{
	size_t i;
	int32_t instances;

	if (!value || !*value)
		return;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (strcmp(stressors[i].name, name) != 0)
			continue;
		if (stress_tribe_stressor_already_listed(&stressors[i]))
			return;
		g_item_current = stress_list_item_find(&stressors[i]);
		g_opt_flags |= OPT_FLAGS_SET;
		instances = (int32_t)strtol(value, NULL, 0);
		stress_check_max_stressors(name, instances);
		g_item_current->instances = instances;
		if (stress_tribe_time_debug_enabled()) {
			(void)fprintf(stderr,
				"TRIBE_STRESS_ARG fallback stressor=%s instances=%" PRId32 "\n",
				name, instances);
			(void)fflush(stderr);
		}
		return;
	}
}

static const char *stress_tribe_next_arg(int *i, int argc, char **argv)
{
	if (*i + 1 >= argc)
		return NULL;
	(*i)++;
	return argv[*i];
}

static void stress_tribe_stressor_fallback(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!arg)
			continue;
		if (strcmp(arg, "--cpu") == 0) {
			stress_tribe_stressor_fallback_add("cpu", stress_tribe_next_arg(&i, argc, argv));
		} else if (strncmp(arg, "--cpu=", 6) == 0) {
			stress_tribe_stressor_fallback_add("cpu", arg + 6);
		} else if (strcmp(arg, "-c") == 0) {
			stress_tribe_stressor_fallback_add("cpu", stress_tribe_next_arg(&i, argc, argv));
		} else if (arg[0] == '-' && arg[1] == 'c' && arg[2] != '\0') {
			stress_tribe_stressor_fallback_add("cpu", arg + 2);
		} else if (strcmp(arg, "--vm") == 0) {
			stress_tribe_stressor_fallback_add("vm", stress_tribe_next_arg(&i, argc, argv));
		} else if (strncmp(arg, "--vm=", 5) == 0) {
			stress_tribe_stressor_fallback_add("vm", arg + 5);
		} else if (strcmp(arg, "-m") == 0) {
			stress_tribe_stressor_fallback_add("vm", stress_tribe_next_arg(&i, argc, argv));
		} else if (arg[0] == '-' && arg[1] == 'm' && arg[2] != '\0') {
			stress_tribe_stressor_fallback_add("vm", arg + 2);
		}
	}
}

'''
if marker not in text:
    raise SystemExit("could not find stress_opts_parse")
text = text.replace(marker, helper + "\n" + marker, 1)

needle = "\tif (optind < argc) {"
replacement = "\tstress_tribe_stressor_fallback(argc, argv);\n\n" + needle
if needle not in text:
    raise SystemExit("could not find optind check")
text = text.replace(needle, replacement, 1)
path.write_text(text)
print("patched stress-ng Tribe stressor fallback")
PY

    python3 - "${src}/core-mmap.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()

needle = """#else
\tconst int prot_flag = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);

\treturn mmap(NULL, length, prot_flag, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
#endif
}
"""
replacement = """#else
\tconst int prot_flag = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);
\tvoid *addr;

\taddr = mmap(NULL, length, prot_flag, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
#if defined(HAVE_SYS_SHM_H)
\t/* tribe stress-ng shared mmap fallback: on the small Tribe Linux image,
\t * anonymous shared mmap can fail with EROFS when shmem/tmpfs backing is
\t * unavailable. SysV shared memory gives stress-ng the same parent/child
\t * sharing semantics without depending on the filesystem path. */
\tif (addr == MAP_FAILED && errno == EROFS) {
\t\tconst int saved_errno = errno;
\t\tconst int shm_flag = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
\t\tint shmid = shmget(IPC_PRIVATE, length, shm_flag);

\t\tif (shmid >= 0) {
\t\t\taddr = shmat(shmid, NULL, 0);
\t\t\t(void)shmctl(shmid, IPC_RMID, NULL);
\t\t\tif (addr != (void *)-1) {
\t\t\t\t(void)mprotect(addr, length, prot_flag);
\t\t\t\treturn addr;
\t\t\t}
\t\t}
\t\taddr = mmap(NULL, length, prot_flag, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
\t\tif (addr != MAP_FAILED)
\t\t\treturn addr;
\t\terrno = saved_errno;
\t}
#endif
\treturn addr;
#endif
}
"""
if "tribe stress-ng shared mmap fallback" not in text:
    if needle not in text:
        raise SystemExit("could not find stress_mmap_anon_shared mmap return")
    path.write_text(text.replace(needle, replacement, 1))
    print("patched stress-ng shared mmap fallback")
elif "MAP_ANONYMOUS | MAP_PRIVATE" not in text:
    needle = """\t\tif (shmid >= 0) {
\t\t\taddr = shmat(shmid, NULL, 0);
\t\t\t(void)shmctl(shmid, IPC_RMID, NULL);
\t\t\tif (addr != (void *)-1) {
\t\t\t\t(void)mprotect(addr, length, prot_flag);
\t\t\t\treturn addr;
\t\t\t}
\t\t}
\t\terrno = saved_errno;
"""
    replacement = """\t\tif (shmid >= 0) {
\t\t\taddr = shmat(shmid, NULL, 0);
\t\t\t(void)shmctl(shmid, IPC_RMID, NULL);
\t\t\tif (addr != (void *)-1) {
\t\t\t\t(void)mprotect(addr, length, prot_flag);
\t\t\t\treturn addr;
\t\t\t}
\t\t}
\t\taddr = mmap(NULL, length, prot_flag, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
\t\tif (addr != MAP_FAILED)
\t\t\treturn addr;
\t\terrno = saved_errno;
"""
    if needle not in text:
        raise SystemExit("could not update existing stress-ng shared mmap fallback")
    path.write_text(text.replace(needle, replacement, 1))
    print("updated stress-ng shared mmap fallback")
PY

    python3 - "${src}/core-stressors.h" "${src}/Makefile" <<'PY'
import pathlib
import re
import sys

core_stressors = pathlib.Path(sys.argv[1])
makefile = pathlib.Path(sys.argv[2])

text = core_stressors.read_text()
if "tribe stress-ng minimal stressors" not in text:
    text = re.sub(
        r"#define STRESSORS\(MACRO\)\s+\\\n(?:\s*MACRO\([^)]+\)\s*\\\n)*\s*MACRO\([^)]+\)\n",
        "/* tribe stress-ng minimal stressors: keep SD image executable small. */\n"
        "#define STRESSORS(MACRO)\t\\\n"
        "\tMACRO(cpu)\t\t\\\n"
        "\tMACRO(vm)\n",
        text,
        count=1,
    )
    core_stressors.write_text(text)
    print("patched stress-ng minimal stressor table")

text = makefile.read_text()
if "# tribe stress-ng minimal stressor objects" not in text:
    text = re.sub(
        r"STRESS_SRC = \\\n(?:\tstress-[^\n]+\.c\s*\\\n)+",
        "# tribe stress-ng minimal stressor objects\n"
        "STRESS_SRC = \\\n"
        "\tstress-cpu.c \\\n"
        "\tstress-vm.c \\\n",
        text,
        count=1,
    )
    makefile.write_text(text)
    print("patched stress-ng minimal stressor objects")
PY
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

build_strace()
{
    local src="${BUILD_DIR}/strace"

    clone_or_update "${STRACE_REPO}" "${STRACE_REF}" "${src}"
    patch_strace_riscv32 "${src}"
    if [[ -x "${src}/bootstrap" ]]; then
        (cd "${src}" && ./bootstrap)
    elif [[ ! -x "${src}/configure" && -x "${src}/autogen.sh" ]]; then
        (cd "${src}" && ./autogen.sh)
    elif [[ ! -x "${src}/configure" ]]; then
        echo "strace source has no configure/bootstrap/autogen.sh" >&2
        exit 1
    fi
    (
        cd "${src}"
        make clean >/dev/null 2>&1 || true
        ./configure --host=riscv32-unknown-linux-gnu CC="${CC}" \
            CFLAGS="-march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os" \
            LDFLAGS="-static" \
            --enable-mpers=no
        make -C src -j"${JOBS}" native_printer_decls.h native_printer_defs.h printers.h sys_func.h scno.h sen.h ioctlent0.h bpf_attr_check.c
        make -C src -j"${JOBS}" strace
    )
    install_tool "${src}/src/strace" "${ROOTFS_DIR}/STRACE" 2>/dev/null ||
        install_tool "${src}/strace" "${ROOTFS_DIR}/STRACE"
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

build_clock_test()
{
    local out="${BUILD_DIR}/clock_test"

    if [[ ! -f "${TRIBE_CLOCK_TEST_SRC}" ]]; then
        echo "missing clock test source: ${TRIBE_CLOCK_TEST_SRC}" >&2
        exit 1
    fi
    "${CC}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static \
        -o "${out}" "${TRIBE_CLOCK_TEST_SRC}"
    install_tool "${out}" "${ROOTFS_DIR}/CLOCKTST"
}

build_argvdump()
{
    local src="${BUILD_DIR}/argvdump.c"
    local out="${BUILD_DIR}/argvdump"

    cat > "${src}" <<'EOF'
#include <stdio.h>

int main(int argc, char **argv)
{
    printf("ARGVDUMP argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        printf("argv[%d]=<%s>\n", i, argv[i] ? argv[i] : "(null)");
    }
    return 0;
}
EOF
    "${CC}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static \
        -o "${out}" "${src}"
    install_tool "${out}" "${ROOTFS_DIR}/ARGVDUMP"
}

build_mmap_test()
{
    local src="${BUILD_DIR}/mmap_test.c"
    local out="${BUILD_DIR}/mmap_test"

    cat > "${src}" <<'EOF'
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void say(const char *s)
{
    write(1, s, strlen(s));
}

int main(void)
{
    const size_t size = 4096;
    say("MMAPTEST start\n");
    unsigned char *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        dprintf(1, "MMAPTEST shared-anon failed errno=%d %s\n", errno, strerror(errno));
        return 1;
    }
    dprintf(1, "MMAPTEST mapped %p\n", p);
    p[0] = 0x5a;
    pid_t pid = fork();
    if (pid < 0) {
        dprintf(1, "MMAPTEST fork failed errno=%d %s\n", errno, strerror(errno));
        return 2;
    }
    if (pid == 0) {
        say("MMAPTEST child\n");
        p[1] = p[0] ^ 0xff;
        _exit(0);
    }
    dprintf(1, "MMAPTEST parent pid=%d\n", pid);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        dprintf(1, "MMAPTEST wait failed errno=%d %s\n", errno, strerror(errno));
        return 3;
    }
    dprintf(1, "MMAPTEST status=%d p0=%02x p1=%02x\n", status, p[0], p[1]);
    return (status == 0 && p[0] == 0x5a && p[1] == 0xa5) ? 0 : 4;
}
EOF
    "${CC}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -Os -static \
        -o "${out}" "${src}"
    install_tool "${out}" "${ROOTFS_DIR}/MMAPTEST"
}

write_readme()
{
    cat > "${ROOTFS_DIR}/README.TXT" <<'EOF'
Tribe RISC-V Linux stress SD image

Tools:
  ARGVDUMP
  CLOCKTST
  MMAPTEST
  RUNSTNG
  TCPU1
  TVM1
  TVM5
  STRESSNG
  STRESS
  STRACE
  RUNTRACE
  SDTEST

Example after mounting the card:
  /mnt/CLOCKTST
  /mnt/MMAPTEST
  sh /mnt/TCPU1
  sh /mnt/TVM1
  sh /mnt/TVM5
  sh /mnt/RUNSTNG --cpu 1 --vm 2 --vm-bytes 5m --timeout 1s
  /mnt/STRESS --cpu 1 --io 1 --vm 1 --timeout 10
  /mnt/STRESSNG --cpu 1 --matrix 1 --timeout 10 --metrics-brief
  /mnt/STRACE -f -e futex /mnt/STRESSNG --cpu 1 --matrix 1 --timeout 5
  sh /mnt/RUNTRACE

Raw SD driver test:
  cp /mnt/SDTEST /tmp/SDTEST
  umount /mnt
  /tmp/SDTEST /dev/tribesd1 --passes 1

SDTEST is destructive: it overwrites the target block device with PRBS data.
EOF

cat > "${ROOTFS_DIR}/RUNTRACE" <<'EOF'
#!/bin/sh
echo RUNTRACE_START
/mnt/ARGVDUMP /mnt/STRESSNG --cpu 1 --timeout 1 --verbose
/mnt/STRACE -f -tt /mnt/STRESSNG --cpu 1 --timeout 1 --verbose
rc=$?
echo RUNTRACE_DONE:$rc
EOF

cat > "${ROOTFS_DIR}/RUNSTNG" <<'EOF'
#!/bin/sh
set -e
exec /mnt/STRESSNG "$@"
EOF

cat > "${ROOTFS_DIR}/TCPU1" <<'EOF'
#!/bin/sh
echo TCPU1_START
STRESS_TRIBE_TIME_DEBUG=1 /mnt/STRESSNG --cpu 1 --timeout 1s --temp-path /tmp
rc=$?
echo TCPU1_RC:$rc
EOF

cat > "${ROOTFS_DIR}/TVM1" <<'EOF'
#!/bin/sh
echo TVM1_START
STRESS_TRIBE_TIME_DEBUG=1 /mnt/STRESSNG --cpu 1 --vm 1 --vm-bytes 1m --timeout 1s --temp-path /tmp
rc=$?
echo TVM1_RC:$rc
EOF

cat > "${ROOTFS_DIR}/TVM5" <<'EOF'
#!/bin/sh
echo TVM5_START
STRESS_TRIBE_TIME_DEBUG=1 /mnt/STRESSNG --cpu 1 --vm 2 --vm-bytes 5m --timeout 1s --temp-path /tmp
rc=$?
echo TVM5_RC:$rc
EOF

chmod +x "${ROOTFS_DIR}/RUNTRACE" "${ROOTFS_DIR}/RUNSTNG" \
  "${ROOTFS_DIR}/TCPU1" "${ROOTFS_DIR}/TVM1" "${ROOTFS_DIR}/TVM5"
mkdir -p "${ROOTFS_DIR}/TMP"
}

pack_ext2()
{
    if ! command -v mke2fs >/dev/null 2>&1; then
        echo "missing mke2fs; install e2fsprogs to build ext2 SD image" >&2
        exit 1
    fi

    python3 - "${ROOTFS_DIR}" "${SD_IMG}" "${SD_IMAGE_MB}" "${BUILD_DIR}/sd.ext2.part" <<'PY'
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
image = pathlib.Path(sys.argv[2])
image_mb = int(sys.argv[3], 0)
part = pathlib.Path(sys.argv[4])
sector_size = 512
part_start = 2048
total_sectors = image_mb * 1024 * 1024 // sector_size
if image_mb < 16 or total_sectors <= part_start + 4096:
    raise SystemExit("TRIBE_SD_IMAGE_MB is too small")
part_sectors = total_sectors - part_start
part.parent.mkdir(parents=True, exist_ok=True)
with part.open("wb") as out:
    out.truncate(part_sectors * sector_size)

files = [path for path in sorted(root.iterdir()) if path.is_file()]
print(f"Preparing ext2 partition image {part} ({part_sectors * sector_size // (1024 * 1024)} MiB)")
for path in files:
    print(f"  {path.name}: {path.stat().st_size} bytes")
PY
    mke2fs -F -q -t ext2 -L TRIBE_SD -d "${ROOTFS_DIR}" "${BUILD_DIR}/sd.ext2.part"

    python3 - "${SD_IMG}" "${SD_IMAGE_MB}" "${BUILD_DIR}/sd.ext2.part" <<'PY'
import pathlib
import struct
import sys

image = pathlib.Path(sys.argv[1])
image_mb = int(sys.argv[2], 0)
part = pathlib.Path(sys.argv[3])
sector_size = 512
part_start = 2048
total_sectors = image_mb * 1024 * 1024 // sector_size
part_sectors = total_sectors - part_start

mbr = bytearray(sector_size)
entry = 446
mbr[entry] = 0x00
mbr[entry + 4] = 0x83
struct.pack_into("<I", mbr, entry + 8, part_start)
struct.pack_into("<I", mbr, entry + 12, part_sectors)
mbr[510:512] = b"\x55\xaa"

image.parent.mkdir(parents=True, exist_ok=True)
with image.open("wb") as out:
    out.truncate(total_sectors * sector_size)
    out.seek(0)
    out.write(mbr)
    out.seek(part_start * sector_size)
    with part.open("rb") as inp:
        while True:
            chunk = inp.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)

print(f"Created {image} ({image_mb} MiB, ext2 partition starts at sector {part_start})")
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
        restore_cached_tool "${ROOTFS_DIR}/STRESSNG.BIN" "${BUILD_DIR}/stress-ng/stress-ng" || true
        install_stress_ng_wrapper
    fi
    if [[ "${TRIBE_SD_SKIP_STRESS}" != "1" ]]; then
        build_stress
    else
        restore_cached_tool "${ROOTFS_DIR}/STRESS" "${BUILD_DIR}/stress/src/stress" "${BUILD_DIR}/stress/stress" || true
    fi
    if [[ "${TRIBE_SD_SKIP_STRACE}" != "1" ]]; then
        build_strace
    else
        restore_cached_tool "${ROOTFS_DIR}/STRACE" "${BUILD_DIR}/strace/src/strace" "${BUILD_DIR}/strace/strace" || true
    fi
else
    restore_cached_tool "${ROOTFS_DIR}/STRESSNG.BIN" "${BUILD_DIR}/stress-ng/stress-ng" || true
    install_stress_ng_wrapper
    restore_cached_tool "${ROOTFS_DIR}/STRESS" "${BUILD_DIR}/stress/src/stress" "${BUILD_DIR}/stress/stress" || true
    restore_cached_tool "${ROOTFS_DIR}/STRACE" "${BUILD_DIR}/strace/src/strace" "${BUILD_DIR}/strace/strace" || true
fi

build_sd_test
build_clock_test
build_argvdump
build_mmap_test

write_readme
pack_ext2

echo
echo "SD image ready: ${SD_IMG}"
echo "Run Linux with:"
echo "  TRIBE_LINUX_SD_IMAGE=${SD_IMG} ./run_linux_probe.sh"
