#!/usr/bin/env bash
set -euo pipefail

export RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PATTERN="rv32ui-p-* rv32um-p-* rv32uc-p-rvc"

export PATH="${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"

make -C "${BUILD_DIR}" tribe

echo "Running supported RV32 riscv-tests on C++ model"
TRIBE_RISCV_TESTS_PATTERN="${PATTERN}" \
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_tests_rv32$'

echo "Running supported RV32 riscv-tests on Verilator model"
TRIBE_RISCV_TESTS_PATTERN="${PATTERN}" \
TRIBE_RISCV_TESTS_BACKEND=verilator \
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_tests_rv32$'
