#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
ELF="${ELF:-$SCRIPT_DIR/matrix_multiply.riscv}"
TOHOST="${TOHOST:-0x80001000}"
MAX_CYCLES="${MAX_CYCLES:-2000}"

if [[ ! -x "$OUT/run_cpphdl_matrix_opt" ]]; then
    "$SCRIPT_DIR/build.sh"
fi

"$OUT/run_cpphdl_matrix_opt" "$ELF" "$TOHOST" "$MAX_CYCLES"
