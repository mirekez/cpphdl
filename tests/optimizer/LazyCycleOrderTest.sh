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

"$cpphdl" --optimize-combs LazyCycleOrderRoot \
    --generated-dir="$build_dir" \
    "$source_dir/LazyCycleOrderSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

objects=()
for source in "$build_dir"/*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
    -I"$include_dir" "$source_dir/LazyCycleOrderRun.cc" \
    "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
"$build_dir/run"
