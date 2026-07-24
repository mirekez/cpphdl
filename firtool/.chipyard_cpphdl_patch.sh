#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
chipyard_root=${1:-"$script_dir/chipyard"}
chipyard_root=$(cd "$chipyard_root" && pwd)
chipyard_patch="$script_dir/chipyard_cpphdl.patch"
firrtl_patch="$script_dir/firrtl_cpphdl.patch"
circt_commit=${CIRCT_CPPHDL_COMMIT:-481cb60add7358934414a3c6b396f5d29ad934fe}
circt_repo=${CIRCT_CPPHDL_REPO_URL:-https://github.com/llvm/circt.git}
circt_dir=${CIRCT_CPPHDL_SOURCE_DIR:-"$chipyard_root/tools/circt-cpphdl"}

step() { printf '\n==> %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

[[ -f "$chipyard_root/build.sbt" ]] || die "$chipyard_root is not a Chipyard checkout"
[[ -f "$chipyard_patch" ]] || die "missing $chipyard_patch"
[[ -f "$firrtl_patch" ]] || die "missing $firrtl_patch"

apply_once() {
  local repo=$1 patch=$2 label=$3
  if git -C "$repo" apply --check "$patch" 2>/dev/null; then
    git -C "$repo" apply "$patch"
    echo "applied $label patch"
  elif git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
    echo "$label patch is already applied"
  elif [[ "$label" == Chipyard ]] &&
       [[ -x "$repo/scripts/build-cpphdl-rocket64.sh" ]] &&
       [[ -f "$repo/tools/firtool-cpphdl/runtime/CMakeLists.txt" ]] &&
       grep -qF 'CPPHDL_USE_OPTIMIZED_PCH' \
         "$repo/tools/firtool-cpphdl/runtime/CMakeLists.txt" &&
       grep -qF 'CPPHDL_BUILD_JOBS:-2' \
         "$repo/scripts/build-cpphdl-rocket64.sh"; then
    echo "$label patch is already applied (with local extensions)"
  elif [[ "$label" == firtool ]] &&
       [[ -f "$repo/include/circt/Conversion/ExportCppHDL.h" ]] &&
       [[ -f "$repo/lib/Conversion/ExportCppHDL/ExportCppHDL.cpp" ]] &&
       grep -qF 'OutputSplitCppHDL' "$repo/tools/firtool/firtool.cpp"; then
    echo "$label patch is already applied (with local extensions)"
  else
    die "$label patch does not apply cleanly in $repo"
  fi
}

step "Apply Chipyard C++HDL integration"
apply_once "$chipyard_root" "$chipyard_patch" Chipyard

step "Clone pinned CIRCT/firtool sources"
if [[ ! -d "$circt_dir/.git" ]]; then
  [[ ! -e "$circt_dir" ]] || die "$circt_dir exists but is not a Git checkout"
  mkdir -p "$(dirname "$circt_dir")"
  git init "$circt_dir"
  git -C "$circt_dir" remote add origin "$circt_repo"
  git -C "$circt_dir" fetch --depth=1 origin "$circt_commit"
  git -C "$circt_dir" checkout --detach FETCH_HEAD
fi
if [[ -n "$(git -C "$circt_dir" status --porcelain)" ]]; then
  if ! git -C "$circt_dir" apply --reverse --check "$firrtl_patch" 2>/dev/null; then
    if [[ ! -f "$circt_dir/include/circt/Conversion/ExportCppHDL.h" ]] ||
       [[ ! -f "$circt_dir/lib/Conversion/ExportCppHDL/ExportCppHDL.cpp" ]] ||
       ! grep -qF 'OutputSplitCppHDL' "$circt_dir/tools/firtool/firtool.cpp"; then
      die "$circt_dir has unrelated local changes"
    fi
    echo "using locally extended firtool C++HDL exporter"
  fi
else
  if ! git -C "$circt_dir" cat-file -e "$circt_commit^{commit}" 2>/dev/null; then
    git -C "$circt_dir" fetch --depth=1 origin "$circt_commit"
  fi
  git -C "$circt_dir" checkout --detach "$circt_commit"
fi

step "Apply direct firtool-to-C++HDL exporter"
apply_once "$circt_dir" "$firrtl_patch" firtool

step "Build patched firtool"
export PATH="$chipyard_root/.conda-env/bin:$PATH"
export RISCV=${RISCV:-"$chipyard_root/.conda-env/riscv-tools"}
if [[ "${CHIPYARD_CPPHDL_SKIP_BUILD:-0}" != 1 ]]; then
  CIRCT_CPPHDL_SOURCE_DIR="$circt_dir" \
    CPPHDL_FIRTOOL_BUILD_DIR="${CPPHDL_FIRTOOL_BUILD_DIR:-$chipyard_root/tools/firtool-cpphdl/build}" \
    JOBS="${CPPHDL_FIRTOOL_JOBS:-1}" \
    "$chipyard_root/scripts/build-firtool-cpphdl.sh"
else
  echo "skipped firtool build (CHIPYARD_CPPHDL_SKIP_BUILD=1)"
fi

step "Installation complete"
echo "Patched firtool: ${CPPHDL_FIRTOOL_BUILD_DIR:-$chipyard_root/tools/firtool-cpphdl/build}/firtool-cpphdl"
echo "C++HDL build: $script_dir/.build_rocket64_cpphdl.sh"
echo "C++HDL run: $script_dir/.run_rocket64_cpphdl.sh"
echo "Build-and-run validation: $script_dir/chipyard_cpphdl_test.sh"
