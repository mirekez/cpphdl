#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
stdcxxexp_library="${4:-}"
link_options=()
[[ -n "$stdcxxexp_library" ]] && link_options+=("$stdcxxexp_library")
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs-l1 OpaqueSubtreeRoot \
    --generated-dir="$build_dir" \
    "$source_dir/OpaqueSubtreeCollect.cc" \
    "$source_dir/OpaqueSubtreeSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

grep -q 'n1\._work(__cpphdl_reset)' \
    "$build_dir"/OpaqueSubtreeRoot_optimized_combs_work_*.cpp
grep -q 'n1\._strobe()' \
    "$build_dir"/OpaqueSubtreeRoot_optimized_combs_strobe_*.cpp
grep -q 'n1\.output()' \
    "$build_dir"/OpaqueSubtreeRoot_optimized_combs*.cpp
if rg -q 'n1\._assign\(' "$build_dir"/OpaqueSubtreeRoot_optimized_combs*.cpp; then
    printf 'opaque _assign call was emitted into optimized runtime code\n' >&2
    exit 1
fi

objects=()
for source in "$build_dir"/*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
    -I"$include_dir" "$source_dir/OpaqueSubtreeRun.cc" \
    "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
"$build_dir/run"
