#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs WorkMutationRoot \
    --generated-dir="$build_dir" \
    "$source_dir/WorkMutationSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Root classes can be declared in headers whose filenames differ from the class.
# The generated implementation must use Clang's declaration location, otherwise a
# stale same-named specialization header can redefine the concrete optimizer root.
grep -q '#include ".*WorkMutation.h"' "$build_dir/WorkMutationRoot_optimized_combs_internal.h"
! grep -q '#include ".*WorkMutationRoot.h"' "$build_dir/WorkMutationRoot_optimized_combs_internal.h"

objects=()
for source in "$build_dir"/*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
    -I"$include_dir" "$source_dir/WorkMutationRun.cc" \
    "${objects[@]}" -lstdc++exp -o "$build_dir/run"
"$build_dir/run"
