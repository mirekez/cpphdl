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

    if search_q 'n1\.decoded_(op|result)_comb\s*=' "$build_dir"/*.cpp; then
        printf '%s retained a duplicate projected comb evaluator\n' "$mode" >&2
        exit 1
    fi
    search_q 'n1\.decoded_comb\s*=' "$build_dir"/*.cpp || {
        printf '%s did not schedule the aggregate comb evaluator\n' "$mode" >&2
        exit 1
    }
    view_eval=$(awk '
        /^void .*_optimized_comb_eval_[0-9]+\(/ {
            id=$0
            sub(/^.*_optimized_comb_eval_/, "", id)
            sub(/[^0-9].*$/, "", id)
        }
        /n1\.view_comb\s*=/ { print id; exit }
    ' "$build_dir"/*_optimized_combs_dynamic_*.cpp)
    [[ -n "$view_eval" ]] || {
        printf '%s did not emit the repeated pure comb evaluator\n' "$mode" >&2
        exit 1
    }
    search_q "evaluated${view_eval} = _system_clock" "$build_dir"/*.cpp || {
        printf '%s did not memoize the repeated pure comb evaluator\n' "$mode" >&2
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
