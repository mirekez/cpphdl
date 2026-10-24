#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PREFIX="${SAIL_RISCV_PREFIX:-${RISCV_HOME:-${HOME}/riscv}}"
REPO_DIR="${SAIL_RISCV_REPO_DIR:-${SCRIPT_DIR}/sail-riscv}"
REPO_URL="${SAIL_RISCV_REPO_URL:-https://github.com/riscv/sail-riscv.git}"
RISCV_ARCH_TEST_DIR="${RISCV_ARCH_TEST_DIR:-${SCRIPT_DIR}/riscv-arch-test}"
RISCV_ARCH_TEST_REPO_URL="${RISCV_ARCH_TEST_REPO_URL:-https://github.com/riscv-non-isa/riscv-arch-test.git}"
# These defaults are read from the same lock as the ACT4 source revision.
SAIL_COMPILER_REPO="${SAIL_COMPILER_REPO:-https://github.com/rems-project/sail}"
MISE_PREFIX="${MISE_PREFIX:-${HOME}/.local}"
MISE_INSTALL_URL="${MISE_INSTALL_URL:-https://mise.jdx.dev/install.sh}"
PYDEPS_DIR="${TRIBE_PYDEPS_DIR:-${ROOT_DIR}/build/pydeps}"
PYTHON_BIN="${PYTHON_BIN:-/usr/bin/python3}"
BUILD_DIR="${ROOT_DIR}/build"

RISCV_ARCH_TEST_PYTHON_DEPS=(
  wheel
  "setuptools>=65"
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

LOCK_HELPER="${SCRIPT_DIR}/scripts/riscv_dependencies.py"
LOCKED_SAIL_MODEL="$("${PYTHON_BIN}" "${LOCK_HELPER}" value sail_model_version)"
LOCKED_SAIL_COMPILER="$("${PYTHON_BIN}" "${LOCK_HELPER}" value sail_compiler_version)"
SAIL_RISCV_VERSION="${SAIL_RISCV_VERSION:-${LOCKED_SAIL_MODEL}}"
SAIL_COMPILER_VERSION="${SAIL_COMPILER_VERSION:-${LOCKED_SAIL_COMPILER}}"
SAIL_COMPILER_REQUIRED_VERSION="${SAIL_COMPILER_REQUIRED_VERSION:-${LOCKED_SAIL_COMPILER}}"
if [[ "${SAIL_RISCV_VERSION}" != "${LOCKED_SAIL_MODEL}" ||
      "${SAIL_COMPILER_VERSION}" != "${LOCKED_SAIL_COMPILER}" ||
      "${SAIL_COMPILER_REQUIRED_VERSION}" != "${LOCKED_SAIL_COMPILER}" ]]; then
  echo "error: Sail overrides disagree with riscv-tools.lock.json; update and validate the lock as a compatible pair" >&2
  exit 1
fi

ensure_dependency() {
  "${PYTHON_BIN}" "${SCRIPT_DIR}/scripts/ensure_git_repo.py" \
    "$3" "$2" "$4" --dependency "$1" --update
}

# Select the compatible sources before installing or building tools.
ensure_dependency riscv-arch-test "${RISCV_ARCH_TEST_REPO_URL}" "${RISCV_ARCH_TEST_DIR}" tests
ensure_dependency sail-riscv "${REPO_URL}" "${REPO_DIR}" CMakeLists.txt

mkdir -p "${PREFIX}/bin"
mkdir -p "${MISE_PREFIX}/bin"
export PATH="${MISE_PREFIX}/bin:${PREFIX}/bin:${PATH}"
export MISE_DATA_DIR="${MISE_DATA_DIR:-${BUILD_DIR}/mise-data}"
export MISE_CACHE_DIR="${MISE_CACHE_DIR:-${BUILD_DIR}/mise-cache}"
export MISE_CONFIG_DIR="${MISE_CONFIG_DIR:-${BUILD_DIR}/mise-config}"
export MISE_STATE_DIR="${MISE_STATE_DIR:-${BUILD_DIR}/mise-state}"
export MISE_RUBY_COMPILE="${MISE_RUBY_COMPILE:-false}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-${BUILD_DIR}/xdg-data}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-${BUILD_DIR}/xdg-cache}"

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

mkdir -p "${MISE_DATA_DIR}" "${MISE_CACHE_DIR}" "${MISE_CONFIG_DIR}" "${MISE_STATE_DIR}" "${XDG_DATA_HOME}" "${XDG_CACHE_HOME}"
# riscv-arch-test uses Ruby through mise. Prefer mise's prebuilt Ruby so hosts
# without Ruby psych/libyaml build dependencies do not compile Ruby.
mise settings set ruby.compile false >/dev/null 2>&1 || \
  mise settings ruby.compile=false >/dev/null 2>&1 || true

mise --version

install_sail_compiler() {
  local kernel
  local machine
  local asset
  local url
  local fallback_url
  local archive

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

  # Recent Sail releases publish platform archives from a separate
  # `<version>-binary` release/tag.  Older releases placed the same archive on
  # the plain version tag, so retain that URL as a compatibility fallback.
  url="${SAIL_COMPILER_REPO}/releases/download/${SAIL_COMPILER_VERSION}-binary/${asset}"
  fallback_url="${SAIL_COMPILER_REPO}/releases/download/${SAIL_COMPILER_VERSION}/${asset}"
  archive="$(mktemp)"
  trap 'rm -f "${archive}"' EXIT
  echo "Installing Sail compiler ${SAIL_COMPILER_VERSION} into ${PREFIX}"
  if ! curl --fail --location --output "${archive}" "${url}"; then
    curl --fail --location --output "${archive}" "${fallback_url}"
  fi
  tar xzf "${archive}" --directory="${PREFIX}" --strip-components=1
  rm -f "${archive}"
  trap - EXIT
}

need_sail_install=0
if ! command -v sail >/dev/null 2>&1; then
  need_sail_install=1
elif ! sail --version | awk -v required="${SAIL_COMPILER_REQUIRED_VERSION}" \
    'NR == 1 { matched = ($1 == "Sail" && $2 == required) } END { exit !matched }'; then
  need_sail_install=1
fi

if [[ "${need_sail_install}" == "1" ]]; then
  if [[ "${SAIL_INSTALL_COMPILER:-1}" == "0" ]]; then
    echo "error: Sail compiler ${SAIL_COMPILER_REQUIRED_VERSION} is not available on PATH" >&2
    exit 1
  fi
  install_sail_compiler
fi

if ! sail --version | awk -v required="${SAIL_COMPILER_REQUIRED_VERSION}" \
    'NR == 1 { matched = ($1 == "Sail" && $2 == required) } END { exit !matched }'; then
  echo "error: installed Sail compiler does not match ${SAIL_COMPILER_REQUIRED_VERSION}" >&2
  exit 1
fi
sail --version

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

"${PYTHON_BIN}" "${LOCK_HELPER}" check-sail "${PREFIX}/bin/sail_riscv_sim"

mkdir -p "${PYDEPS_DIR}"
if ! PATH="${PYDEPS_DIR}/bin:${PATH}" command -v uv >/dev/null 2>&1; then
  "${PYTHON_BIN}" -m pip install --target "${PYDEPS_DIR}" --upgrade --no-warn-conflicts "${RISCV_ARCH_TEST_PYTHON_DEPS[@]}"
fi
