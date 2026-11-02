#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cxx=${1:-g++}
timer=${TIMEOUT:-$(command -v gnutimeout || command -v timeout)}
flags=(-std=c++23 -O1 -pthread -I"$root/include")
"$cxx" "${flags[@]}" "$root/tests/graph/NativeQuietProbesEmit.cc" -o "$work/emit"
for mode in serial plain cached; do
    mkdir "$work/$mode"
    threads=4; memo=1
    [[ $mode != serial ]] || threads=1
    [[ $mode != plain ]] || memo=0
    "$timer" 120 "$work/emit" "$work/$mode" "$threads" "$memo"
    "$timer" 180 "$cxx" "${flags[@]}" -I"$work/$mode" \
        "$root/tests/graph/NativeQuietProbesRun.cc" -o "$work/$mode/run"
    for policy in serial parallel auto; do
        CPPHDL_NATIVE_DISPATCH=$policy CPPHDL_NATIVE_SPIN=64 "$timer" 60 "$work/$mode/run"
    done
done
