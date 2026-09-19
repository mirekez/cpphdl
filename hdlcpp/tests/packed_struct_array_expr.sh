#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
cpphdl=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$hdlcpp" "$source_dir/packed_struct_array_expr.sv"
for optimization in -O0 -O2; do
    "$cxx" -std=c++23 "$optimization" -g0 -I"$include_dir" -I. \
        "$source_dir/packed_struct_array_expr.cpp" -o run
    ./run
done
if [[ -n "$cpphdl" ]]; then
    printf '#include "generated/packed_struct_array_expr.h"\npacked_struct_array_expr root;\n' > seed.cpp
    for mode in --optimize-combs --optimize-combs-l1; do
        output="$work/${mode#--}"
        "$cpphdl" "$mode" packed_struct_array_expr --generated-dir="$output" \
            "$work/seed.cpp" -- -std=c++23 -I"$include_dir" -I"$work"
        for optimization in -O0 -O2; do
            "$cxx" -std=c++23 "$optimization" -g0 -DCPPHDL_TEST_OPTIMIZED \
                -I"$include_dir" -I. -I"$output" "$source_dir/packed_struct_array_expr.cpp" \
                "$output"/*.cpp -o run
            ./run
        done
    done
fi
