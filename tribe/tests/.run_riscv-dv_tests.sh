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

export PATH="/usr/bin:${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"
export TRIBE_RISCV_DV_PYTHON="${TRIBE_RISCV_DV_PYTHON:-/usr/bin/python3}"
export TRIBE_RISCV_DV_ISA="${TRIBE_RISCV_DV_ISA:-rv32imac_zicsr_zifencei}"
export PYTHONPATH="${BUILD_DIR}/pydeps:${PYTHONPATH:-}"

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
    echo "Running riscv-dv generated tests on Verilator Tribe model"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_dv_verilator$'
fi
