#!/usr/bin/env bash
set -euo pipefail
hdlcpp=$1
include_dir=$2
cxx=$3
verilator=${4:-}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$hdlcpp" "$source_dir/constant_widths.sv"
python3 - <<'PY'
from pathlib import Path
header = Path('generated/constant_widths.h').read_text()
assert 'using strb_t = logic<STRB_WIDTH>;' in header
assert 'using id_t = logic<ID_WIDTH>;' in header
initializer = next(line for line in header.splitlines() if 'STRB_WIDTH = ' in line)
assert '(uint64_t)' not in initializer and '&' not in initializer, initializer
assert 'DATA_WIDTH' in initializer and '8ull' in initializer, initializer
assert header.index('STRB_WIDTH = ') < header.index('using strb_t')
PY
for optimization in -O0 -O2; do
    "$cxx" -std=c++23 "$optimization" -g0 -I"$include_dir" -I. \
        "$source_dir/constant_widths.cpp" -o run
    ./run
done
if [[ -n "$verilator" ]]; then
    "$verilator" --cc --exe --top-module constant_widths_reference -Wno-fatal \
        --Mdir "$work/reference" "$source_dir/constant_widths.sv" \
        "$source_dir/constant_widths_reference.sv" "$source_dir/constant_widths_reference.cpp"
    make -C "$work/reference" -f Vconstant_widths_reference.mk -j1 CXX="$cxx" LINK="$cxx"
    "$work/reference/Vconstant_widths_reference"
fi
