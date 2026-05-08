#!/usr/bin/env bash
set -euo pipefail

export RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

export PATH="${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"

make -C "${BUILD_DIR}" tribe

echo "Running RV32 Spike fragments on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_rv32_spike_fragments$'

echo "Running RV32 Spike fragments on Verilator Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_rv32_spike_fragments_verilator$'
