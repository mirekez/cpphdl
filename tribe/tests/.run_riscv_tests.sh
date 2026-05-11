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
PATTERN="rv32ui-p-* rv32um-p-* rv32ua-p-* rv32uc-p-rvc rv32mi-p-* rv32si-p-*"

export PATH="${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"

make -C "${BUILD_DIR}" tribe256 tribe128 tribe64

echo "Running supported RV32 riscv-tests on C++ model"
TRIBE_RISCV_TESTS_PATTERN="${PATTERN}" \
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_tests_rv32$'

if [[ "${NO_VERIL}" -eq 0 ]]; then
    echo "Running supported RV32 riscv-tests on Verilator model"
    TRIBE_RISCV_TESTS_PATTERN="${PATTERN}" \
        ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_tests_rv32_verilator_supported$'
fi
