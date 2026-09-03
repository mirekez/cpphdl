#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "${script_dir}/../.." && pwd)"
profile="${TRIBE_FEATURE_PROFILE:-full}"
output_dir="${TRIBE_RTL_OUTPUT_DIR:-}"

usage()
{
    echo "usage: $0 [--profile full|fmax-base] [--output-dir DIR]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --profile)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            profile="$2"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            output_dir="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

case "${profile}" in
    full)
        cfg_rv32ia=1
        cfg_isr=1
        cfg_mmu_tlb=1
        ;;
    fmax-base)
        cfg_rv32ia=0
        cfg_isr=0
        cfg_mmu_tlb=0
        ;;
    *)
        echo "Unknown feature profile: ${profile}" >&2
        usage
        exit 2
        ;;
esac

cfg_rv32ia="${TRIBE_CFG_RV32IA:-${cfg_rv32ia}}"
cfg_isr="${TRIBE_CFG_ISR:-${cfg_isr}}"
cfg_mmu_tlb="${TRIBE_CFG_MMU_TLB:-${cfg_mmu_tlb}}"
for value in "${cfg_rv32ia}" "${cfg_isr}" "${cfg_mmu_tlb}"; do
    if [[ "${value}" != 0 && "${value}" != 1 ]]; then
        echo "Feature values must be 0 or 1" >&2
        exit 2
    fi
done

if [[ -z "${output_dir}" ]]; then
    if [[ "${profile}" == full ]]; then
        output_dir="${script_dir}/cpphdl_tribe256_multicore"
    else
        output_dir="${script_dir}/cpphdl_tribe256_multicore_${profile}"
    fi
fi

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

converter_args=(
    --primary_clock clk 312000000 \
    --secondary_clock l2_clock 156000000 \
    "${repo_dir}/tribe_cpu/main.cpp" \
    -DL2_AXI_WIDTH=256 \
    -DTRIBE_RAM_BYTES_CONFIG=458752 \
    -DTRIBE_IO_REGION_SIZE_CONFIG=4194304 \
    -DTRIBE_CFG_RV32IA="${cfg_rv32ia}" \
    -DTRIBE_CFG_ISR="${cfg_isr}" \
    -DTRIBE_CFG_MMU_TLB="${cfg_mmu_tlb}" \
    -DMULTICORE \
    -I "${repo_dir}/include" \
    -I "${repo_dir}/tribe_cpu/common" \
    -I "${repo_dir}/tribe_cpu/spec" \
    -I "${repo_dir}/tribe_cpu/devices" \
    -I "${repo_dir}/tribe_cpu/cache"
)

"${converter}" "${converter_args[@]}"

repo_revision="$(git -C "${repo_dir}" rev-parse HEAD)"
if [[ -n "$(git -C "${repo_dir}" status --porcelain --untracked-files=no)" ]]; then
    repo_dirty=1
else
    repo_dirty=0
fi
converter_hash="$(sha256sum "${converter}" | awk '{print $1}')"
{
    echo "profile=${profile}"
    echo "repo_revision=${repo_revision}"
    echo "repo_dirty=${repo_dirty}"
    echo "converter=${converter}"
    echo "converter_sha256=${converter_hash}"
    echo "TRIBE_CFG_RV32IA=${cfg_rv32ia}"
    echo "TRIBE_CFG_ISR=${cfg_isr}"
    echo "TRIBE_CFG_MMU_TLB=${cfg_mmu_tlb}"
    echo "L2_AXI_WIDTH=256"
    echo "TRIBE_RAM_BYTES_CONFIG=458752"
    echo "TRIBE_IO_REGION_SIZE_CONFIG=4194304"
    echo "primary_clock_hz=312000000"
    echo "secondary_clock_hz=156000000"
    printf 'command=%q' "${converter}"
    printf ' %q' "${converter_args[@]}"
    printf '\n'
} > rtl_manifest.txt

echo "Generated ${profile} RTL in ${output_dir}"
