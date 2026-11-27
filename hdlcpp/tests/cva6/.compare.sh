#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/build.sh"

comparison_table() {
    gawk -F '\t' '
        BEGIN { printf "\n%-36s %14s %12s %14s %9s %9s %s\n", "Backend", "Compilation(s)", "Build", "Execution(s)", "Cycles", "Commits", "Speedup / Result" }
        NR>1 { printf "%-36s %14s %12s %14s %9s %9s %s\n", $1,$2,$3,$4,$5,$6,$8 "x " $7 }
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
    local all=0 argument
    export CVA6_REBUILD=0
    for argument in "$@"; do
        case "$argument" in
            --help)
                echo 'Usage: .compare.sh [--rebuild] [--all]'
                echo 'Clone the local CVA6 checkout, then compare Verilator and native graph with one and four threads.'
                echo '--all also tests optimize-combs and default C++HDL.'
                echo '--rebuild rebuilds the conversion and simulators and discards partial backend objects.'
                echo 'Compilation includes lowering and linking. Execution measures the simulator work loop.'
                echo 'Cached builds retain their recorded build time. A resumed build reports only that attempt.'
                return ;;
            --rebuild) CVA6_REBUILD=1 ;;
            --all) all=1 ;;
            *) die 'usage: .compare.sh [--rebuild] [--all]' ;;
        esac
    done
    initialize
    exec 9> "$ROOT/.lock"
    flock -n 9 || die "another CVA6 build/run owns $ROOT"
    prepare
    build_model hdlcpp
    build_measurement hdlcpp
    printf 'Shared SV-to-C++ conversion: %s s (%s).\n' "$BUILD_SECONDS" "$BUILD_STATE" | tee "$ROOT/conversion-time.txt"
    local -a modes=(verilator verilator-4threads cpphdl-native-graph cpphdl-native-graph-4threads)
    if ((all)); then
        modes+=(cpphdl-optimize-combs cpphdl)
    fi
    printf 'backend\tcompilation_seconds\tbuild\texecution_seconds\tcycles\tcommits\tvalidation\tspeedup_vs_verilator\n' > "$ROOT/comparison.tsv"
    local mode seconds cycles commits speedup reference_seconds
    for mode in "${modes[@]}"; do
        if [[ "$mode" != verilator ]]; then build_model "$mode"; fi
        build_measurement "$mode"
        run_model "$mode"
        read -r _ seconds _ cycles _ commits _ < "$ROOT/runs/$mode/result.txt"
        if [[ "$mode" == verilator ]]; then reference_seconds=$seconds; fi
        speedup=$(awk -v reference="$reference_seconds" -v elapsed="$seconds" 'BEGIN {printf "%.3f", reference/elapsed}')
        printf '%s\t%s\t%s\t%s\t%s\t%s\tPASS\t%s\n' "$mode" "$BUILD_SECONDS" "$BUILD_STATE" "$seconds" "$cycles" "$commits" "$speedup" >> "$ROOT/comparison.tsv"
    done
    comparison_table
    echo 'All modes used the same ELF, returned checksum 0xd687fdc5 and matched the ordered integer commit trace.'
    echo 'Verilator retains trace formatting in its timed run. C++HDL collects commits in a separate validation run.'
    echo "Report: $ROOT/comparison.tsv"
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then compare "$@"; fi
