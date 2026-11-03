#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/build.sh"
initialize
[[ -f "$ROOT/prepared" ]] || die 'run .build_openc910.sh first'
exec 9> "$ROOT/.lock"
flock -n 9 || die 'another OpenC910 build/run owns this work root'
results="$ROOT/comparison.tsv"
printf 'Backend\tBuild seconds\tRun seconds\tCycles\tCommits\tValidation\n' > "$results"
failed=0
reference_ok=0
for mode in verilator verilator-4threads hdlcpp cpphdl-native-graph cpphdl-native-graph-4threads cpphdl-optimize-combs cpphdl; do
    # Separate shells keep errexit effective inside every build and check.
    # FD 9 remains held by this comparison; children do not reacquire it.
    started=$EPOCHREALTIME
    if ! bash "$SCRIPT_DIR/build.sh" compare-build "$mode"; then
        elapsed=$(gawk -v start="$started" -v end="$EPOCHREALTIME" 'BEGIN {printf "%.6f",end-start}')
        printf '%s\t%s\t-\t-\t-\tBUILD FAILED\n' "$mode" "$elapsed" >> "$results"
        failed=1
        continue
    fi
    build_seconds=$(cat "$ROOT/stamps/$mode.time")
    if [[ "$mode" == hdlcpp ]]; then
        printf '%s\t%s\t-\t-\t-\tCONVERTED\n' "$mode" "$build_seconds" >> "$results"
        continue
    fi
    if [[ "$mode" != verilator && "$reference_ok" != 1 ]]; then
        printf '%s\t%s\t-\t-\t-\tNO VALIDATED REFERENCE\n' "$mode" "$build_seconds" >> "$results"
        failed=1
        continue
    fi
    if bash "$SCRIPT_DIR/build.sh" compare-run "$mode"; then
        [[ "$mode" != verilator ]] || reference_ok=1
        gawk -v mode="$mode" -v build="$build_seconds" '
            /^OPENC910_BENCH / {split($2,cycles,"="); split($3,commits,"="); split($4,seconds,"=");
                printf "%s\t%s\t%s\t%s\t%s\tPASS\n",mode,build,seconds[2],cycles[2],commits[2]}
        ' "$ROOT/logs/run-$mode.log" >> "$results"
    else
        printf '%s\t%s\t-\t-\t-\tRUN/VALIDATION FAILED\n' "$mode" "$build_seconds" >> "$results"
        failed=1
    fi
done
column -t -s $'\t' "$results"
exit "$failed"
