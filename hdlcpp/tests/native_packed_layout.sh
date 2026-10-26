#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$hdlcpp" "$source_dir/native_packed_layout.sv"
for standard in c++17 c++20 c++23; do
    "$cxx" -std="$standard" -O2 -g0 -I"$include_dir" \
        "$source_dir/native_packed_compat.cpp" -o compat
    ./compat
done
for mode in default compatibility; do
    flags=()
    if [[ "$mode" == compatibility ]]; then flags+=(-DCPPHDL_NATIVE_PACKED); fi
    for optimization in -O0 -O2; do
        "$cxx" -std=c++23 "$optimization" -g0 "${flags[@]}" \
            -I"$include_dir" -I. "$source_dir/native_packed_layout.cpp" -o run
        ./run
    done
    if [[ ${CPPHDL_TEST_SANITIZE:-0} == 1 ]]; then
        "$cxx" -std=c++23 -O1 -g0 "${flags[@]}" -fsanitize=address,undefined \
            -fno-omit-frame-pointer -I"$include_dir" -I. \
            "$source_dir/native_packed_layout.cpp" -o run-sanitized
        ./run-sanitized
    fi
done
