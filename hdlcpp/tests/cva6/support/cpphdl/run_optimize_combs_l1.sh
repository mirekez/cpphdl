#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPPHDL_COMB_OPTIMIZER_MODE=l1 exec "$SCRIPT_DIR/run_optimize_combs.sh" "$@"
