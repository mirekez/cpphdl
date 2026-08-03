#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs DynamicCombRepeatRoot \
    --generated-dir="$build_dir" \
    "$source_dir/DynamicCombRepeatSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Frequently reused ordinary combs must remain repeatable. They stay as hidden
# source-partition functions; duplicating definitions in the common header made
# every generated translation unit optimize the same body without a speedup.
if rg -q '\[\[gnu::always_inline\]\] inline void .*_optimized_comb_eval_' \
    "$build_dir/DynamicCombRepeatRoot_optimized_combs_internal.h"; then
    printf 'repeatable evaluator body was duplicated in the common header\n' >&2
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
    -I"$include_dir" "$source_dir/DynamicCombRepeatRun.cc" \
    "${objects[@]}" -lstdc++exp -o "$build_dir/run"
"$build_dir/run"
