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
# Clang optimizes the multi-megabyte concrete comb partitions several times
# faster than GCC while producing the same C++ ABI. Keep a GCC fallback for
# hosts without the toolchain used by the conversion tests.
DEFAULT_CXX="/home/me/scalepnr/.conda/bin/clang++"
if [[ ! -x "$DEFAULT_CXX" ]]; then
    DEFAULT_CXX="g++"
fi
CPPHDL_CXX="${CPPHDL_CXX:-$DEFAULT_CXX}"
# Constructor-only units build the complete hierarchy but run once before the
# simulation loop. Disable Clang optimization there while all cycle code keeps
# the requested -O2 flags supplied through CXXFLAGS.
if [[ "$(basename "$CPPHDL_CXX")" == clang++* ]]; then
    CPPHDL_CONSTRUCTOR_CXXFLAGS="${CPPHDL_CONSTRUCTOR_CXXFLAGS:--O0 -fno-inline}"
else
    CPPHDL_CONSTRUCTOR_CXXFLAGS="${CPPHDL_CONSTRUCTOR_CXXFLAGS:---param ggc-min-expand=1 --param ggc-min-heapsize=4096}"
fi
DEFAULT_CXXFLAGS="-std=c++23 -O2 -g0 -w -fno-asynchronous-unwind-tables -I/home/me/cpphdl/include -I$OUT"
if [[ "$(basename "$CPPHDL_CXX")" == "g++" ]]; then
    DEFAULT_CXXFLAGS+=" -fno-var-tracking -fno-var-tracking-assignments --param ggc-min-expand=5 --param ggc-min-heapsize=32768"
fi
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
build_signature="$CPPHDL_CXX $CPPHDL_CXXFLAGS"
if [[ ! -f "$flags_stamp" ]] || [[ "$(cat "$flags_stamp")" != "$build_signature" ]]; then
    rm -rf "$OUT/build/opt" "$OUT/$RUNNER"
    printf '%s' "$build_signature" > "$flags_stamp"
fi

make -C "$OUT" -f Makefile.optimize -j"$JOBS" \
    CXX="$CPPHDL_CXX" CXXFLAGS="$CPPHDL_CXXFLAGS" \
    CONSTRUCTOR_CXXFLAGS="$CPPHDL_CONSTRUCTOR_CXXFLAGS" \
    LDFLAGS="$CPPHDL_LDFLAGS" "$RUNNER"
