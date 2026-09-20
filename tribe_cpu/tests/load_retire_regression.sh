#!/usr/bin/env bash
# Preserve retired load values while younger multicycle operations hold the pipeline.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
compiler="${CXX:-$root/.conda/bin/clang++}"
riscv="${RISCV_HOME:-$HOME/riscv}/bin/riscv32-unknown-elf-gcc"
work="$(mktemp -d "${TMPDIR:-/tmp}/tribe-load-tests.XXXXXX")"
trap 'rm -r "$work"' EXIT
ram_bytes="${TRIBE_TEST_RAM_BYTES:-458752}"
simulator="${TRIBE_TEST_SIMULATOR:-$work/tribe}"
if [[ -z "${TRIBE_TEST_SIMULATOR:-}" ]]; then
  "$compiler" -std=c++2c -O2 -fno-strict-aliasing \
    -DTRIBE_CFG_RV32IA=0 -DTRIBE_CFG_ISR=0 -DTRIBE_CFG_MMU_TLB=0 \
    -DTRIBE_RAM_BYTES_CONFIG="$ram_bytes" \
    -I"$root/include" -I"$root/tribe_cpu/common" \
    -I"$root/tribe_cpu/spec" -I"$root/tribe_cpu/cache" \
    -I"$root/tribe_cpu/devices" -I"$root/examples/axi" \
    "$root/tribe_cpu/main.cpp" -o "$simulator"
fi
"$riscv" -march=rv32im_zicsr -mabi=ilp32 -nostdlib -static \
  -DTEST_UART_BASE="$ram_bytes" -Wl,-Ttext-segment=0 \
  "$root/tribe_cpu/code/load_retire.S" -o "$work/loads.elf"
(cd "$work" && "$simulator" --noveril --program "$work/loads.elf" --elf \
  --start-mem-addr 0 --boot-priv m --cycles 200000 --ram-size "$((ram_bytes / 4))" \
  --expected-output-contains LOAD_RETIRE_PASS --mirror-uart)
echo 'PASS: load retirement during MUL/DIV regression'
