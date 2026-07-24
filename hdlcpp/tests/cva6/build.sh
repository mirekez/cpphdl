#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPPHDL_CVA6_NATIVE_HARNESS="${CPPHDL_CVA6_NATIVE_HARNESS:-0}"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl_testharness}"
    RUNNER="run_cpphdl_testharness_opt"
else
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
    RUNNER="run_cpphdl_matrix_opt"
fi
JOBS="${JOBS:-1}"
# The specialized model is intended to run optimized; the conservative GCC
# memory settings keep the generated translation units buildable on modest hosts.
# CPPHDL_CXXFLAGS remains available for temporary diagnostic builds.
DEFAULT_CXXFLAGS="-std=c++23 -O2 -g0 -w -fno-asynchronous-unwind-tables -fno-var-tracking -fno-var-tracking-assignments --param ggc-min-expand=5 --param ggc-min-heapsize=32768 -I/home/me/cpphdl/include -I$OUT"
# The converted native harness contains a very large statically allocated SRAM.
# x86-64's small model cannot link references beyond 2 GiB, while medium keeps
# code addressing compact and permits this workload's large data section.
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    SPIKE_DIR="$SCRIPT_DIR/cva6/tools/spike"
    DEFAULT_CXXFLAGS+=" -mcmodel=medium -I$SPIKE_DIR/include"
fi
CPPHDL_CXXFLAGS="${CPPHDL_CXXFLAGS:-$DEFAULT_CXXFLAGS}"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    DEFAULT_LDFLAGS="-L$SPIKE_DIR/lib -Wl,-rpath,$SPIKE_DIR/lib -lfesvr -lriscv -ldisasm -lyaml-cpp -pthread -latomic -lstdc++exp"
else
    DEFAULT_LDFLAGS="-lstdc++exp"
fi
CPPHDL_LDFLAGS="${CPPHDL_LDFLAGS:-$DEFAULT_LDFLAGS}"

if [[ ! -f "$OUT/Makefile.optimize" ]]; then
    CPPHDL_CVA6_NATIVE_HARNESS="$CPPHDL_CVA6_NATIVE_HARNESS" CPPHDL_OUT="$OUT" "$SCRIPT_DIR/convert.sh"
fi

mkdir -p "$OUT/build"
flags_stamp="$OUT/build/cxxflags.optimize"
if [[ ! -f "$flags_stamp" ]] || [[ "$(cat "$flags_stamp")" != "$CPPHDL_CXXFLAGS" ]]; then
    rm -rf "$OUT/build/opt" "$OUT/$RUNNER"
    printf '%s' "$CPPHDL_CXXFLAGS" > "$flags_stamp"
fi

make -C "$OUT" -f Makefile.optimize -j"$JOBS" \
    CXXFLAGS="$CPPHDL_CXXFLAGS" LDFLAGS="$CPPHDL_LDFLAGS" "$RUNNER"
