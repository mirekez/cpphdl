#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
SRC="${MATRIX_SRC:-$SCRIPT_DIR/matrix_multiply.cpp}"
ELF="${ELF:-$SCRIPT_DIR/matrix_multiply.riscv}"
TOHOST="${TOHOST:-0x80001000}"
MAX_CYCLES="${MAX_CYCLES:-20000}"
RISCV="${RISCV:-/home/me/riscv}"
TARGET_ROOT="${CVA6_SRC:-$SCRIPT_DIR/cva6}"
ISA="${ISA:-rv32imac_zicsr_zifencei_zbkb_zbkx_zkne_zknd_zknh}"
MABI="${MABI:-ilp32}"
GCC_OPTS=(-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles -g)

build_elf() {
    local obj_dir="$SCRIPT_DIR/build/matrix_obj"
    local cc="${RISCV_CC:-$RISCV/bin/riscv32-unknown-elf-gcc}"
    local cxx="${RISCV_CXX:-$RISCV/bin/riscv32-unknown-elf-g++}"
    local linker="${LINKER:-$TARGET_ROOT/config/gen_from_riscv_config/linker/link.ld}"
    local common="$TARGET_ROOT/verif/tests/custom/common"
    local env="$TARGET_ROOT/verif/tests/custom/env"
    local user_ext="$TARGET_ROOT/verif/sim/dv/user_extension"
    local includes=(-I "$common" -I "$env" -I "$user_ext")

    mkdir -p "$obj_dir" "$(dirname "$ELF")"
    "$cxx" "$SRC" "${includes[@]}" "${GCC_OPTS[@]}" -march="$ISA" -mabi="$MABI" -c -o "$obj_dir/matrix_multiply.o"
    "$cc" "$SCRIPT_DIR/matrix_runtime.c" "${includes[@]}" "${GCC_OPTS[@]}" -march="$ISA" -mabi="$MABI" -c -o "$obj_dir/matrix_runtime.o"
    "$cc" "$common/crt.S" "${includes[@]}" "${GCC_OPTS[@]}" -march="$ISA" -mabi="$MABI" -c -o "$obj_dir/crt.o"
    "$cxx" -T "$linker" "${GCC_OPTS[@]}" \
        "$obj_dir/matrix_multiply.o" \
        "$obj_dir/matrix_runtime.o" \
        "$obj_dir/crt.o" \
        -lgcc -march="$ISA" -mabi="$MABI" -o "$ELF"
}

if [[ ! -f "$ELF" || "$SRC" -nt "$ELF" || "$SCRIPT_DIR/matrix_runtime.c" -nt "$ELF" || "$SCRIPT_DIR/run.sh" -nt "$ELF" ]]; then
    build_elf
fi

if [[ ! -x "$OUT/run_cpphdl_matrix_opt" || "$OUT/cpphdl_optimized_main.cpp" -nt "$OUT/run_cpphdl_matrix_opt" ]]; then
    "$SCRIPT_DIR/build.sh"
fi

if ! output="$("$OUT/run_cpphdl_matrix_opt" "$ELF" "$TOHOST" "$MAX_CYCLES" 2>&1)"; then
    printf '%s\n' "$output"
    exit 1
fi

printf '%s\n' "$output"
if ! grep -Fq "UART: PASSED" <<<"$output"; then
    printf 'ERROR: missing UART success output: PASSED\n' >&2
    exit 1
fi
if ! grep -Fq "cpphdl PASS" <<<"$output"; then
    printf 'ERROR: missing cpphdl PASS line\n' >&2
    exit 1
fi
