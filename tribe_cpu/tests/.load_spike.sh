#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
REPO_DIR="${SCRIPT_DIR}/riscv-isa-sim"
BUILD_DIR="${REPO_DIR}/build"
DTC_DIR="${SCRIPT_DIR}/dtc"
RISCV_HOME="${RISCV_HOME:-${HOME}/riscv}"
PREFIX="${SPIKE_PREFIX:-${RISCV_HOME}}"
REPO_URL="${SPIKE_REPO_URL:-https://github.com/riscv-software-src/riscv-isa-sim.git}"
DTC_REPO_URL="${DTC_REPO_URL:-https://github.com/dgibson/dtc.git}"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-${SCRIPT_DIR}/riscv-tests}"
RISCV_TESTS_REPO_URL="${RISCV_TESTS_REPO_URL:-https://github.com/riscv-software-src/riscv-tests.git}"
RISCV_DV_DIR="${RISCV_DV_DIR:-${SCRIPT_DIR}/riscv-dv}"
RISCV_DV_REPO_URL="${RISCV_DV_REPO_URL:-https://github.com/google/riscv-dv.git}"
PYDEPS_DIR="${TRIBE_PYDEPS_DIR:-${ROOT_DIR}/build/pydeps}"
if [[ -n "${CONDA_PREFIX:-}" && -x "${CONDA_PREFIX}/bin/python" ]]; then
  DEFAULT_PYTHON_BIN="${CONDA_PREFIX}/bin/python"
else
  DEFAULT_PYTHON_BIN="python3"
fi
PYTHON_BIN="${PYTHON_BIN:-${DEFAULT_PYTHON_BIN}}"

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

SPIKE_CONFIGURE_ARGS=("--prefix=${PREFIX}")
if [[ -n "${CONDA_PREFIX:-}" ]]; then
  if [[ ! -d "${CONDA_PREFIX}/include/boost" || ! -e "${CONDA_PREFIX}/lib/libboost_regex.so" ]]; then
    echo "error: Boost development files are missing from the active Conda environment" >&2
    echo "hint: conda env update --prefix \"${CONDA_PREFIX}\" --file \"${ROOT_DIR}/requirements.yaml\"" >&2
    exit 1
  fi

  # Spike's bundled Autoconf macros can find Conda's Boost headers through
  # compiler flags while still failing to infer the matching library path.
  SPIKE_CONFIGURE_ARGS+=(
    "--with-boost=${CONDA_PREFIX}"
    "--with-boost-libdir=${CONDA_PREFIX}/lib"
  )
fi

clone_or_update() {
  local url="$1"
  local dir="$2"

  if [[ ! -d "${dir}/.git" ]]; then
    git clone "${url}" "${dir}"
  else
    git -C "${dir}" fetch --tags --prune
  fi
}

if ! command -v git >/dev/null 2>&1; then
  echo "error: git is required" >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "error: make is required" >&2
  exit 1
fi

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "error: ${PYTHON_BIN} is required" >&2
  exit 1
fi

if ! "${PYTHON_BIN}" - <<'PY' >/dev/null 2>&1; then
import sys
assert (3, 10) <= sys.version_info[:2] < (3, 14)
PY
  echo "error: Python 3.10 through 3.13 is required for riscv-dv/PyBoolector" >&2
  echo "hint: update the Conda environment from ${ROOT_DIR}/requirements.yaml" >&2
  exit 1
fi

if ! command -v dtc >/dev/null 2>&1 && [[ ! -x "${PREFIX}/bin/dtc" ]]; then
  clone_or_update "${DTC_REPO_URL}" "${DTC_DIR}"

  make -C "${DTC_DIR}" NO_PYTHON=1 NO_VALGRIND=1 NO_YAML=1 PREFIX="${PREFIX}" -j"$(nproc)"
  make -C "${DTC_DIR}" NO_PYTHON=1 NO_VALGRIND=1 NO_YAML=1 PREFIX="${PREFIX}" install
fi

export PATH="${PREFIX}/bin:${PATH}"
export RISCV="${RISCV:-${PREFIX}}"

clone_or_update "${REPO_URL}" "${REPO_DIR}"

git -C "${REPO_DIR}" submodule update --init --recursive

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [[ ! -f Makefile ]]; then
  ../configure "${SPIKE_CONFIGURE_ARGS[@]}"
fi

make -j"$(nproc)"
make install

if [[ ! -x "${PREFIX}/bin/spike" ]]; then
  echo "error: spike was not installed at ${PREFIX}/bin/spike" >&2
  exit 1
fi

clone_or_update "${RISCV_TESTS_REPO_URL}" "${RISCV_TESTS_DIR}"
git -C "${RISCV_TESTS_DIR}" submodule update --init --recursive

clone_or_update "${RISCV_DV_REPO_URL}" "${RISCV_DV_DIR}"
git -C "${RISCV_DV_DIR}" submodule update --init --recursive

mkdir -p "${PYDEPS_DIR}"
if ! PYTHONPATH="${PYDEPS_DIR}:${PYTHONPATH:-}" "${PYTHON_BIN}" - <<'PY' >/dev/null 2>&1; then
import requests
import pyboolector
import vsc
import yaml

def version_tuple(text):
  return tuple(int(part) for part in text.split(".")[:2] if part.isdigit())

assert version_tuple(yaml.__version__) >= (6, 0), yaml.__version__
assert version_tuple(requests.__version__) >= (2, 31), requests.__version__
PY
  "${PYTHON_BIN}" -m pip install --target "${PYDEPS_DIR}" --upgrade --no-warn-conflicts "${RISCV_DV_PYTHON_DEPS[@]}"
fi
