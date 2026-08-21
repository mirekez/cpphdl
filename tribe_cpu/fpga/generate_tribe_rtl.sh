#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "${script_dir}/../.." && pwd)"
output_dir="${script_dir}/cpphdl_tribe256_multicore"

converter="${CPPHDL_CONVERTER:-}"
if [[ -z "${converter}" ]]; then
    for candidate in \
        "${repo_dir}/build/cpphdl" \
        "${repo_dir}/build-cpphdl-all-tests/cpphdl" \
        "${script_dir}/test_build/cpphdl"; do
        if [[ -x "${candidate}" ]]; then
            converter="${candidate}"
            break
        fi
    done
fi

if [[ -z "${converter}" || ! -x "${converter}" ]]; then
    echo "Missing CppHDL converter." >&2
    echo "Build the cpphdl target or set CPPHDL_CONVERTER to its executable." >&2
    exit 1
fi

mkdir -p "${output_dir}/generated"
cd "${output_dir}"

"${converter}" \
    --primary_clock clk 312000000 \
    --secondary_clock l2_clock 156000000 \
    "${repo_dir}/tribe_cpu/main.cpp" \
    -DL2_AXI_WIDTH=256 \
    -DTRIBE_RAM_BYTES_CONFIG=458752 \
    -DTRIBE_IO_REGION_SIZE_CONFIG=4194304 \
    -DMULTICORE \
    -I "${repo_dir}/include" \
    -I "${repo_dir}/tribe_cpu/common" \
    -I "${repo_dir}/tribe_cpu/spec" \
    -I "${repo_dir}/tribe_cpu/devices" \
    -I "${repo_dir}/tribe_cpu/cache"
