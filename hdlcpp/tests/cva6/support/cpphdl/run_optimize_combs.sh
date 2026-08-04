#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-.}"
CPPHDL="${CPPHDL:-/home/me/cpphdl/build/cpphdl}"
CLANGXX="${CPPHDL_CLANGXX:-/home/me/scalepnr/.conda/bin/clang++}"
MODE="${CPPHDL_COMB_OPTIMIZER_MODE:-full}"
# The complete CVA6 graph contains enough dynamic nodes that tiny partitions
# spend most build time reparsing common headers. One thousand keeps GCC memory
# bounded while reducing the number of optimized translation units substantially.
CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK="${CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK:-1000}"
export CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK
if [[ -n "${CPPHDL_COMB_WORK_DIR:-}" ]]; then
    WORK_DIR="$CPPHDL_COMB_WORK_DIR"
    mkdir -p "$WORK_DIR"
    OWN_WORK_DIR=0
else
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cpphdl-cva6-combs.XXXXXX")"
    OWN_WORK_DIR=1
fi
PCH="$WORK_DIR/context.pch"

case "$MODE" in
    full) optimizer_flag="--optimize-combs" ;;
    l1) optimizer_flag="--optimize-combs-l1" ;;
    *) echo "invalid CPPHDL_COMB_OPTIMIZER_MODE: $MODE" >&2; exit 2 ;;
esac

if [[ ! -x "$CPPHDL" ]]; then
    echo "missing cpphdl executable: $CPPHDL" >&2
    exit 2
fi
if [[ ! -x "$CLANGXX" ]]; then
    echo "missing Clang compiler for comb optimizer PCH: $CLANGXX" >&2
    exit 2
fi

cleanup_intermediates() {
    if [[ "${CPPHDL_KEEP_COMB_INTERMEDIATES:-0}" != "1" ]]; then
        find "$WORK_DIR" -mindepth 1 -delete
        if [[ "$OWN_WORK_DIR" == "1" ]]; then
            rmdir "$WORK_DIR"
        fi
    fi
}
trap cleanup_intermediates EXIT

# The context header includes the CppHDL runtime, so checking only the wrapper
# can retain an ABI-stale PCH after a runtime update. Rebuild when any public
# CppHDL header is newer than the cached compilation context.
runtime_header_newer=0
if [[ -f "$PCH" ]] && find /home/me/cpphdl/include -type f -newer "$PCH" -print -quit | grep -q .; then
    runtime_header_newer=1
fi
# The umbrella timestamp does not change when one of its generated includes is
# regenerated. Include those dependencies in the freshness test so Clang never
# consumes an AST whose declarations disagree with the current model headers.
generated_header_newer=0
if [[ -f "$PCH" ]] && find "$OUT/generated" -type f -name '*.h' -newer "$PCH" -print -quit | grep -q .; then
    generated_header_newer=1
fi
if [[ ! -f "$PCH" || "$OUT/cpphdl_comb_optimization_context.h" -nt "$PCH" || \
      "$runtime_header_newer" == "1" || "$generated_header_newer" == "1" ]]; then
    "$CLANGXX" -std=c++26 -DSYNTHESIS -w \
        -I/home/me/cpphdl/include -I"$OUT" -x c++-header \
        "$OUT/cpphdl_comb_optimization_context.h" -o "$PCH"
fi

mapfile -t sources < "$OUT/cpphdl_comb_collection_sources.txt"
if [[ "${#sources[@]}" -eq 0 ]]; then
    echo "comb optimizer collection manifest is empty" >&2
    exit 2
fi

collections=()
for index in "${!sources[@]}"; do
    source="$OUT/${sources[index]}"
    collection="$WORK_DIR/collection_$(printf '%03d' "$index").bin"
    collections+=("$collection")
    # Collection metadata changes when either its source, PCH, or optimizer
    # executable changes. Otherwise retain the bounded AST result so graph-only
    # optimizer iterations do not repeat the expensive Clang collection pass.
    reuse_graph_collection=0
    if [[ "${CPPHDL_REUSE_COMB_COLLECTIONS:-0}" == "1" ]]; then
        reuse_graph_collection=1
    fi
    if [[ -f "$collection" && "$collection" -nt "$source" && \
          "$collection" -nt "$PCH" && \
          ( "$reuse_graph_collection" == "1" || "$collection" -nt "$CPPHDL" ) ]]; then
        continue
    fi
    "$CPPHDL" "$optimizer_flag" cpphdl_opt_t0 \
        --optimize-combs-collect="$collection" \
        "$source" -- -std=c++26 -w -include-pch "$PCH" \
        -I/home/me/cpphdl/include -I"$OUT"
done

load_args=()
for collection in "${collections[@]}"; do
    load_args+=(--optimize-combs-load="$collection")
done
seed="$OUT/${sources[${#sources[@]}-1]}"
"$CPPHDL" "$optimizer_flag" cpphdl_opt_t0 \
    "${load_args[@]}" --generated-dir "$OUT" "$seed" -- \
    -std=c++26 -w -include-pch "$PCH" \
    -I/home/me/cpphdl/include -I"$OUT"
