#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${SCRIPT_DIR}/riscv-isa-sim"
BUILD_DIR="${REPO_DIR}/build"
DTC_DIR="${SCRIPT_DIR}/dtc"
PREFIX="${SPIKE_PREFIX:-/home/me/riscv}"
REPO_URL="${SPIKE_REPO_URL:-https://github.com/riscv-software-src/riscv-isa-sim.git}"
DTC_REPO_URL="${DTC_REPO_URL:-https://github.com/dgibson/dtc.git}"

if ! command -v git >/dev/null 2>&1; then
  echo "error: git is required" >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "error: make is required" >&2
  exit 1
fi

if ! command -v dtc >/dev/null 2>&1 && [[ ! -x "${PREFIX}/bin/dtc" ]]; then
  if [[ ! -d "${DTC_DIR}/.git" ]]; then
    git clone "${DTC_REPO_URL}" "${DTC_DIR}"
  else
    git -C "${DTC_DIR}" fetch --tags --prune
  fi

  make -C "${DTC_DIR}" NO_PYTHON=1 NO_VALGRIND=1 PREFIX="${PREFIX}" -j"$(nproc)"
  make -C "${DTC_DIR}" NO_PYTHON=1 NO_VALGRIND=1 PREFIX="${PREFIX}" install
fi

export PATH="${PREFIX}/bin:${PATH}"

if [[ ! -d "${REPO_DIR}/.git" ]]; then
  git clone "${REPO_URL}" "${REPO_DIR}"
else
  git -C "${REPO_DIR}" fetch --tags --prune
fi

git -C "${REPO_DIR}" submodule update --init --recursive

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [[ ! -f Makefile ]]; then
  ../configure --prefix="${PREFIX}"
fi

make -j"$(nproc)"
make install

"${PREFIX}/bin/spike" --help >/dev/null
