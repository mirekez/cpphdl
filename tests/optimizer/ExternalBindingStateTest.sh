#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
for lanes in 1 2; do
  generated="$build_dir/$lanes"
  "$cpphdl" --optimize-combs ExternalBindingStateRoot --optimize-threads="$lanes" \
    --generated-dir="$generated" "$source_dir/ExternalBindingStateSeed.cc" -- \
    -std=c++17 -w -I"$include_dir" -I"$source_dir"
  for optimization in -O0 -O2; do
    g++ -std=c++17 "$optimization" -g0 -w -pthread \
      -I"$generated" -I"$include_dir" -I"$source_dir" \
      "$source_dir/ExternalBindingStateRun.cc" "$generated"/*.cpp -o "$generated/run"
    "$generated/run"
  done
done
