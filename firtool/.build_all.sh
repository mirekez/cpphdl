#!/usr/bin/env bash
# Prepared checkouts only; every entry point enforces revisions.json.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
run_tests=0
for argument in "$@"; do
  case "$argument" in
    --run-tests) run_tests=1 ;;
    -h|--help)
      printf '%s\n' \
        'Usage: .build_all.sh [--run-tests]' \
        'Validate pinned Chipyard, apply integration/exporter patches, prepare' \
        'the pinned CIRCT SDK and firtool, then build SystemVerilog/Verilator,' \
        'plain C++, optimized combs, and native graph in that order.' \
        '--run-tests also runs each backend RV64 matrix smoke test.'
      exit 0 ;;
    *) echo "error: unknown argument: $argument" >&2; exit 2 ;;
  esac
done

# Sourcing resolves the shared checkout/link but does not start a build.
source "$script_dir/.build_chipyard.sh"
step "1/6 Validate pinned Chipyard and all initialized dependencies"
require_clean_enough_checkout

step "2/6 Apply integration/exporter patches, prepare pinned SDK, build firtool"
"$script_dir/.chipyard_cpphdl_patch.sh"

step "3/6 Build SystemVerilog/Verilator"
"$script_dir/.build_rocket64_verilator.sh"

step "4/6 Build plain C++HDL"
"$script_dir/.build_rocket64_cpphdl.sh"

step "5/6 Build optimized-comb C++HDL"
"$script_dir/.build_rocket64_cpphdl-optimize-combs.sh"

step "6/6 Build native-graph C++HDL"
"$script_dir/.build_rocket64_cpphdl-native-graph.sh"

if [[ "$run_tests" == 1 ]]; then
  step "Run RV64 matrix tests for all four backends"
  "$script_dir/.run_rocket64_verilator.sh"
  "$script_dir/.run_rocket64_cpphdl.sh" plain
  "$script_dir/.run_rocket64_cpphdl-optimize-combs.sh"
  "$script_dir/.run_rocket64_cpphdl-native-graph.sh"
fi
step "All backends built"
