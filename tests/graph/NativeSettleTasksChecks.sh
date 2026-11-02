#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cxx=${1:-g++}
timer=${TIMEOUT:-}
if [[ -z $timer ]]; then timer=$(command -v gnutimeout || command -v timeout); fi
flags=(-std=c++23 -O1 -pthread -I"$root/include")
"$cxx" "${flags[@]}" "$root/tests/graph/NativeSettleTasksEmit.cc" -o "$work/emit"
for variant in plain cached split; do
    memo=1; split=0
    if [[ $variant == plain ]]; then memo=0; fi
    if [[ $variant == split ]]; then split=1; fi
    mkdir "$work/$variant"
    "$work/emit" "$work/$variant/model.h" 0 "$memo" "$split"
    parts=("$work/$variant"/model.part.*.cpp)
    [[ -f ${parts[0]} ]] || parts=()
    "$cxx" "${flags[@]}" -DEXPECT_FILTERED -I"$work/$variant" \
        "$root/tests/graph/NativeSettleTasksRun.cc" "${parts[@]}" -o "$work/$variant/run"
    for cap in 1 4 6; do
        for policy in serial parallel auto; do
            CPPHDL_NATIVE_SPIN=64 CPPHDL_NATIVE_TASK_WORKERS=$cap CPPHDL_NATIVE_DISPATCH=$policy \
                "$timer" 60 "$work/$variant/run"
        done
    done
done
