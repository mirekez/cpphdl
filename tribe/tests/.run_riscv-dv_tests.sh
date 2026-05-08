#!/usr/bin/env bash
set -euo pipefail

export RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

export PATH="/usr/bin:${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"
export TRIBE_RISCV_DV_PYTHON="${TRIBE_RISCV_DV_PYTHON:-/usr/bin/python3}"

make -C "${BUILD_DIR}" tribe

echo "Running riscv-dv generated tests on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_dv$'

echo "Running riscv-dv generated tests on Verilator Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_dv_verilator$'
