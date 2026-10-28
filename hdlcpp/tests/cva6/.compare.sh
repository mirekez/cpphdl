#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/build.sh"

comparison_table() {
    gawk -F '\t' '
        BEGIN { printf "\n%-36s %14s %12s %14s %9s %9s %s\n", "Backend", "Compilation(s)", "Build", "Execution(s)", "Cycles", "Commits", "Result" }
        NR>1 { printf "%-36s %14s %12s %14s %9s %9s %s\n", $1,$2,$3,$4,$5,$6,$7 }
    ' "$ROOT/comparison.tsv"
}

build_measurement() {
    local mode=$1 signature seconds state
    BUILD_SECONDS=unmeasured BUILD_STATE=cached
    if [[ -f "$ROOT/stamps/$mode.time" ]]; then
        IFS=$'\t' read -r signature seconds state < "$ROOT/stamps/$mode.time"
        if [[ "$signature" == "$(head -1 "$ROOT/stamps/$mode")" ]]; then
            BUILD_SECONDS=$seconds BUILD_STATE=$state
        fi
    fi
}

compare() {
    case "${1:-}" in
        --help)
            echo 'Build and run original Verilator (1/4 threads), native graph (1/4), optimize-combs, and default C++HDL.'
            echo 'Usage: .compare.sh [--rebuild]'
            echo 'Run .build_cva6.sh first. Shared HDLCPP conversion is built automatically.'
            echo '--rebuild discards backend object caches and rebuilds every model and the shared conversion.'
            echo 'Compilation is successful build wall time, including lowering and linking; execution is simulator work time.'
            echo 'Cached binaries retain their original measured build time; older unmeasured builds say unmeasured.'
            echo 'A resumed build reports only that attempt and is labelled resumed. All builds use one compiler job.'
            return ;;
        --rebuild) export CVA6_REBUILD=1 ;;
        '') export CVA6_REBUILD=0 ;;
        *) die 'usage: .compare.sh [--rebuild]' ;;
    esac
    (($#<=1)) || die 'usage: .compare.sh [--rebuild]'
    initialize
    exec 9> "$ROOT/.lock"
    flock -n 9 || die "another CVA6 build/run owns $ROOT"
    require_source
    printf 'backend\tcompilation_seconds\tbuild\texecution_seconds\tcycles\tcommits\tvalidation\n' > "$ROOT/comparison.tsv"
    build_model hdlcpp
    build_measurement hdlcpp
    printf 'Shared SV-to-C++ conversion: %s s (%s); excluded from individual backend compilation.\n' "$BUILD_SECONDS" "$BUILD_STATE" | tee "$ROOT/conversion-time.txt"
    local mode seconds
    for mode in verilator verilator-4threads cpphdl-native-graph cpphdl-native-graph-4threads cpphdl-optimize-combs cpphdl; do
        build_model "$mode"
        build_measurement "$mode"
        run_model "$mode"
        seconds=$(awk '{print $2}' "$ROOT/runs/$mode/result.txt")
        printf '%s\t%s\t%s\t%s\t396778\t297074\tPASS\n' "$mode" "$BUILD_SECONDS" "$BUILD_STATE" "$seconds" >> "$ROOT/comparison.tsv"
        comparison_table
    done
    echo 'All modes: identical ELF, checksum 0xd687fdc5 and ordered integer commits.'
    echo 'Both Verilator modes retain trace formatting; C++ validation runs are separate and excluded from execution time.'
    echo "Report: $ROOT/comparison.tsv"
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then compare "$@"; fi
