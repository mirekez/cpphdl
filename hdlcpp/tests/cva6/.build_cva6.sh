#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 -B "$SCRIPT_DIR/cva6_workflow.py" prepare "$@"
