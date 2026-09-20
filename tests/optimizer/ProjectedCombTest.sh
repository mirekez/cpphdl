#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/Search.sh"

cpphdl=$1
include_dir=$2
source_dir=$3
stdcxxexp_library=${4:-}
link_options=()
[[ -n "$stdcxxexp_library" ]] && link_options+=("$stdcxxexp_library")
build_root=$(mktemp -d)
trap 'rm -rf "$build_root"' EXIT

for mode in --optimize-combs --optimize-combs-l1; do
    build_dir="$build_root/${mode#--}"
    mkdir -p "$build_dir"
    "$cpphdl" "$mode" ProjectedCombRoot \
        --generated-dir="$build_dir" \
        "$source_dir/ProjectedCombSeed.cc" -- \
        -w -I"$source_dir" -I"$include_dir"

    if search_q 'n1\.decoded_comb\s*=' "$build_dir"/*.cpp; then
        printf '%s widened a projected read to the whole aggregate\n' "$mode" >&2
        exit 1
    fi
    search_q 'n1\.decoded_op_comb\s*=' "$build_dir"/*.cpp || {
        printf '%s did not retain the field comb evaluator\n' "$mode" >&2
        exit 1
    }
    # A repeated pure comb is one graph value. Once root inputs are recognized
    # as external leaves, emit that value once in the eager schedule instead of
    # retaining a clock-guarded dynamic evaluator for it.
    view_assignments=$(grep -Eh '^[[:space:]]+n1\.view_comb = 0;' \
        "$build_dir"/*.cpp | wc -l)
    [[ "$view_assignments" -eq 1 ]] || {
        printf '%s emitted the repeated pure comb %s times\n' \
            "$mode" "$view_assignments" >&2
        exit 1
    }

    objects=()
    for source in "$build_dir"/*.cpp; do
        object="${source%.cpp}.o"
        g++ -std=c++23 -O2 -g0 -w -I"$build_dir" -I"$source_dir" \
            -I"$include_dir" -c "$source" -o "$object"
        objects+=("$object")
    done
    g++ -std=c++23 -O2 -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" "$source_dir/ProjectedCombRun.cc" \
        "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
    "$build_dir/run"
done
