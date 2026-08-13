#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPPHDL_CVA6_NATIVE_HARNESS="${CPPHDL_CVA6_NATIVE_HARNESS:-0}"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl_testharness}"
    RUNNER="run_cpphdl_testharness_opt"
    MAX_CYCLES="${MAX_CYCLES:-500000}"
else
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
    RUNNER="run_cpphdl_matrix_opt"
    MAX_CYCLES="${MAX_CYCLES:-20000}"
fi
SRC="${MATRIX_SRC:-$SCRIPT_DIR/matrix_multiply.cpp}"
ELF="${ELF:-$SCRIPT_DIR/matrix_multiply.riscv}"
TOHOST="${TOHOST:-0x80001000}"
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

if [[ ! -f "$ELF" || "$SRC" -nt "$ELF" || "$SCRIPT_DIR/matrix_runtime.c" -nt "$ELF" ]]; then
    build_elf
fi

if [[ ! -x "$OUT/$RUNNER" || "$OUT/cpphdl_optimized_main.cpp" -nt "$OUT/$RUNNER" ]]; then
    CPPHDL_CVA6_NATIVE_HARNESS="$CPPHDL_CVA6_NATIVE_HARNESS" "$SCRIPT_DIR/build.sh"
fi

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    runner_args=("$ELF" "$MAX_CYCLES")
else
    runner_args=("$ELF" "$TOHOST" "$MAX_CYCLES")
fi

run_log="$OUT/build/run.log"
mkdir -p "$(dirname "$run_log")"
set +e
"$OUT/$RUNNER" "${runner_args[@]}" 2>&1 | tee "$run_log"
runner_status=${PIPESTATUS[0]}
set -e
output="$(cat "$run_log")"
if [[ "$runner_status" -ne 0 ]]; then
    exit "$runner_status"
fi

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    if ! grep -Eq '^PASSED\r?$' <<<"$output"; then
        printf 'ERROR: missing UART success output: PASSED\n' >&2
        exit 1
    fi
    if ! grep -Fq "*** SUCCESS ***" <<<"$output"; then
        printf 'ERROR: missing exact-harness success line\n' >&2
        exit 1
    fi
else
    if ! grep -Fq "UART: PASSED" <<<"$output"; then
        printf 'ERROR: missing UART success output: PASSED\n' >&2
        exit 1
    fi
    if ! grep -Fq "cpphdl PASS" <<<"$output"; then
        printf 'ERROR: missing cpphdl PASS line\n' >&2
        exit 1
    fi
fi
