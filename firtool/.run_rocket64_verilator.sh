#!/usr/bin/env bash

set -euo pipefail

PRODUCT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIPYARD_ROOT="$PRODUCT_ROOT/chipyard"
SIM="$CHIPYARD_ROOT/sims/verilator/simulator-chipyard.harness-RocketConfig"
ELF="$CHIPYARD_ROOT/tests/build/rocket64-mmul.riscv"
OUTPUT_DIR="$CHIPYARD_ROOT/sims/verilator/output/chipyard.harness.TestHarness.RocketConfig"
LOG="$OUTPUT_DIR/rocket64-mmul.log"

if [ ! -x "$SIM" ]; then
  echo "error: missing Rocket simulator: $SIM" >&2
  echo "run $PRODUCT_ROOT/.build_rocket64_verilator.sh first" >&2
  exit 1
fi
if [ ! -s "$ELF" ]; then
  echo "error: missing RV64 test executable: $ELF" >&2
  echo "run $PRODUCT_ROOT/.build_rocket64_verilator.sh first" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$PRODUCT_ROOT/.build_chipyard.sh"
require_clean_enough_checkout
source_chipyard_env

mkdir -p "$OUTPUT_DIR"
export ROOT HOST_BUILD_SYSROOT RISCV
export -f run_host_simulator

wall_timeout="${RV64_TIMEOUT:-600s}"
max_cycles="${RV64_MAX_CYCLES:-100000000}"

step "Run SystemVerilog/Verilator Rocket RV64 matrix test"
printf 'Wall timeout: %s; simulation cycle limit: %s\n' \
  "$wall_timeout" "$max_cycles"

set +e
timeout "$wall_timeout" bash -c 'run_host_simulator "$@"' _ \
  "$SIM" \
  +permissive \
  +max-cycles="$max_cycles" \
  +permissive-off \
  "$ELF" \
  </dev/null 2>&1 | tee "$LOG"
sim_status="${PIPESTATUS[0]}"
set -e

if [ "$sim_status" -ne 0 ]; then
  echo "error: Rocket simulation exited with status $sim_status" >&2
  tail -40 "$LOG" >&2
  exit "$sim_status"
fi

if ! grep -q '^ROCKET RV64 MMUL TEST PASSED:' "$LOG"; then
  echo "error: Verilator Rocket simulation did not report the RV64 PASS signature" >&2
  tail -40 "$LOG" >&2
  exit 1
fi

step "SystemVerilog/Verilator Rocket test PASS"
grep -E '^RV64 matrix|^ROCKET RV64 MMUL TEST PASSED:' "$LOG"
printf 'Log: %s\n' "$LOG"
