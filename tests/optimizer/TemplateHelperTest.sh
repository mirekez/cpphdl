#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
cxx=$4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for mode in --optimize-combs --optimize-combs-l1; do
    output="$work/${mode#--}"
    "$cpphdl" "$mode" TemplateHelperRoot --generated-dir="$output" \
        "$source_dir/TemplateHelperRoot.h" -- -std=c++23 -w -I"$include_dir"
    for optimization in -O0 -O2; do
        "$cxx" -std=c++23 "$optimization" -g0 -w -I"$output" -I"$source_dir" -I"$include_dir" \
            "$source_dir/TemplateHelperRun.cc" "$output"/*.cpp -o "$work/run"
        "$work/run"
    done
done
