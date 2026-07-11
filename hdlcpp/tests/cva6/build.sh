#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
JOBS="${JOBS:-1}"
CPPHDL_CXXFLAGS="${CPPHDL_CXXFLAGS:--std=c++23 -O0 -g0 -w -pipe -fno-asynchronous-unwind-tables -I/home/me/cpphdl/include -I$OUT}"

if [[ ! -f "$OUT/Makefile.optimize" ]]; then
    "$SCRIPT_DIR/convert.sh"
fi

mkdir -p "$OUT/build"
flags_stamp="$OUT/build/cxxflags.optimize"
if [[ ! -f "$flags_stamp" ]] || [[ "$(cat "$flags_stamp")" != "$CPPHDL_CXXFLAGS" ]]; then
    rm -rf "$OUT/build/opt" "$OUT/run_cpphdl_matrix_opt"
    printf '%s' "$CPPHDL_CXXFLAGS" > "$flags_stamp"
fi

make -C "$OUT" -f Makefile.optimize -j"$JOBS" CXXFLAGS="$CPPHDL_CXXFLAGS" run_cpphdl_matrix_opt
