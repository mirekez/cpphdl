#!/usr/bin/env bash
set -euo pipefail
product_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
exec "$product_root/.build_rocket64_cpphdl-common.sh" optimize-combs "$@"
