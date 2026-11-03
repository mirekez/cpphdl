#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
hdlcpp=$(realpath "$1")
include_dir=$(realpath "$2")
cxx=$(command -v "$3")
cxx=$(cd "$(dirname "$cxx")" && pwd)/$(basename "$cxx")
verilator=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d /tmp/hdlcpp-legacy-ports.XXXXXX)
trap 'rm -r -- "$work"' EXIT
cd "$work"
# The test intentionally has no external driver on the GPIO pads.
export HDLCPP_OUTPUT_ONLY_INOUTS=output_only_pad.pad
HDLCPP_METADATA_ONLY=1 HDLCPP_WRITE_PORT_TYPES="$work/ports" \
    HDLCPP_WRITE_MODULE_PARAMS="$work/params" "$hdlcpp" "$source_dir/legacy_ports.sv"
export HDLCPP_PORT_TYPES="$work/ports" HDLCPP_MODULE_PARAMS="$work/params"
"$hdlcpp" "$source_dir/legacy_ports.sv"
"$cxx" -std=c++23 -O0 -g0 -I"$include_dir" -I. "$source_dir/legacy_ports.cpp" -o run
./run > converted.log
if [[ -n "$verilator" ]]; then
    "$verilator" --cc --exe --build -j 1 --top-module legacy_ports -Wno-fatal \
        -CFLAGS '-DUSE_VERILATOR' --Mdir "$work/reference" \
        "$source_dir/legacy_ports.sv" "$source_dir/legacy_ports.cpp" > build.log 2>&1
    "$work/reference/Vlegacy_ports" > reference.log
    cmp converted.log reference.log
fi
echo 'Legacy port names, module order and output-only pads PASS'
