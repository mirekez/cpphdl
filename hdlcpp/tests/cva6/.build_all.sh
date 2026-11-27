#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

for argument; do
    case "$argument" in
        --help)
            echo 'Usage: .build_all.sh [--all] [--rebuild]'
            echo 'Prepare CVA6, then build and run the Verilator and native graph comparison.'
            echo 'Pass --all or --rebuild through to .compare.sh.'
            exit 0 ;;
        --all|--rebuild) ;;
        *) echo 'Usage: .build_all.sh [--all] [--rebuild]' >&2; exit 2 ;;
    esac
done

CVA6_REBUILD=0 "$SCRIPT_DIR/.build_cva6.sh"
exec "$SCRIPT_DIR/.compare.sh" "$@"
