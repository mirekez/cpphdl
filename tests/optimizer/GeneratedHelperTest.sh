#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
cxx=$4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for optimizer in --optimize-combs --optimize-combs-l1; do
  for mode in direct collection; do
    output="$work/${optimizer#--}/$mode"
    options=()
    input="$source_dir/GeneratedHelperRoot.h"
    if [[ "$mode" == collection ]]; then
        "$cpphdl" "$optimizer" GeneratedHelperRoot \
            --optimize-combs-collect="$work/helpers.collection" \
            "$source_dir/GeneratedHelperRoot.h" -- -std=c++23 -w -I"$include_dir"
        python3 - "$work/helpers.collection" "$work/legacy.collection" <<'PY'
import gzip
import sys
with gzip.open(sys.argv[1], 'rb') as source:
    content = source.read()
assert b'cpphdl-combs-collection-v6' in content
with gzip.open(sys.argv[2], 'wb') as output:
    output.write(content.replace(b'cpphdl-combs-collection-v6', b'cpphdl-combs-collection-v5', 1))
PY
        if "$cpphdl" "$optimizer" GeneratedHelperRoot \
            --optimize-combs-load="$work/legacy.collection" --generated-dir="$work/rejected" \
            "$source_dir/GeneratedHelperSeed.cc" -- -std=c++23 -w -I"$include_dir" > "$work/rejected.log" 2>&1; then
            echo 'old runtime-layout collection accepted' >&2
            exit 1
        fi
        grep -q 'recollect with the current runtime layout' "$work/rejected.log"
        options+=(--optimize-combs-load="$work/helpers.collection")
        input="$source_dir/GeneratedHelperSeed.cc"
    fi
    "$cpphdl" "$optimizer" GeneratedHelperRoot "${options[@]}" --generated-dir="$output" \
        "$input" -- -std=c++23 -w -I"$include_dir"
    if grep -Eq '\.__hdlcpp_expr_[0-9]+\(' "$output"/*.cpp; then
        echo 'generated helper still hides producer reads' >&2
        exit 1
    fi
    for optimization in -O0 -O2; do
        "$cxx" -std=c++23 "$optimization" -g0 -w -I"$output" -I"$source_dir" -I"$include_dir" \
            "$source_dir/GeneratedHelperRun.cc" "$output"/*.cpp -o "$work/run"
        "$work/run"
    done
  done
done
