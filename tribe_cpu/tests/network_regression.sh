#!/usr/bin/env bash
# Native Ethernet regressions, including the interrupt-driven bare-metal CPU test.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
compiler="${CXX:-$root/.conda/bin/clang++}"
work="$(mktemp -d "${TMPDIR:-/tmp}/tribe-network-tests.XXXXXX")"
trap 'rm -r "$work"' EXIT
flags=(-std=c++2c -O2 -fno-strict-aliasing
  -DTRIBE_CFG_RV32IA=1 -DTRIBE_CFG_ISR=1 -DTRIBE_CFG_MMU_TLB=1
  -I"$root/include" -I"$root/tribe_cpu/common" -I"$root/tribe_cpu/spec"
  -I"$root/tribe_cpu/cache" -I"$root/tribe_cpu/devices"
  -I"$root/tribe_cpu/verif" -I"$root/examples/axi")
for name in EthGigDMA EthGigMac EthGigCPU; do
  "$compiler" "${flags[@]}" "$root/tribe_cpu/tests/${name}_test.cpp" -o "$work/$name"
  (cd "$work" && "./$name" --noveril)
done
"$compiler" "${flags[@]}" -UTRIBE_CFG_RV32IA -UTRIBE_CFG_ISR -UTRIBE_CFG_MMU_TLB \
  -DTRIBE_CFG_RV32IA=0 -DTRIBE_CFG_ISR=0 -DTRIBE_CFG_MMU_TLB=0 \
  "$root/tribe_cpu/tests/EthGigCPU_test.cpp" -o "$work/EthGigPollingCPU"
(cd "$work" && ./EthGigPollingCPU --noveril --polling)
CXX="$compiler" bash "$root/tribe_cpu/tests/no_mmu_compile_check.sh"
