#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$hdlcpp" --native-graph --top native_signed_casts --output "$work/emit.cpp" "$source_dir/native_signed_casts.sv"
"$cxx" -std=c++23 -O1 -g0 -I"$include_dir" "$work/emit.cpp" -o "$work/emit"
"$work/emit" "$work/model.h"
"$cxx" -std=c++23 -O2 -g0 -I"$include_dir" -I"$work" "$source_dir/native_signed_casts.cpp" -o "$work/test"
"$work/test"
