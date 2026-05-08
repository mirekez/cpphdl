#!/usr/bin/env bash
set -euo pipefail

export RISCV_HOME="${RISCV_HOME:-/home/me/riscv}"

RISCV_DV_PYTHON_DEPS=(
    Jinja2
    Markdown
    MarkupSafe
    Pallets-Sphinx-Themes
    PyYAML
    Pygments
    Sphinx
    alabaster
    annotated-types
    anyio
    attrs
    babel
    bitarray
    bitstring
    certifi
    cffi
    charset-normalizer
    click
    cryptography
    docutils
    exceptiongroup
    flake8
    h11
    httpcore
    httpx
    httpx-sse
    idna
    imagesize
    inflection
    jsonschema
    jsonschema-specifications
    lxml
    markdown-it-py
    mccabe
    mcp
    mdurl
    numpy
    packaging
    pandas
    pillow
    pyboolector
    pycodestyle
    pycparser
    pydantic
    pydantic-core
    pydantic-settings
    pyflakes
    pyjwt
    python-dateutil
    python-dotenv
    python-jsonschema-objects
    python-multipart
    pytz
    pyucis
    pyvsc
    referencing
    reportlab
    requests
    rich
    rpds-py
    rst2pdf
    six
    snowballstemmer
    sphinx-issues
    sphinx-notfound-page
    sphinx-rtd-theme
    sphinxcontrib-applehelp
    sphinxcontrib-devhelp
    sphinxcontrib-htmlhelp
    sphinxcontrib-jquery
    sphinxcontrib-jsmath
    sphinxcontrib-log-cabinet
    sphinxcontrib-qthelp
    sphinxcontrib-serializinghtml
    sse-starlette
    starlette
    tabulate
    tibs
    tomli
    toposort
    typing-extensions
    typing-inspection
    tzdata
    urllib3
    uvicorn
)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

export PATH="/usr/bin:${RISCV_HOME}/bin:${PATH}"
export RISCV="${RISCV:-${RISCV_HOME}}"
export TRIBE_RISCV_DV_PYTHON="${TRIBE_RISCV_DV_PYTHON:-/usr/bin/python3}"
export PYTHONPATH="${BUILD_DIR}/pydeps:${PYTHONPATH:-}"

make -C "${BUILD_DIR}" tribe

if ! "${TRIBE_RISCV_DV_PYTHON}" -c 'import vsc' >/dev/null 2>&1; then
    echo "Installing riscv-dv Python dependencies into ${BUILD_DIR}/pydeps"
    "${TRIBE_RISCV_DV_PYTHON}" -m pip install \
        --target "${BUILD_DIR}/pydeps" \
        "${RISCV_DV_PYTHON_DEPS[@]}"
fi

echo "Running riscv-dv generated tests on C++ Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_dv$'

echo "Building Verilator Tribe model"
(cd "${BUILD_DIR}/tribe" && ./tribe 1)

echo "Running riscv-dv generated tests on Verilator Tribe model"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R '^Tribe_riscv_dv_verilator$'
