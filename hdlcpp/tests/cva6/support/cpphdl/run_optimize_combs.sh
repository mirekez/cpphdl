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
USE_PCH="${CPPHDL_COMB_USE_PCH:-1}"
CPPHDL_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
if [[ ! -f "$CPPHDL_REPO/include/cpphdl.h" ]]; then
    CPPHDL_REPO="${CPPHDL_SOURCE_ROOT:-/home/me/cpphdl}"
fi

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

# The context header includes the C++HDL runtime, so checking only the wrapper
# can retain an ABI-stale PCH after a runtime update. Rebuild when any public
# C++HDL header is newer than the cached compilation context.
runtime_header_newer=0
if [[ -f "$PCH" ]] && find "$CPPHDL_REPO/include" -type f -newer "$PCH" -print -quit | grep -q .; then
    runtime_header_newer=1
fi
# The umbrella timestamp does not change when one of its generated includes is
# regenerated. Include those dependencies in the freshness test so Clang never
# consumes an AST whose declarations disagree with the current model headers.
generated_header_newer=0
if [[ -f "$PCH" ]] && find "$OUT/generated" -type f -name '*.h' -newer "$PCH" -print -quit | grep -q .; then
    generated_header_newer=1
fi
if [[ "$USE_PCH" == "1" ]] && [[ ! -f "$PCH" || "$OUT/cpphdl_comb_optimization_context.h" -nt "$PCH" || \
      "$runtime_header_newer" == "1" || "$generated_header_newer" == "1" ]]; then
    "$CLANGXX" -std=c++26 -DSYNTHESIS -w \
        -I"$CPPHDL_REPO/include" -I"$OUT" -x c++-header \
        "$OUT/cpphdl_comb_optimization_context.h" -o "$PCH"
fi
PCH_FLAGS=()
if [[ "$USE_PCH" == "1" ]]; then
    PCH_FLAGS=(-include-pch "$PCH")
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
    if [[ -n "${CPPHDL_COMB_COLLECTION_KEY:-}" ]]; then
        partial_collections="$(python3 - "$CPPHDL" "$optimizer_flag" "$source" "$collection" \
            "$CPPHDL_REPO/include" "$OUT" <<'PY'
import hashlib
from pathlib import Path
import re
import resource
import subprocess
import sys

tool, optimizer, source, target, include, model = sys.argv[1:]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def collect(source, target):
    checksum = Path(str(target) + '.sha256')
    if target.is_file() and checksum.is_file() and checksum.read_text().strip() == digest(target):
        print(f'verified comb collection cache: {source.name}', file=sys.stderr, flush=True)
        return [target]
    text = source.read_text()
    markers = list(re.finditer(r'^cpphdl_opt_t\d+\* cpphdlCombs(?:Collect|Opaque)\w* = nullptr;[ \t]*$', text, re.MULTILINE))
    partition = Path(str(target) + '.split')
    can_split = (len(markers) > 1 and not text[markers[-1].end():].strip()
                 and all(not text[first.end():second.start()].strip()
                         for first, second in zip(markers, markers[1:])))
    def halves():
        midpoint = len(markers) // 2
        results = []
        for index, group in enumerate((markers[:midpoint], markers[midpoint:])):
            child = target.with_name(target.stem + '-' + str(index))
            child_source = child.with_suffix('.cpp')
            child_source.write_text(text[:markers[0].start()] + '\n'.join(marker.group() for marker in group) + '\n')
            results.extend(collect(child_source, child.with_suffix('.bin')))
        return results
    if can_split and partition.is_file():
        return halves()
    temporary = Path(str(target) + '.tmp')
    def limit():
        maximum = 4_000_000_000 if len(markers) > 1 else 6_000_000_000
        inherited = resource.getrlimit(resource.RLIMIT_AS)[0]
        if inherited != resource.RLIM_INFINITY:
            maximum = min(maximum, inherited)
        resource.setrlimit(resource.RLIMIT_AS, (maximum, maximum))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([tool, optimizer, 'cpphdl_opt_t0', '--optimize-combs-collect=' + str(temporary),
        str(source), '--', '-std=c++26', '-w', '-I' + include, '-I' + model],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, preexec_fn=limit)
    if result.returncode:
        temporary.unlink(missing_ok=True)
        if can_split and b'out of memory' in result.stdout.lower():
            print(f'splitting comb collection: {source.name} ({len(markers)} aliases)', file=sys.stderr, flush=True)
            partition.touch()
            return halves()
        sys.stderr.buffer.write(result.stdout)
        raise RuntimeError(f'comb collection failed ({result.returncode}): {source}')
    sys.stderr.buffer.write(result.stdout)
    temporary.replace(target)
    temporary_checksum = Path(str(checksum) + '.tmp')
    temporary_checksum.write_text(digest(target) + '\n')
    temporary_checksum.replace(checksum)
    return [target]

for path in collect(Path(source), Path(target)):
    print(path)
PY
)"
        while IFS= read -r partial; do
            collections+=("$partial")
        done <<< "$partial_collections"
        continue
    fi
    collections+=("$collection")
    # Collection metadata changes when either its source, PCH, or optimizer
    # executable changes. Otherwise retain the bounded AST result so graph-only
    # optimizer iterations do not repeat the expensive Clang collection pass.
    reuse_graph_collection=0
    if [[ "${CPPHDL_REUSE_COMB_COLLECTIONS:-0}" == "1" ]]; then
        reuse_graph_collection=1
    fi
    if [[ -z "${CPPHDL_COMB_COLLECTION_KEY:-}" && -f "$collection" && "$collection" -nt "$source" && \
          "$collection" -nt "$PCH" && \
          ( "$reuse_graph_collection" == "1" || "$collection" -nt "$CPPHDL" ) ]]; then
        continue
    fi
    "$CPPHDL" "$optimizer_flag" cpphdl_opt_t0 \
        --optimize-combs-collect="$collection.tmp" \
        "$source" -- -std=c++26 -w "${PCH_FLAGS[@]}" \
        -I"$CPPHDL_REPO/include" -I"$OUT"
    mv "$collection.tmp" "$collection"
    actual_checksum="$(sha256sum "$collection")"
    printf '%s\n' "${actual_checksum%% *}" > "$collection.sha256"
done

load_args=()
for collection in "${collections[@]}"; do
    load_args+=(--optimize-combs-load="$collection")
done
seed="$OUT/${sources[${#sources[@]}-1]}"
"$CPPHDL" "$optimizer_flag" cpphdl_opt_t0 \
    "${load_args[@]}" --generated-dir "$OUT" "$seed" -- \
    -std=c++26 -w "${PCH_FLAGS[@]}" \
    -I"$CPPHDL_REPO/include" -I"$OUT"
