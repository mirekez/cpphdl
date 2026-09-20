#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
compiler="${CXX:-$root/.conda/bin/clang++}"
# Compile the real CPU harness with the optional blocks absent. This catches
# unconditional references to registers declared only inside feature guards.
"$compiler" -std=c++2c -fsyntax-only \
  -DTRIBE_CFG_RV32IA=0 -DTRIBE_CFG_ISR=0 -DTRIBE_CFG_MMU_TLB=0 \
  -I"$root/include" -I"$root/tribe_cpu/common" \
  -I"$root/tribe_cpu/spec" -I"$root/tribe_cpu/cache" \
  -I"$root/tribe_cpu/devices" -I"$root/examples/axi" \
  "$root/tribe_cpu/main.cpp"
echo 'PASS: Tribe compiles without MMU, ISR, and atomics'
