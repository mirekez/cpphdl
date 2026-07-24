#!/usr/bin/env bash
set -euo pipefail

product_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
chipyard_root="$product_root/chipyard"
sim="$chipyard_root/cpphdl-build/RocketConfig/runtime/cpphdl-rocket64-optimized-sim"
elf="$chipyard_root/tests/build/rocket64-mmul.riscv"
log=${CPPHDL_TEST_LOG:-"$chipyard_root/cpphdl-build/RocketConfig/rocket64-mmul.log"}
expected='ROCKET RV64 MMUL TEST PASSED:'

[[ -x "$sim" ]] || {
  echo "error: missing optimized C++HDL Rocket simulator: $sim" >&2
  echo "run $product_root/.build_rocket64_cpphdl.sh first" >&2
  exit 1
}
optimized_source="$chipyard_root/cpphdl-build/RocketConfig/generated/TestHarness_optimized_combs.cpp"
[[ -s "$optimized_source" ]] || {
  echo "error: missing optimized comb schedule: $optimized_source" >&2
  exit 1
}
if grep -Eq -- '->_(work|assign)\(|\._(work|assign)\(' \
    "$chipyard_root"/cpphdl-build/RocketConfig/generated/TestHarness_optimized_combs*.cpp; then
  echo "error: optimized sources call _work or _assign" >&2
  exit 1
fi
[[ -s "$elf" ]] || {
  echo "error: missing RV64 matrix executable: $elf" >&2
  echo "run $product_root/.build_rocket64_cpphdl.sh first" >&2
  exit 1
}

mkdir -p "$(dirname "$log")"
wall_timeout=${CPPHDL_TIMEOUT:-3600s}
max_cycles=${CPPHDL_MAX_CYCLES:-100000000}

echo "Running optimized C++HDL Rocket matrix test"
echo "Wall timeout: $wall_timeout; simulation cycle limit: $max_cycles"
set +e
timeout "$wall_timeout" env \
  CPPHDL_MAX_CYCLES="$max_cycles" \
  CPPHDL_PROGRESS_CYCLES="${CPPHDL_PROGRESS_CYCLES:-0}" \
  "$sim" "$elf" </dev/null 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e

if [[ $status -ne 0 ]]; then
  echo "error: C++HDL Rocket simulation exited with status $status" >&2
  exit "$status"
fi
if ! grep -Fq "$expected" "$log"; then
  echo "error: RV64 matrix PASS marker was not found in $log" >&2
  exit 1
fi

echo "C++HDL Rocket RV64 matrix validation PASS"
