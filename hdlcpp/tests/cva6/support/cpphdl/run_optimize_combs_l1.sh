#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-.}"
CPPHDL="${CPPHDL:-/home/me/cpphdl/build/cpphdl}"
CLANGXX="${CPPHDL_CLANGXX:-/home/me/scalepnr/.conda/bin/clang++}"
PCH="$OUT/cpphdl_comb_optimization_context.pch"

if [[ ! -x "$CPPHDL" ]]; then
    echo "missing cpphdl executable: $CPPHDL" >&2
    exit 2
fi
if [[ ! -x "$CLANGXX" ]]; then
    echo "missing Clang compiler for comb optimizer PCH: $CLANGXX" >&2
    exit 2
fi

cleanup_pch() {
    if [[ "${CPPHDL_KEEP_COMB_PCH:-0}" != "1" ]]; then
        find "$OUT" -maxdepth 1 -type f \
            -name 'cpphdl_comb_optimization_context.pch' -delete
    fi
}
trap cleanup_pch EXIT

if [[ ! -f "$PCH" || "$OUT/cpphdl_comb_optimization_context.h" -nt "$PCH" ]]; then
    "$CLANGXX" -std=c++26 -DSYNTHESIS -w \
        -I/home/me/cpphdl/include -I"$OUT" -x c++-header \
        "$OUT/cpphdl_comb_optimization_context.h" -o "$PCH"
fi

mapfile -t sources < "$OUT/cpphdl_comb_collection_sources.txt"
for index in "${!sources[@]}"; do
    sources[index]="$OUT/${sources[index]}"
done

"$CPPHDL" --optimize-combs-l1 cpphdl_opt_t0 \
    --generated-dir "$OUT" "${sources[@]}" -- \
    -std=c++26 -w -include-pch "$PCH" \
    -I/home/me/cpphdl/include -I"$OUT"
