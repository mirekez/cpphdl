#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
JOBS="${JOBS:-2}"

if [[ ! -f "$OUT/Makefile.optimize" ]]; then
    "$SCRIPT_DIR/convert.sh"
fi

make -C "$OUT" -f Makefile.optimize -j"$JOBS" run_cpphdl_matrix_opt
