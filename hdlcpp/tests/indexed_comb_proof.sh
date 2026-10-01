#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
cpphdl=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
HDLCPP_NOCACHE_COMB_METHODS=copied_o_comb,partial_o_comb,cyclic_comb,prefix_comb,dynamic_o_comb \
    "$hdlcpp" "$source_dir/indexed_comb_proof.sv"
! grep -q '__cpphdl_complete_dynamic_o_comb' generated/indexed_comb_proof.h
! grep -q '__cpphdl_complete_cyclic_comb' generated/indexed_comb_proof.h
for optimization in -O0 -O2; do
    "$cxx" -std=c++23 "$optimization" -g0 -fno-access-control -I"$include_dir" -I. \
        "$source_dir/indexed_comb_proof.cpp" -o run
    ./run
done
if [[ -n "$cpphdl" ]]; then
    printf '#pragma once\n#include "generated/indexed_comb_proof.h"\nusing ProofModel = indexed_comb_proof<8>;\n' > ProofModel.h
    printf '#include "ProofModel.h"\nProofModel root;\n' > seed.cpp
    for mode in --optimize-combs --optimize-combs-l1; do
        output="$work/${mode#--}"
        CPPHDL_TRACE_COMB_PROOF=1 "$cpphdl" "$mode" ProofModel --generated-dir="$output" \
            "$work/seed.cpp" -- -std=c++23 -w -I"$include_dir" -I"$work" > optimize.log 2>&1
        grep -q 'copied_o_comb_func complete=1 memoizable=1' optimize.log
        for optimization in -O0 -O2; do
            "$cxx" -std=c++23 "$optimization" -g0 -fno-access-control -DCPPHDL_TEST_OPTIMIZED \
                -I"$include_dir" -I. -I"$output" "$source_dir/indexed_comb_proof.cpp" \
                "$output"/*.cpp -o run
            ./run
        done
    done
fi
