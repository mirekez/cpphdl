#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
verilator=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$hdlcpp" "$source_dir/expression_helpers.sv"
if grep -Fq '[&]' generated/expression_helpers.h; then
    echo 'unexpected helper lambda in generated C++' >&2
    exit 1
fi
grep -q 'if constexpr' generated/expression_helpers.h
if grep -q 'sv_bits_runtime' generated/expression_helpers.h; then
    echo 'obsolete runtime slice helper in generated C++' >&2
    exit 1
fi
for optimization in -O0 -O2; do
    "$cxx" -std=c++23 "$optimization" -g0 -I"$include_dir" -I. \
        "$source_dir/expression_helpers.cpp" -o run
    ./run
done
if [[ -n "$verilator" ]]; then
    for mode in 1 2; do
        "$verilator" --cc --exe --top-module expression_helpers -Wno-fatal \
            -GMode="$mode" -CFLAGS "-DUSE_VERILATOR -DTEST_MODE=$mode" \
            --Mdir "$work/reference-$mode" "$source_dir/expression_helpers.sv" \
            "$source_dir/expression_helpers.cpp"
        make -C "$work/reference-$mode" -f Vexpression_helpers.mk -j1 CXX="$cxx" LINK="$cxx"
        "$work/reference-$mode/Vexpression_helpers"
    done
fi
