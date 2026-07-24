#!/usr/bin/env bash

set -euo pipefail

PRODUCT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIPYARD_ROOT="$PRODUCT_ROOT/chipyard"

if [ ! -f "$PRODUCT_ROOT/.build_chipyard.sh" ]; then
  echo "error: missing $PRODUCT_ROOT/.build_chipyard.sh" >&2
  exit 1
fi
if [ ! -d "$CHIPYARD_ROOT/.git" ]; then
  echo "error: missing Chipyard checkout at $CHIPYARD_ROOT" >&2
  echo "run $PRODUCT_ROOT/.build_chipyard.sh first" >&2
  exit 1
fi
if [ ! -f "$CHIPYARD_ROOT/env.sh" ] || [ ! -x "$CHIPYARD_ROOT/.conda-env/bin/verilator" ]; then
  echo "error: Chipyard environment is not installed" >&2
  echo "run $PRODUCT_ROOT/.build_chipyard.sh first" >&2
  exit 1
fi

# Loading the product build library changes directory to the Chipyard root.
# shellcheck disable=SC1091
source "$PRODUCT_ROOT/.build_chipyard.sh"

require_clean_enough_checkout
apply_local_fixes
source_chipyard_env
configure_host_compilers

step "Generate RocketConfig SystemVerilog"
CHIPYARD_DISABLE_OPTIONAL_MODULES=1 make_with_sbt_retry \
  make -C sims/verilator CONFIG=RocketConfig verilog

step "Build native SystemVerilog/Verilator RocketConfig simulator"
build_simulator RocketConfig

step "Build native RV64 matrix-multiply test"
"$CMAKE_BIN" -S tests -B tests/build -D CMAKE_BUILD_TYPE=Release
"$CMAKE_BIN" --build tests/build \
  --target rocket64-mmul rocket64-mmul-dump -j1

sim="sims/verilator/simulator-chipyard.harness-RocketConfig"
elf="tests/build/rocket64-mmul.riscv"
dump="tests/build/rocket64-mmul.dump"

test -x "$sim"
test -s "$elf"
test -s "$dump"

if ! file "$elf" | grep -q 'ELF 64-bit.*RISC-V'; then
  echo "error: $elf is not a 64-bit RISC-V executable" >&2
  file "$elf" >&2
  exit 1
fi

for instruction in addiw divu mul sll sd ld; do
  if ! grep -Eq "[[:space:]]${instruction}[[:space:]]" "$dump"; then
    echo "error: $instruction was not found in $dump" >&2
    exit 1
  fi
done

step "Native Rocket build ready"
file "$sim"
file "$elf"
printf 'SystemVerilog collateral: %s\n' \
  "sims/verilator/generated-src/chipyard.harness.TestHarness.RocketConfig/gen-collateral"
printf 'Simulator: %s\nTest ELF: %s\n' "$sim" "$elf"
