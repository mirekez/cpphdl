#!/usr/bin/env bash
set -euo pipefail

product_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
chipyard_root="$product_root/chipyard"
cpphdl_include=${CPPHDL_INCLUDE_DIR:-"$(cd "$product_root/.." && pwd)/include"}
cpphdl_tool=${CPPHDL_TOOL:-"$(cd "$product_root/.." && pwd)/build/cpphdl"}

[[ -f "$cpphdl_include/cpphdl.h" ]] || {
  echo "error: cpphdl.h not found in $cpphdl_include" >&2
  exit 1
}
[[ -x "$cpphdl_tool" ]] || {
  echo "error: cpphdl optimizer not found at $cpphdl_tool" >&2
  exit 1
}

CPPHDL_FIRTOOL_JOBS="${CPPHDL_FIRTOOL_JOBS:-1}" \
CPPHDL_OPTIMIZE_COMBS=1 \
CPPHDL_TOOL="$cpphdl_tool" \
  "$product_root/.chipyard_cpphdl_patch.sh"

CPPHDL_INCLUDE_DIR="$cpphdl_include" \
CPPHDL_BUILD_JOBS="${CPPHDL_BUILD_JOBS:-2}" \
CPPHDL_FIRTOOL_JOBS="${CPPHDL_FIRTOOL_JOBS:-1}" \
CPPHDL_OPT_LEVEL="${CPPHDL_OPT_LEVEL:--O2}" \
CPPHDL_OPTIMIZE_COMBS=1 \
CPPHDL_REOPTIMIZE_COMBS="${CPPHDL_REOPTIMIZE_COMBS:-1}" \
CPPHDL_TOOL="$cpphdl_tool" \
  "$chipyard_root/scripts/build-cpphdl-rocket64.sh"

sim="$chipyard_root/cpphdl-build/RocketConfig/runtime/cpphdl-rocket64-optimized-sim"
elf="$chipyard_root/tests/build/rocket64-mmul.riscv"
test -x "$sim"
test -s "$elf"

echo "Optimized C++HDL Rocket build ready"
echo "Simulator: $sim"
echo "Matrix ELF: $elf"
