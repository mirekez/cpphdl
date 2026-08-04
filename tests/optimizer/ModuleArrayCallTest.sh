#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs ModuleArrayCallRoot \
    --generated-dir="$build_dir" \
    "$source_dir/ModuleArrayCallCollect.cc" \
    "$source_dir/ModuleArrayCallSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# A dynamic child index must dispatch over the elaborated graph instances.
# No generated evaluator or eager chunk may retain a legacy child-array port
# call, which could re-enter stale function_ref caches outside graph ordering.
if rg -q 'children\s*\[[^]]+\]\.output\s*\(' \
    "$build_dir"/ModuleArrayCallRoot_optimized_combs*.cpp; then
    printf 'legacy child-array port call remained in optimized output\n' >&2
    exit 1
fi

# A cast-wrapped constexpr index is one concrete graph edge. It must not emit
# a selector against every elaborated child and falsely couple their graphs.
if [[ "$(rg -o ' == [01]\) return' \
        "$build_dir"/ModuleArrayCallRoot_optimized_combs*.cpp | wc -l)" -ne 2 ]]; then
    printf 'constant module-array index emitted a runtime dispatcher\n' >&2
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
    -I"$include_dir" "$source_dir/ModuleArrayCallRun.cc" \
    "${objects[@]}" -lstdc++exp -o "$build_dir/run"
"$build_dir/run"
