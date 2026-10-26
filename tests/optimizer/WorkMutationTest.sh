#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
stdcxxexp_library="${4:-}"
link_options=()
[[ -n "$stdcxxexp_library" ]] && link_options+=("$stdcxxexp_library")
build_root="$(mktemp -d)"
trap 'rm -rf "$build_root"' EXIT
for mode in --optimize-combs --optimize-combs-l1; do
  for collection in direct serialized; do
    build_dir="$build_root/${mode#--}/$collection"
    mkdir -p "$build_dir"
    load_options=()
    if [[ "$collection" == serialized ]]; then
        metadata="$build_dir/collection.bin"
        "$cpphdl" "$mode" WorkMutationRoot --optimize-combs-collect="$metadata" \
            "$source_dir/WorkMutationSeed.cc" -- -w -I"$source_dir" -I"$include_dir"
        load_options+=(--optimize-combs-load="$metadata")
    fi
"$cpphdl" "$mode" WorkMutationRoot "${load_options[@]}" \
    --generated-dir="$build_dir" \
    "$source_dir/WorkMutationSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Root classes can be declared in headers whose filenames differ from the class.
# The generated implementation must use Clang's declaration location, otherwise a
# stale same-named specialization header can redefine the concrete optimizer root.
grep -q '#include ".*WorkMutation.h"' "$build_dir/WorkMutationRoot_optimized_combs_internal.h"
! grep -q '#include ".*WorkMutationRoot.h"' "$build_dir/WorkMutationRoot_optimized_combs_internal.h"

for optimization in -O0 -O2; do
objects=()
for source in "$build_dir"/*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 "$optimization" -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 "$optimization" -g0 -w -I"$build_dir" -I"$source_dir" \
    -I"$include_dir" "$source_dir/WorkMutationRun.cc" \
    "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
"$build_dir/run"
done
  done
done
