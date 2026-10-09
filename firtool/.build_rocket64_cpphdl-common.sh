#!/usr/bin/env bash
# Shared implementation; public entry points select one backend explicitly.
set -euo pipefail
product_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
chipyard_root=$(cd "$product_root/chipyard" && pwd -P)
cpphdl_include=${CPPHDL_INCLUDE_DIR:-"$(cd "$product_root/.." && pwd)/include"}
cpphdl_tool=${CPPHDL_TOOL:-"$(cd "$product_root/.." && pwd)/build/cpphdl"}
if [[ -z "${CPPHDL_GRAPH_CXX:-}" && -x "$product_root/../.conda/bin/clang++" ]]; then
  export CPPHDL_GRAPH_CXX="$product_root/../.conda/bin/clang++"
fi
backend=${1:?backend required}
shift
[[ -f "$cpphdl_include/cpphdl.h" ]] || {
  echo "error: cpphdl.h not found in $cpphdl_include" >&2; exit 1;
}
case "$backend" in
  plain) default_opt=-O2 ;;
  optimize-combs|native-graph)
    default_opt=-O2
    [[ -x "$cpphdl_tool" ]] || {
      echo "error: cpphdl tool not found at $cpphdl_tool" >&2; exit 1;
    } ;;
  *) echo "error: unknown backend: $backend" >&2; exit 1 ;;
esac
if [[ "$backend" == native-graph ]]; then
  native_threads=${CPPHDL_OPTIMIZE_THREADS:-1}
  if [[ ! "$native_threads" =~ ^[1-9][0-9]{0,2}$ ]] || (( native_threads > 256 )); then
    echo 'error: native-graph CPPHDL_OPTIMIZE_THREADS must be between 1 and 256' >&2
    exit 1
  fi
  export CPPHDL_OPTIMIZE_THREADS="$native_threads"
fi
CPPHDL_FIRTOOL_JOBS="${CPPHDL_FIRTOOL_JOBS:-1}" \
  "$product_root/.chipyard_cpphdl_patch.sh"
CPPHDL_BACKEND="$backend" \
CPPHDL_INCLUDE_DIR="$cpphdl_include" \
CPPHDL_BUILD_JOBS="${CPPHDL_BUILD_JOBS:-1}" \
CPPHDL_FIRTOOL_JOBS="${CPPHDL_FIRTOOL_JOBS:-1}" \
CPPHDL_OPT_LEVEL="${CPPHDL_OPT_LEVEL:-$default_opt}" \
CPPHDL_USE_OPTIMIZED_PCH="${CPPHDL_USE_OPTIMIZED_PCH:-OFF}" \
CPPHDL_TOOL="$cpphdl_tool" \
  "$chipyard_root/scripts/build-cpphdl-rocket64.sh" "$@"
