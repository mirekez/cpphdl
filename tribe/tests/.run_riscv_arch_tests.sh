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

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

export PATH="${BUILD_DIR}/pydeps/bin:${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"
export TRIBE_ARCH_TEST_GCC="${TRIBE_ARCH_TEST_GCC:-riscv32-unknown-elf-gcc}"
export TRIBE_ARCH_TEST_OBJDUMP="${TRIBE_ARCH_TEST_OBJDUMP:-riscv32-unknown-elf-objdump}"
export TRIBE_ARCH_TEST_EXTENSIONS="${TRIBE_ARCH_TEST_EXTENSIONS:-I,M,Zicsr,Zifencei,Zca,Zaamo,Zalrsc,ExceptionsS,ExceptionsU}"
export TRIBE_ARCH_TEST_EXCLUDE_EXTENSIONS="${TRIBE_ARCH_TEST_EXCLUDE_EXTENSIONS:-F,D,Zcf,Zcd,Zabha,Zicntr,Zihpm,Misalign,MisalignZca}"
export UV_CACHE_DIR="${UV_CACHE_DIR:-${BUILD_DIR}/tribe/tests/riscv-arch-test-uv-cache}"
export UV_PYTHON="${UV_PYTHON:-/usr/bin/python3}"
export MISE_DATA_DIR="${MISE_DATA_DIR:-${BUILD_DIR}/mise-data}"
export MISE_CACHE_DIR="${MISE_CACHE_DIR:-${BUILD_DIR}/mise-cache}"
export MISE_CONFIG_DIR="${MISE_CONFIG_DIR:-${BUILD_DIR}/mise-config}"
export MISE_STATE_DIR="${MISE_STATE_DIR:-${BUILD_DIR}/mise-state}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-${BUILD_DIR}/xdg-data}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-${BUILD_DIR}/xdg-cache}"

if ! command -v uv >/dev/null 2>&1; then
    echo "Installing uv into ${BUILD_DIR}/pydeps"
    "${Python3_EXECUTABLE:-/usr/bin/python3}" -m pip install \
        --target "${BUILD_DIR}/pydeps" \
        "${RISCV_ARCH_TEST_PYTHON_DEPS[@]}"
fi

if command -v mise >/dev/null 2>&1 && [[ -f "${ROOT_DIR}/tribe/tests/riscv-arch-test/.mise.toml" ]]; then
    mise trust "${ROOT_DIR}/tribe/tests/riscv-arch-test/.mise.toml"
    mise install --cd "${ROOT_DIR}/tribe/tests/riscv-arch-test"
fi

make -C "${BUILD_DIR}" tribe256 tribe128 tribe64

echo "Running RISC-V architectural tests on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_arch_test$'

if [[ "${NO_VERIL}" -eq 0 ]]; then
    echo "Running RISC-V architectural tests on Verilator Tribe model"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe(256|128|64)_riscv_arch_test_verilator$'
fi
