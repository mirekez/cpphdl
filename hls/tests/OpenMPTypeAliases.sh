#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
hdlcpp=$2
root=$3
cxx=$4
work=$5
mkdir -p "$work"
work=$(realpath "$work")
case_dir=$(mktemp -d "$work/aliases.XXXXXX")
"$cpphdl" --generated-dir "$case_dir/rtl" "$root/hls/tests/OpenMPTypeAliases.cpp" -- \
    -std=c++17 -DSYNTHESIS -fopenmp -I"$root/include"
workers=("$case_dir/rtl/"*ClockedPipeline*.sv)
[[ ${#workers[@]} == 1 ]]
grep -q 'HLS pipelined_logic: II=1, STAGES=3' "${workers[0]}"
"$hdlcpp" --native-graph --top PipelineTop --output "$case_dir/emit.cpp" "$case_dir/rtl/"*.sv
"$cxx" -std=c++23 -O1 -g0 -pthread -I"$root/include" "$case_dir/emit.cpp" -o "$case_dir/emit"
"$case_dir/emit" "$case_dir/model.h"
"$cxx" -std=c++23 -O1 -g0 -pthread -I"$root/include" -I"$case_dir" \
    "$root/hls/tests/OpenMPTypeAliasesCheck.cpp" -o "$case_dir/check"
"$case_dir/check"
