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

RISCV_DV_PYTHON_DEPS=(
    wheel
    "setuptools>=65"
    bitarray
    bitstring
    numpy
    pandas
    pyboolector
    pyucis
    pyvsc
    "PyYAML>=6.0"
    "requests>=2.31"
    tabulate
    toposort
)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
TESTLIST="${ROOT_DIR}/tribe/tests/riscv_dv_tribe_testlist.yaml"

clean_verilator_obj_dirs() {
    for width in 256 128 64; do
        rm -rf "${BUILD_DIR}/tribe${width}/Tribe/obj_dir"
    done
}

ensure_riscv_dv_testlist() {
    if [[ -f "${TESTLIST}" ]]; then
        return
    fi

    cat > "${TESTLIST}" <<'YAML'
- test: tribe_arithmetic_basic_test
  description: >
    Short RV32IMC arithmetic/random corner test for Tribe smoke regression.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=80
    +num_of_sub_program=0
    +directed_instr_0=riscv_int_numeric_corner_stream,4
    +no_fence=1
    +no_data_page=1
    +no_branch_jump=1
    +boot_mode=m
    +no_csr_instr=1
  rtl_test: core_base_test

- test: tribe_amo_test
  description: >
    Short RV32A AMO/LR/SC directed test for Tribe.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=120
    +directed_instr_0=riscv_lr_sc_instr_stream,4
    +directed_instr_1=riscv_amo_instr_stream,6
    +no_fence=1
    +no_branch_jump=1
    +num_of_sub_program=0
    +boot_mode=m
    +no_csr_instr=1
  rtl_test: core_base_test

- test: tribe_trap_test
  description: >
    Short illegal-instruction trap regression for Tribe privileged trap flow.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=120
    +illegal_instr_ratio=8
    +num_of_sub_program=0
    +boot_mode=m
    +no_fence=1
    +no_data_page=1
  rtl_test: core_base_test

- test: tribe_interrupt_test
  description: >
    Short privileged interrupt-handler generation smoke for Tribe.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=100
    +enable_interrupt=1
    +enable_timer_irq=1
    +num_of_sub_program=0
    +boot_mode=m
    +no_fence=1
    +no_data_page=1
  rtl_test: core_base_test
YAML
}

export PATH="/usr/bin:${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"
export TRIBE_RISCV_DV_PYTHON="${TRIBE_RISCV_DV_PYTHON:-/usr/bin/python3}"
export TRIBE_RISCV_DV_ISA="${TRIBE_RISCV_DV_ISA:-rv32imac_zicsr_zifencei}"
export TRIBE_RISCV_DV_TESTLIST="${TRIBE_RISCV_DV_TESTLIST:-${TESTLIST}}"
export PYTHONPATH="${BUILD_DIR}/pydeps:${PYTHONPATH:-}"

ensure_riscv_dv_testlist
make -C "${BUILD_DIR}" tribe256 tribe128 tribe64

if ! "${TRIBE_RISCV_DV_PYTHON}" - <<'PY' >/dev/null 2>&1; then
import requests
import vsc
import yaml

def version_tuple(text):
    return tuple(int(part) for part in text.split(".")[:2] if part.isdigit())

assert version_tuple(yaml.__version__) >= (6, 0), yaml.__version__
assert version_tuple(requests.__version__) >= (2, 31), requests.__version__
PY
    echo "Installing riscv-dv Python dependencies into ${BUILD_DIR}/pydeps"
    "${TRIBE_RISCV_DV_PYTHON}" -m pip install \
        --target "${BUILD_DIR}/pydeps" \
        --upgrade \
        --no-warn-conflicts \
        "${RISCV_DV_PYTHON_DEPS[@]}"
fi

echo "Running riscv-dv generated tests on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_dv$'

if [[ "${NO_VERIL}" -eq 0 ]]; then
    clean_verilator_obj_dirs
    echo "Running riscv-dv generated tests on Verilator Tribe model"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_dv_verilator$'
fi
