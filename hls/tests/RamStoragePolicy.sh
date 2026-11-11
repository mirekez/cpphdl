#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$(realpath "$1")
cxx=$2
root=$(realpath "$3")
mkdir -p "$4"
work=$(realpath "$4")
source_file="$work/input.sv"
for header in SynchronousRam MaskedSynchronousRam; do
    awk '/^module cpphdl_examples_/ {emit=1} emit {print} /^endmodule/ {exit}' \
        "$root/lib/memory/$header.h"
done > "$source_file"
cat >> "$source_file" <<'RTL'
module Test(input clk, reset, read_in, write_in,
    input [31:0] read_address_in, write_address_in,
    input [2:0] write_mask_in, input [95:0] data_in,
    output [575:0] result_out);
RTL
bank=0
for depth in 4 64 65; do
    for kind in SynchronousRam MaskedSynchronousRam; do
        mask=''
        if [[ $kind == MaskedSynchronousRam ]]; then mask='.write_mask_in(write_mask_in),'; fi
        cat >> "$source_file" <<RTL
cpphdl_examples_$kind #(.WORDS(3),.DEPTH($depth)) bank$bank(
 .clk(clk),.reset(reset),.read_in(read_in),.write_in(write_in),
 .read_address_in(read_address_in%$depth),.write_address_in(write_address_in%$depth),
 $mask .data_in(data_in),.data_out(result_out[$((bank*96))+:96]));
RTL
        bank=$((bank+1))
    done
done
echo endmodule >> "$source_file"
"$hdlcpp" --native-graph --top Test --output "$work/converted.cc" "$source_file" > "$work/convert.log" 2>&1
"$cxx" -std=c++23 -O1 -pthread -I"$root/include" "$work/converted.cc" -o "$work/emit" > "$work/compile-emit.log" 2>&1
"$work/emit" "$work/model.h" > "$work/emit.log" 2>&1
"$cxx" -std=c++23 -O1 -pthread -I"$root/include" -I"$work" \
    "$root/hls/tests/RamStoragePolicyCheck.cpp" -o "$work/check" > "$work/compile-check.log" 2>&1
"$work/check"
