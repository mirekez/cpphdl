#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs StructuralNttpRoot \
    --generated-dir="$build_dir" \
    "$source_dir/StructuralNttpSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Structural non-type arguments must be deduced from the concrete module type.
# Clang's printed aggregate value is not guaranteed to be valid source, so it
# must not be copied into an extracted comb expression or template argument.
if rg -q 'StructuralNttpConfig\{' \
    "$build_dir"/StructuralNttpRoot_optimized_combs*.cpp; then
    printf 'structural template aggregate leaked into optimized output\n' >&2
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
    -I"$include_dir" "$source_dir/StructuralNttpRun.cc" \
    "${objects[@]}" -lstdc++exp -o "$build_dir/run"
"$build_dir/run"
