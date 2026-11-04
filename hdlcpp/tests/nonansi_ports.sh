#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
hdlcpp=$(realpath "$1")
include_dir=$(realpath "$2")
cxx=$(command -v "$3")
cxx=$(cd "$(dirname "$cxx")" && pwd)/$(basename "$cxx")
verilator=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d /tmp/hdlcpp-nonansi.XXXXXX)
trap 'rm -r -- "$work"' EXIT
cd "$work"
HDLCPP_WRITE_PORT_TYPES="$work/ports.tsv" HDLCPP_WRITE_MODULE_PARAMS="$work/params.tsv" \
    "$hdlcpp" "$source_dir/nonansi_ports.sv"
grep -Fqx $'legacy_ram.$port.0\tq' ports.tsv
grep -Fqx $'legacy_ram.$port.4\twe' ports.tsv
grep '^legacy_ram' params.tsv | grep -q 'WIDTH.*ADDR.*DEPTH'
if grep '^legacy_ram' params.tsv | grep -q 'LAST'; then exit 1; fi
"$cxx" -std=c++23 -O0 -g0 -I"$include_dir" -I. "$source_dir/nonansi_ports.cpp" -o run
./run > converted.log
if [[ -n "$verilator" ]]; then
    "$verilator" --cc --exe --build -j 1 --top-module nonansi_ports -Wno-fatal \
        -CFLAGS '-DUSE_VERILATOR' --Mdir "$work/reference" \
        "$source_dir/nonansi_ports.sv" "$source_dir/nonansi_ports.cpp" > build.log 2>&1
    "$work/reference/Vnonansi_ports" > reference.log
    cmp converted.log reference.log
fi
echo 'Non-ANSI ports, parameterized RAM and explicit combinational sensitivity PASS'
