#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PREFIX="${SAIL_RISCV_PREFIX:-${RISCV_HOME:-/home/me/riscv}}"
REPO_DIR="${SAIL_RISCV_REPO_DIR:-${SCRIPT_DIR}/sail-riscv}"
REPO_URL="${SAIL_RISCV_REPO_URL:-https://github.com/riscv/sail-riscv.git}"
RISCV_ARCH_TEST_DIR="${RISCV_ARCH_TEST_DIR:-${SCRIPT_DIR}/riscv-arch-test}"
RISCV_ARCH_TEST_REPO_URL="${RISCV_ARCH_TEST_REPO_URL:-https://github.com/riscv-non-isa/riscv-arch-test.git}"
SAIL_RISCV_VERSION="${SAIL_RISCV_VERSION:-0.11}"
SAIL_COMPILER_VERSION="${SAIL_COMPILER_VERSION:-0.20.1}"
SAIL_COMPILER_REQUIRED_VERSION="${SAIL_COMPILER_REQUIRED_VERSION:-0.20.1}"
SAIL_COMPILER_REPO="${SAIL_COMPILER_REPO:-https://github.com/rems-project/sail}"
MISE_PREFIX="${MISE_PREFIX:-${HOME}/.local}"
MISE_INSTALL_URL="${MISE_INSTALL_URL:-https://mise.jdx.dev/install.sh}"
PYDEPS_DIR="${TRIBE_PYDEPS_DIR:-${ROOT_DIR}/build/pydeps}"
PYTHON_BIN="${PYTHON_BIN:-/usr/bin/python3}"

RISCV_ARCH_TEST_PYTHON_DEPS=(
  uv
  uv-build
  pydantic
  pyjson5
  rich
  ruamel-yaml
  typer
  pyright
  ruff
)

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: $1 is required" >&2
    exit 1
  fi
}

need_tool git
need_tool make
need_tool cmake
need_tool curl
need_tool tar
need_tool "${PYTHON_BIN}"

clone_or_update() {
  local url="$1"
  local dir="$2"

  if [[ ! -d "${dir}/.git" ]]; then
    git clone "${url}" "${dir}"
  else
    git -C "${dir}" fetch --tags --prune
  fi
}

mkdir -p "${PREFIX}/bin"
mkdir -p "${MISE_PREFIX}/bin"
export PATH="${MISE_PREFIX}/bin:${PREFIX}/bin:${PATH}"

install_mise() {
  if [[ "${SAIL_RISCV_INSTALL_MISE:-1}" == "0" ]]; then
    echo "error: mise is not available on PATH" >&2
    exit 1
  fi

  echo "Installing mise into ${MISE_PREFIX}"
  curl --fail --location "${MISE_INSTALL_URL}" | sh
}

if ! command -v mise >/dev/null 2>&1; then
  install_mise
fi

mise --version

install_sail_compiler() {
  local kernel
  local machine
  local asset
  local url

  kernel="$(uname -s)"
  machine="$(uname -m)"
  case "${kernel}:${machine}" in
    Linux:x86_64)
      asset="sail-Linux-x86_64.tar.gz"
      ;;
    Linux:aarch64|Linux:arm64)
      asset="sail-Linux-aarch64.tar.gz"
      ;;
    *)
      echo "error: no default Sail compiler binary for ${kernel}/${machine}" >&2
      echo "Install Sail manually or set PATH to an existing sail executable." >&2
      exit 1
      ;;
  esac

  url="${SAIL_COMPILER_REPO}/releases/download/${SAIL_COMPILER_VERSION}/${asset}"
  echo "Installing Sail compiler ${SAIL_COMPILER_VERSION} into ${PREFIX}"
  curl --fail --location "${url}" | tar xz --directory="${PREFIX}" --strip-components=1
}

need_sail_install=0
if ! command -v sail >/dev/null 2>&1; then
  need_sail_install=1
elif ! sail --version | grep -q "Sail ${SAIL_COMPILER_REQUIRED_VERSION}"; then
  need_sail_install=1
fi

if [[ "${need_sail_install}" == "1" ]]; then
  if [[ "${SAIL_INSTALL_COMPILER:-1}" == "0" ]]; then
    echo "error: Sail compiler ${SAIL_COMPILER_REQUIRED_VERSION} is not available on PATH" >&2
    exit 1
  fi
  install_sail_compiler
fi

sail --version

clone_or_update "${REPO_URL}" "${REPO_DIR}"

git -C "${REPO_DIR}" checkout "${SAIL_RISCV_VERSION}"
git -C "${REPO_DIR}" submodule update --init --recursive

cd "${REPO_DIR}"

if [[ -x ./build_simulator.sh ]]; then
  DOWNLOAD_GMP="${DOWNLOAD_GMP:-${SAIL_RISCV_DOWNLOAD_GMP:-TRUE}}" ./build_simulator.sh
else
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target sail_riscv_sim -j"$(nproc)"
fi

SIM="${REPO_DIR}/build/c_emulator/sail_riscv_sim"
if [[ ! -x "${SIM}" ]]; then
  echo "error: built simulator not found: ${SIM}" >&2
  exit 1
fi

install -m 0755 "${SIM}" "${PREFIX}/bin/sail_riscv_sim"

"${PREFIX}/bin/sail_riscv_sim" --version

clone_or_update "${RISCV_ARCH_TEST_REPO_URL}" "${RISCV_ARCH_TEST_DIR}"
git -C "${RISCV_ARCH_TEST_DIR}" submodule update --init --recursive

mkdir -p "${PYDEPS_DIR}"
if ! PATH="${PYDEPS_DIR}/bin:${PATH}" command -v uv >/dev/null 2>&1; then
  "${PYTHON_BIN}" -m pip install --target "${PYDEPS_DIR}" "${RISCV_ARCH_TEST_PYTHON_DEPS[@]}"
fi
