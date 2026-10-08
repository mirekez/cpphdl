#!/usr/bin/env bash
set -euo pipefail
product_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
exec "$product_root/.run_rocket64_cpphdl.sh" native-graph "$@"
