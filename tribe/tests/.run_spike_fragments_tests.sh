#!/usr/bin/env bash
set -euo pipefail

export RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"
NO_VERIL=0
for arg in "$@"; do
    case "${arg}" in
        --noveril)
            NO_VERIL=1
            ;;
        *)
            echo "unknown option: ${arg}" >&2
            exit 2
            ;;
    esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

export PATH="${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"

make -C "${BUILD_DIR}" tribe

echo "Running RV32 Spike fragments on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_rv32_spike_fragments$'

if [[ "${NO_VERIL}" -eq 0 ]]; then
    echo "Running RV32 Spike fragments on Verilator Tribe model"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_rv32_spike_fragments_verilator$'
fi
