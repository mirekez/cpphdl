#!/usr/bin/env bash
#
# Starting point used for this script:
#   chipyard/chipyard commit: 0acc1e1de2d3284bcd4d876956932a013ffe1949
#   rocket-chip:             8f1e33b253e3bce741861c0a2e3ba8b7ff85b292
#   riscv-isa-sim:           9c190a07c6838f6392bafa4ad83acea462c7f759
#   libgloss:                39234a16247ab1fa234821b251f1f1870c3de343
#   install-circt:           3f8dda6e1c1965537b5801a43c81c287bac4eae4
#
# Optional fresh checkout:
#   git clone <chipyard-repo-url> chipyard
#   cd chipyard
#   git checkout 0acc1e1de2d3284bcd4d876956932a013ffe1949
#   bash .build.sh

set -euo pipefail

PRODUCT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -d "$PRODUCT_ROOT/chipyard" ]; then
    echo "Cloning chipyard"
    git clone https://github.com/ucb-bar/chipyard "$PRODUCT_ROOT/chipyard"
fi

cd "$PRODUCT_ROOT/chipyard"

TOP_COMMIT="0acc1e1de2d3284bcd4d876956932a013ffe1949"
ROCKET_COMMIT="8f1e33b253e3bce741861c0a2e3ba8b7ff85b292"
RISCV_ISA_SIM_COMMIT="9c190a07c6838f6392bafa4ad83acea462c7f759"
LIBGLOSS_COMMIT="39234a16247ab1fa234821b251f1f1870c3de343"
INSTALL_CIRCT_COMMIT="3f8dda6e1c1965537b5801a43c81c287bac4eae4"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

step() {
  printf '\n==> %s\n' "$*"
}

find_cmake() {
  local candidate
  for candidate in \
    "${CMAKE:-}" \
    "$(command -v cmake 2>/dev/null || true)" \
    /usr/bin/cmake \
    /usr/local/bin/cmake \
    "$HOME/miniforge3/bin/cmake" \
    "$HOME/miniconda3/bin/cmake" \
    "$HOME/anaconda3/bin/cmake"
  do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

CMAKE_BIN="$(find_cmake || true)"
if [ -z "$CMAKE_BIN" ]; then
  echo "error: cmake not found. Set CMAKE=/path/to/cmake before running ./.build.sh" >&2
  exit 1
fi
export CMAKE_BIN

clean_sbt_boot_after_failure() {
  echo "cleaning partial SBT boot/cache after failed dependency download"
  /usr/bin/rm -rf \
    "$ROOT/.sbt/boot" \
    "$ROOT/.sbt/launchers" \
    "$ROOT/.ivy2/cache/org.scala-sbt" \
    "$ROOT/.ivy2/cache/org.scala-lang" \
    "$ROOT/.ivy2/cache/org.scala-lang.modules" \
    "$ROOT/.ivy2/cache/org.scala-lang.plugins" \
    "$ROOT/.classpath_cache/chipyard.jar" \
    "$ROOT/.classpath_cache/stamp"
}

make_with_sbt_retry() {
  local attempts="${SBT_RETRIES:-5}"
  local delay="${SBT_RETRY_DELAY:-20}"
  local attempt rc log
  log="$(mktemp /tmp/chipyard-sbt-make.XXXXXX.log)"
  local v
  for v in CHIPYARD_DISABLE_OPTIONAL_MODULES CONDA_BUILD_SYSROOT; do
    if [ "${!v+x}" = "x" ]; then
      export "$v"
    fi
  done

  for attempt in $(seq 1 "$attempts"); do
    if [ "$attempt" -gt 1 ]; then
      echo "retrying SBT-backed make step: attempt $attempt/$attempts"
    fi

    set +e
    "$@" 2>&1 | tee "$log"
    rc="${PIPESTATUS[0]}"
    set -e

    if [ "$rc" -eq 0 ]; then
      /usr/bin/rm -f "$log"
      return 0
    fi

    if grep -Eq 'Connection timed out|Server access Error|No Scala version specified|Error during sbt execution|sbt-.*\.pom|repo1\.maven\.org' "$log"; then
      clean_sbt_boot_after_failure
      if [ "$attempt" -lt "$attempts" ]; then
        echo "SBT dependency bootstrap failed transiently; sleeping ${delay}s before retry"
        sleep "$delay"
      fi
    else
      cat "$log" >/dev/null
      /usr/bin/rm -f "$log"
      return "$rc"
    fi
  done

  echo "error: SBT-backed make step failed after $attempts attempts" >&2
  /usr/bin/rm -f "$log"
  return "$rc"
}

source_chipyard_env() {
  # Conda activation/deactivation hooks can reference unset CONDA_BACKUP_*
  # variables. Keep this script strict, but do not run conda hooks under nounset.
  set +u
  # shellcheck disable=SC1091
  source env.sh
  set -u
}

deactivate_local_conda_env() {
  if [ "${CONDA_PREFIX:-}" != "$ROOT/.conda-env" ]; then
    return
  fi

  set +u
  if command -v conda >/dev/null 2>&1; then
    conda deactivate >/dev/null 2>&1 || true
  fi
  set -u
  hash -r 2>/dev/null || true
}

remove_conda_setup_dirs() {
  deactivate_local_conda_env

  local d
  for d in "$ROOT/.conda-env" "$ROOT/.conda-lock-env"; do
    if [ -e "$d" ] || [ -L "$d" ]; then
      echo "removing ${d#$ROOT/}"
      /usr/bin/rm -rf --one-file-system "$d"
      hash -r 2>/dev/null || true
    fi
    if [ -e "$d" ] || [ -L "$d" ]; then
      echo "error: failed to remove $d" >&2
      /usr/bin/ls -ld "$d" >&2 || true
      exit 1
    fi
  done
}

compiler_supports_fcoroutines() {
  local cxx="$1"
  local sysroot_flag="${2:-}"
  local tmp
  tmp="$(mktemp -d)"
  cat > "$tmp/test.cc" <<'EOF'
#include <coroutine>
int main() { return 0; }
EOF
  "$cxx" ${sysroot_flag:+$sysroot_flag} -std=c++17 -fcoroutines -c "$tmp/test.cc" -o "$tmp/test.o" >/dev/null 2>&1
  local rc=$?
  /usr/bin/rm -rf "$tmp"
  return "$rc"
}

select_host_cxx() {
  local candidates=()
  local conda_prefix="${CONDA_PREFIX:-$ROOT/.conda-env}"
  local conda_cxx="$conda_prefix/bin/x86_64-conda-linux-gnu-c++"
  local conda_sysroot="$conda_prefix/x86_64-conda-linux-gnu/sysroot"

  if [ -x "$conda_cxx" ]; then
    if [ -d "$conda_sysroot" ]; then
      HOST_BUILD_SYSROOT="$conda_sysroot"
      HOST_SYSROOT_FLAG="--sysroot=$conda_sysroot"
    else
      HOST_BUILD_SYSROOT=""
      HOST_SYSROOT_FLAG=""
    fi
    if CONDA_BUILD_SYSROOT="$HOST_BUILD_SYSROOT" compiler_supports_fcoroutines "$conda_cxx" "$HOST_SYSROOT_FLAG"; then
      PATCH_VERILATOR_FCOROUTINES=0
      VERILATOR_NO_TIMING=0
      export HOST_BUILD_SYSROOT HOST_SYSROOT_FLAG PATCH_VERILATOR_FCOROUTINES VERILATOR_NO_TIMING
      printf '%s\n' "$conda_cxx"
      return 0
    fi
  fi

  if [ -n "${HOST_CXX:-}" ]; then
    candidates+=("$HOST_CXX")
  fi
  candidates+=(
    /opt/rh/gcc-toolset-14/root/usr/bin/g++
    /opt/rh/gcc-toolset-13/root/usr/bin/g++
    /opt/rh/gcc-toolset-12/root/usr/bin/g++
    /opt/rh/gcc-toolset-11/root/usr/bin/g++
    /opt/rh/gcc-toolset-10/root/usr/bin/g++
    /opt/rh/devtoolset-14/root/usr/bin/g++
    /opt/rh/devtoolset-13/root/usr/bin/g++
    /opt/rh/devtoolset-12/root/usr/bin/g++
    /opt/rh/devtoolset-11/root/usr/bin/g++
    /opt/rh/devtoolset-10/root/usr/bin/g++
    /usr/bin/g++-13
    /usr/bin/g++-12
    /usr/bin/g++-11
    /usr/bin/g++-10
    /usr/bin/g++
  )
  local cxx
  for cxx in "${candidates[@]}"; do
    if [ -x "$cxx" ] && compiler_supports_fcoroutines "$cxx"; then
      HOST_BUILD_SYSROOT=""
      HOST_SYSROOT_FLAG=""
      PATCH_VERILATOR_FCOROUTINES=0
      VERILATOR_NO_TIMING=0
      export HOST_BUILD_SYSROOT HOST_SYSROOT_FLAG PATCH_VERILATOR_FCOROUTINES VERILATOR_NO_TIMING
      printf '%s\n' "$cxx"
      return 0
    fi
  done

  printf 'error: no coroutine-capable host g++ found.\n' >&2
  printf 'Verilator timing is required; /usr/bin/g++ without <coroutine> cannot run this design.\n' >&2
  printf 'Expected conda compiler at: %s\n' "$conda_cxx" >&2
  printf 'Or rerun with HOST_CXX=/path/to/new-g++ HOST_CC=/path/to/new-gcc.\n' >&2
  return 1
}

matching_host_cc() {
  local cxx="$1"
  local cc="${cxx/g++/gcc}"
  if [ "$cc" = "$cxx" ]; then
    cc="${cxx%c++}cc"
  fi
  if [ -x "$cc" ]; then
    printf '%s\n' "$cc"
  elif [ -n "${HOST_CC:-}" ]; then
    printf '%s\n' "$HOST_CC"
  else
    printf '/usr/bin/gcc\n'
  fi
}

configure_host_compilers() {
  step "Select host compiler for FESVR and Verilator simulator"
  local verilator_bin
  HOST_CXX="$(select_host_cxx)"
  HOST_CC="$(matching_host_cc "$HOST_CXX")"
  verilator_bin="$(command -v verilator)"
  HOST_BUILD_SYSROOT="${HOST_BUILD_SYSROOT:-}"
  HOST_SYSROOT_FLAG="${HOST_SYSROOT_FLAG:-}"
  PATCH_VERILATOR_FCOROUTINES="${PATCH_VERILATOR_FCOROUTINES:-0}"
  VERILATOR_NO_TIMING="${VERILATOR_NO_TIMING:-0}"
  if [ "$VERILATOR_NO_TIMING" = "1" ]; then
    VERILATOR_CMD="$verilator_bin --main --no-timing --cc --exe"
  else
    VERILATOR_CMD="$verilator_bin --main --timing --cc --exe"
  fi
  export HOST_CC HOST_CXX HOST_BUILD_SYSROOT HOST_SYSROOT_FLAG PATCH_VERILATOR_FCOROUTINES VERILATOR_NO_TIMING VERILATOR_CMD
  "$HOST_CXX" --version | head -1
  echo "using HOST_CXX=$HOST_CXX"
  echo "using HOST_CC=$HOST_CC"
  if [ -n "${HOST_SYSROOT_FLAG:-}" ]; then
    echo "using HOST_SYSROOT_FLAG=$HOST_SYSROOT_FLAG"
  fi
  if [ "$VERILATOR_NO_TIMING" = "1" ]; then
    echo "using VERILATOR_CMD=$VERILATOR_CMD"
  fi
}

host_configure_args() {
  if [[ "${HOST_CC:-}" == *x86_64-conda-linux-gnu-cc ]]; then
    printf '%s\n' --build=x86_64-pc-linux-gnu --host=x86_64-conda-linux-gnu
  fi
}

host_build_env() {
  local sysroot_assignment=()
  local sysroot_unset=(-u CONDA_BUILD_SYSROOT)
  if [ -n "${HOST_BUILD_SYSROOT:-}" ]; then
    sysroot_assignment=(CONDA_BUILD_SYSROOT="$HOST_BUILD_SYSROOT")
    sysroot_unset=()
  fi

  env \
    "${sysroot_unset[@]}" \
    -u CFLAGS \
    -u CXXFLAGS \
    -u CPPFLAGS \
    -u LDFLAGS \
    -u LIBRARY_PATH \
    -u LD_LIBRARY_PATH \
    -u CPATH \
    -u C_INCLUDE_PATH \
    -u CPLUS_INCLUDE_PATH \
    -u PKG_CONFIG_PATH \
    -u PKG_CONFIG_LIBDIR \
    "${sysroot_assignment[@]}" \
    PATH="/usr/bin:/bin:${PATH:-}" \
    CC="$HOST_CC" \
    CXX="$HOST_CXX" \
    "$@"
}

require_clean_enough_checkout() {
  local current
  current="$(git rev-parse HEAD)"
  if [ "$current" = "$TOP_COMMIT" ]; then
    return 0
  fi

  if [ "${ALLOW_UNPINNED_CHIPYARD:-0}" = "1" ]; then
    printf 'warning: using unsupported Chipyard commit %s (expected %s)\n' \
      "$current" "$TOP_COMMIT" >&2
    return 0
  fi

  if git diff --quiet --ignore-submodules -- && \
     git diff --cached --quiet --ignore-submodules --; then
    printf 'checking out supported Chipyard commit %s (was %s)\n' \
      "$TOP_COMMIT" "$current"
    git checkout "$TOP_COMMIT"
    return 0
  fi

  printf 'error: Chipyard checkout is %s, but this build requires %s\n' \
    "$current" "$TOP_COMMIT" >&2
  printf 'error: local tracked changes prevent an automatic checkout.\n' >&2
  printf 'Use a clean checkout, or set ALLOW_UNPINNED_CHIPYARD=1 only when intentionally porting the patches.\n' >&2
  exit 1
}

enough_checkout() {
  require_clean_enough_checkout "$@"
}

early_cleanup_for_fresh_build() {
  step "Clean stale local setup state"

  remove_conda_setup_dirs

  /usr/bin/rm -rf .classpath_cache/chipyard.jar .classpath_cache/stamp
  /usr/bin/rm -rf tools/cde/target generators/diplomacy/diplomacy/src/target
  /usr/bin/rm -f sims/verilator/simulator-chipyard.harness-*
  /usr/bin/rm -rf toolchains/riscv-tools/riscv-isa-sim/build
  /usr/bin/rm -f .conda-env/riscv-tools/lib/libriscv.so* .conda-env/riscv-tools/lib/libfesvr.*
  make -C tools/DRAMSim2 clean >/dev/null 2>&1 || true

  for d in \
    generators/bar-fetchers \
    generators/boom \
    generators/gemmini \
    generators/constellation \
    generators/diplomacy \
    generators/hardfloat \
    generators/icenet \
    generators/rerocc \
    generators/rocket-chip-blocks \
    generators/rocket-chip-inclusive-cache \
    generators/shuttle \
    generators/testchipip \
    sims/firesim \
    tools/cde \
    tools/firrtl2 \
    tools/fixedpoint \
    tools/rocket-dsp-utils \
    tools/DRAMSim2
  do
    if [ -e "$d" ] && [ ! -e "$d/.git" ]; then
      mv "$d" "$d.bak-$(date +%Y%m%d-%H%M%S)"
    fi
  done

  git config --file .gitmodules --get-regexp path | awk '{print $2}' | while read -r d; do
    if [ -e "$d" ] && [ ! -e "$d/.git" ]; then
      mv "$d" "$d.bak-$(date +%Y%m%d-%H%M%S)"
    fi
  done
}

prepare_submodule_path() {
  local path="$1"
  if [ -e "$path" ] && [ ! -e "$path/.git" ]; then
    local backup="${path}.bak-$(date +%Y%m%d-%H%M%S)"
    echo "moving non-submodule directory $path to $backup"
    mv "$path" "$backup"
  fi
}

apply_local_fixes() {
  step "Apply local Rocket-only setup/build fixes"

  # Keep the conda sysroot pinned to the lockfile's available package.
  sed -i 's/^\([[:space:]]*-[[:space:]]*sysroot_linux-64=\).*/\12.34 # need to be close to system glibc for VCS compatibility/' \
    conda-reqs/chipyard-base.yaml

  # build-setup.sh used to rewrite sysroot_linux-64 from 2.34 to the host glibc
  # version and then regenerate the lockfile. On this host that selected 2.35,
  # which is not available in the conda channels. Trust the existing lockfile.
  for f in build-setup.sh scripts/build-setup.sh; do
    if ! grep -q 'using existing lockfile' "$f"; then
      perl -0pi -e 's/if \[ "\$SYS_GLIBC" != "\$DEFAULT_GLIBC" \]; then/if [ "$SYS_GLIBC" != "$DEFAULT_GLIBC" ] \&\& [ ! -f "$LOCKFILE" ]; then/' "$f"
      perl -0pi -e 's/(\n[ \t]*\$CYDIR\/scripts\/generate-conda-lockfiles\.sh\n[ \t]*exit_if_last_command_failed\n)/$1    elif [ "$SYS_GLIBC" != "$DEFAULT_GLIBC" ]; then\n        echo "System glibc ($SYS_GLIBC) differs from pinned conda sysroot ($DEFAULT_GLIBC); using existing lockfile $LOCKFILE"\n/' "$f"
    fi
  done

  # Partially initialized optional accelerator submodules make SBT compile
  # Gemmini/Radiance/etc. and their deps. For normal Rocket, disable optional
  # module discovery and exclude Chipyard DSP tutorial sources when requested.
  if ! grep -q 'disableOptionalModules' build.sbt; then
    perl -0pi -e 's/(lazy val chipyard = \{\n[ \t]*val useChisel7 = sys\.env\.contains\("USE_CHISEL7"\)\n)/$1  val disableOptionalModules = sys.env.contains("CHIPYARD_DISABLE_OPTIONAL_MODULES")\n/' build.sbt
  fi
  perl -0pi -e 's/if \(!useChisel7\) \{\n    hf = hf\.dependsOn\(midas_target_utils\)\n  \}/if (!useChisel7 \&\& !sys.env.contains("CHIPYARD_DISABLE_OPTIONAL_MODULES")) {\n    hf = hf.dependsOn(midas_target_utils)\n  }/' build.sbt
  perl -0pi -e 's/val dspExcludeSettings: Seq\[Def\.Setting\[_\]\] = if \(useChisel7\) Seq\(/val dspExcludeSettings: Seq[Def.Setting[_]] = if (useChisel7 || disableOptionalModules) Seq(/' build.sbt
  perl -0pi -e 's/constellation, barf, shuttle, rerocc,/constellation, barf,/g; s/constellation, barf, shuttle,/constellation, barf,/g' build.sbt
  perl -0pi -e 's/if \(useChisel7\) Seq\(\) else Seq\(sbt\.Project\.projectToRef\(firrtl2_bridge\)\)/if (useChisel7 || disableOptionalModules) Seq() else Seq(sbt.Project.projectToRef(firrtl2_bridge))/g' build.sbt
  perl -0pi -e 's/if \(useChisel7\) Seq\(\) else Seq\(sbt\.Project\.projectToRef\(dsptools\), sbt\.Project\.projectToRef\(rocket_dsp_utils\)\)/if (useChisel7 || disableOptionalModules) Seq() else Seq(sbt.Project.projectToRef(dsptools), sbt.Project.projectToRef(rocket_dsp_utils))/g' build.sbt
  if grep -q 'val discovered = optionalModules.filter' build.sbt; then
    perl -0pi -e 's/  \/\/ Discover optional modules if their submodule is initialized\n  val discovered = optionalModules\.filter \{ case \(dir, _\) =>\n    file\(s"generators\/\$dir\/\.git"\)\.exists\n  \}/  \/\/ Discover optional modules if their submodule is initialized\n  val discovered =\n    if (disableOptionalModules) Seq.empty\n    else optionalModules.filter { case (dir, _) =>\n      file(s"generators\/\$dir\/.git").exists\n    }/' build.sbt
  fi

  # Diplomacy imports org.chipsalliance.cde.config.Parameters. Some fresh
  # checkouts have the submodule but miss the explicit SBT dependency after
  # local patching, which makes diplomacy compile before CDE is on classpath.
  perl -0pi -e 's/lazy val diplomacy = freshProject\("diplomacy", file\("generators\/diplomacy\/diplomacy"\)\)\n(?!  \.dependsOn\(cde\)\n)/lazy val diplomacy = freshProject("diplomacy", file("generators\/diplomacy\/diplomacy"))\n  .dependsOn(cde)\n/s' build.sbt
  if ! grep -q 'lazy val cdeDir' build.sbt; then
    perl -0pi -e 's/lazy val cde = \(project in file\("tools\/cde"\)\)\n  \.settings\(commonSettings\)\n  \.settings\(Compile \/ scalaSource := baseDirectory\.value \/ ".*?"\)/lazy val cdeDir = {\n  val standalone = file("tools\/cde")\n  val nested = file("generators\/rocket-chip\/dependencies\/cde")\n  if ((standalone \/ "cde\/src\/chipsalliance\/rocketchip\/config.scala").exists) standalone else nested\n}\n\nlazy val cde = (project in cdeDir)\n  .settings(commonSettings)\n  .settings(Compile \/ scalaSource := baseDirectory.value \/ "cde\/src\/chipsalliance\/rocketchip")/s' build.sbt
  fi

  if ! grep -q 'tools/stage/src/main/scala/phases/LegacyFirrtl2.scala' build.sbt; then
    perl -0pi -e 's/"generators\/chipyard\/src\/main\/scala\/upf"/"generators\/chipyard\/src\/main\/scala\/upf",\n        "tools\/stage\/src\/main\/scala\/phases\/LegacyFirrtl2.scala"/' build.sbt
  fi

  # Rocket-only mode removes Shuttle/ReRoCC from the chipyard classpath. Keep
  # their config-only sources out of the same compilation and remove optional
  # type references from the shared DigitalTop/TileFragments sources.
  if ! grep -q 'config/ShuttleConfigs.scala' build.sbt; then
    perl -0pi -e 's#"generators/chipyard/src/main/scala/upf",\n(?:[ \t]*"tools/stage/src/main/scala/phases/LegacyFirrtl2.scala",?\n)?#"generators/chipyard/src/main/scala/upf",\n        "generators/chipyard/src/main/scala/config/ShuttleConfigs.scala",\n        "generators/chipyard/src/main/scala/config/RoCCAcceleratorConfigs.scala",\n        "tools/stage/src/main/scala/phases/LegacyFirrtl2.scala"\n#' build.sbt
  fi
  sed -i '/with rerocc\.CanHaveReRoCCTiles/d' \
    generators/chipyard/src/main/scala/DigitalTop.scala
  sed -i '/import shuttle\.common\.ShuttleTileAttachParams/d' \
    generators/chipyard/src/main/scala/config/fragments/TileFragments.scala
  perl -0pi -e 's/\n    case tp: ShuttleTileAttachParams => tp\.copy\(tileParams = tp\.tileParams\.copy\(\n      traceParams = Some\(tp\.tileParams\.traceParams\.get\.copy\(useArbiterMonitor = true\)\)\)\)//' \
    generators/chipyard/src/main/scala/config/fragments/TileFragments.scala
  perl -0pi -e 's/    val enableSFCFIRRTLEmissionPasses = if \(view\[ChipyardOptions\]\(annotations\)\.enableSFCFIRRTLEmission\) \{\n      Seq\(Dependency\[chipyard\.stage\.phases\.LegacyFirrtl2Emission\]\)\n    \} else \{\n      Seq\.empty\n    \}/    val enableSFCFIRRTLEmissionPasses = Seq.empty/' \
    tools/stage/src/main/scala/ChipyardStage.scala

  if grep -Eq 'rerocc\.CanHaveReRoCCTiles|import shuttle\.common\.ShuttleTileAttachParams|Dependency\[chipyard\.stage\.phases\.LegacyFirrtl2Emission\]' \
      generators/chipyard/src/main/scala/DigitalTop.scala \
      generators/chipyard/src/main/scala/config/fragments/TileFragments.scala \
      tools/stage/src/main/scala/ChipyardStage.scala; then
    echo "error: failed to remove optional Rocket-only Scala references" >&2
    exit 1
  fi

  rm -f tests/rocket64_native.c
  sed -i '/add_executable(rocket64-native rocket64_native\.c)/d' tests/CMakeLists.txt
  sed -i '/add_dump_target(rocket64-native)/d' tests/CMakeLists.txt
  install -m 644 "$PRODUCT_ROOT/rocket64_mmul.c" tests/rocket64_mmul.c
  if ! grep -q 'add_executable(rocket64-mmul ' tests/CMakeLists.txt; then
    sed -i '/add_executable(hello hello\.c)/a add_executable(rocket64-mmul rocket64_mmul.c)' \
      tests/CMakeLists.txt
  fi
  if ! grep -q 'add_dump_target(rocket64-mmul)' tests/CMakeLists.txt; then
    sed -i '/add_dump_target(hello)/a add_dump_target(rocket64-mmul)' \
      tests/CMakeLists.txt
  fi
}

init_critical_submodules() {
  step "Initialize Rocket/Chipyard base submodules"
  for path in \
    generators/rocket-chip \
    generators/diplomacy \
    generators/hardfloat \
    tools/cde \
    tools/firrtl2 \
    tools/fixedpoint \
    tools/DRAMSim2 \
    generators/testchipip \
    generators/rocket-chip-blocks \
    generators/rocket-chip-inclusive-cache \
    generators/boom \
    generators/bar-fetchers \
    generators/constellation \
    generators/icenet \
    generators/shuttle \
    generators/rerocc \
    toolchains/riscv-tools/riscv-isa-sim \
    toolchains/libgloss \
    tools/install-circt
  do
    prepare_submodule_path "$path"
  done

  git submodule update --init --recursive \
    generators/rocket-chip \
    generators/diplomacy \
    generators/hardfloat \
    tools/cde \
    tools/firrtl2 \
    tools/fixedpoint \
    tools/DRAMSim2 \
    generators/testchipip \
    generators/rocket-chip-blocks \
    generators/rocket-chip-inclusive-cache \
    generators/boom \
    generators/bar-fetchers \
    generators/constellation \
    generators/icenet \
    generators/shuttle \
    generators/rerocc \
    toolchains/riscv-tools/riscv-isa-sim \
    toolchains/libgloss \
    tools/install-circt

  git -C generators/rocket-chip checkout "$ROCKET_COMMIT"
  git -C toolchains/riscv-tools/riscv-isa-sim checkout "$RISCV_ISA_SIM_COMMIT"
  git -C toolchains/libgloss checkout "$LIBGLOSS_COMMIT"
  git -C tools/install-circt checkout "$INSTALL_CIRCT_COMMIT"

  if [ ! -f tools/cde/cde/src/chipsalliance/rocketchip/config.scala ]; then
    echo "warning: tools/cde source is missing; falling back to generators/rocket-chip/dependencies/cde" >&2
    echo "missing: tools/cde/cde/src/chipsalliance/rocketchip/config.scala" >&2
    git submodule status tools/cde >&2 || true
    if [ ! -f generators/rocket-chip/dependencies/cde/cde/src/chipsalliance/rocketchip/config.scala ]; then
      echo "error: fallback CDE source is also missing" >&2
      echo "expected: generators/rocket-chip/dependencies/cde/cde/src/chipsalliance/rocketchip/config.scala" >&2
      git submodule status generators/rocket-chip/dependencies/cde >&2 || true
      exit 1
    fi
  fi
  if ! grep -q 'dependsOn(cde)' build.sbt; then
    echo "error: build.sbt does not make diplomacy depend on cde" >&2
    exit 1
  fi

  /usr/bin/rm -rf tools/cde/target generators/diplomacy/diplomacy/src/target .classpath_cache/chipyard.jar .classpath_cache/stamp
}

make_conda_env() {
  step "Create/update lean local conda environment"
  remove_conda_setup_dirs

  if [ ! -f env.sh ] || [ ! -x .conda-env/bin/conda ]; then
    set +u
    ./build-setup.sh \
      --use-lean-conda \
      --skip-submodules \
      -s 2 \
      --skip-toolchain \
      --skip-circt \
      --skip-precompile \
      --skip-clean
    set -u
  fi

  source_chipyard_env
}

install_circt() {
  step "Install pinned CIRCT/firtool if missing"
  source_chipyard_env

  if ! command -v firtool >/dev/null 2>&1; then
    tools/install-circt/bin/download-release-or-nightly-circt.sh \
      -f circt-full-static-linux-x64.tar.gz \
      -i "$RISCV" \
      -v version-file \
      -x conda-reqs/circt.json \
      -g "${GITHUB_TOKEN:-unused}"
  fi

  firtool --version | head -3
}

install_fesvr_and_libgloss() {
  step "Install FESVR headers/libfesvr and Chipyard libgloss HTIF support"
  source_chipyard_env

  /usr/bin/rm -rf toolchains/riscv-tools/riscv-isa-sim/build
  /usr/bin/rm -f "$RISCV"/lib/libriscv.so* "$RISCV"/lib/libfesvr.* \
    "$RISCV"/lib/libsoftfloat.so* "$RISCV"/lib/libdisasm.so* "$RISCV"/lib/libcustomext.so*
  (
    cd toolchains/riscv-tools/riscv-isa-sim
    mkdir -p build
    cd build
    host_build_env ../configure \
      $(host_configure_args) \
      --prefix="$RISCV" \
      --with-boost=no \
      --with-boost-asio=no \
      --with-boost-regex=no
    host_build_env make -j1
    host_build_env make libriscv.a
    host_build_env make install
    host_build_env make libfesvr.a
    mkdir -p "$RISCV/lib"
    cp -p libfesvr.a "$RISCV/lib/"
    for lib in libriscv.a libfdt.a libsoftfloat.a libdisasm.a; do
      if [ -f "$lib" ]; then
        cp -p "$lib" "$RISCV/lib/"
      fi
    done
    /usr/bin/rm -f "$RISCV"/lib/libriscv.so* "$RISCV"/lib/libsoftfloat.so* \
      "$RISCV"/lib/libdisasm.so* "$RISCV"/lib/libcustomext.so*
  )

  (
    cd toolchains/libgloss
    mkdir -p build
    cd build
    ../configure --prefix="$RISCV/riscv64-unknown-elf" --host=riscv64-unknown-elf
    make -j1
    make install
  )

  local riscv_libdir="$RISCV/riscv64-unknown-elf/lib"
  mkdir -p "$riscv_libdir"
  for f in htif.specs htif_nano.specs htif_wrap.specs htif_argv.specs htif.ld; do
    if [ ! -f "$riscv_libdir/$f" ]; then
      cp -p "toolchains/libgloss/util/$f" "$riscv_libdir/$f"
    fi
  done
  if ! riscv64-unknown-elf-g++ -print-file-name=htif_nano.specs | grep -q '/htif_nano\.specs$'; then
    echo "error: riscv64-unknown-elf-g++ still cannot find htif_nano.specs after libgloss install" >&2
    echo "expected it under: $riscv_libdir" >&2
    exit 1
  fi

  # The conda toolchain provides <riscv/encoding.h>; Chipyard tests include
  # <riscv-pk/encoding.h>. Add the expected compatibility include path.
  mkdir -p "$RISCV/riscv64-unknown-elf/include/riscv-pk"
  ln -sfn "$RISCV/include/riscv/encoding.h" \
    "$RISCV/riscv64-unknown-elf/include/riscv-pk/encoding.h"
}

build_dramsim2() {
  local build_jobs="${CHIPYARD_BUILD_JOBS:-1}"
  if [ ! -d tools/DRAMSim2 ]; then
    return
  fi

  (
    cd tools/DRAMSim2
    perl -0pi -e 's/^\t?.*CXX\)\s+/\t\${CXX} /mg' Makefile
    perl -0pi -e 's/^\tg\+\+ /\t\${CXX} /mg' Makefile
    make clean >/dev/null 2>&1 || true
    env \
      -u CFLAGS \
      -u CXXFLAGS \
      -u CPPFLAGS \
      -u LDFLAGS \
      -u LIBRARY_PATH \
      -u LD_LIBRARY_PATH \
      -u CPATH \
      -u C_INCLUDE_PATH \
      -u CPLUS_INCLUDE_PATH \
      -u PKG_CONFIG_PATH \
      -u PKG_CONFIG_LIBDIR \
      PATH="/usr/bin:/bin:${PATH:-}" \
      CC="$HOST_CC" \
      CXX="$HOST_CXX" \
      AR="${AR:-ar}" \
      make libdramsim.a -j"$build_jobs"
  )
}

patch_verilator_fcoroutines_if_needed() {
  local config="$1"
  local model_dir="$ROOT/sims/verilator/generated-src/chipyard.harness.TestHarness.${config}/chipyard.harness.TestHarness.${config}"
  local mk="$model_dir/VTestDriver.mk"

  if [ "${PATCH_VERILATOR_FCOROUTINES:-0}" != "1" ]; then
    return
  fi

  local verilator_root
  verilator_root="$(cd "$(dirname "$(command -v verilator)")/../share/verilator" && pwd)"
  if [ -f "$verilator_root/include/verilated.mk" ]; then
    cp -p "$verilator_root/include/verilated.mk" "$verilator_root/include/verilated.mk.bak" 2>/dev/null || true
    perl -0pi -e 's/^CFG_CXXFLAGS_COROUTINES\s*=.*$/CFG_CXXFLAGS_COROUTINES =/m; s/\s+-fcoroutines\b//g' \
      "$verilator_root/include/verilated.mk"
  fi

  CHIPYARD_DISABLE_OPTIONAL_MODULES=1 make_with_sbt_retry make -C sims/verilator \
    CONFIG="$config" \
    VERILATOR="$VERILATOR_CMD" \
    CFG_CXXFLAGS_COROUTINES= \
    "$mk"

  perl -0pi -e 's/(^VM_CXXFLAGS =.*?)(?:\s+-fcoroutines)(.*?\n)/$1$2/ms; s/(^CXXFLAGS =.*?)(?:\s+-fcoroutines)(.*?\n)/$1$2/ms; s/\s+-fcoroutines\b//g; s/\s+-DVL_TIME_CONTEXT\b//g' \
    "$model_dir"/VTestDriver*.mk
  perl -0pi -e 's/^VM_TIMING\s*=\s*1$/VM_TIMING = 0/m; s/^[ \t]*verilated_timing[ \t]*\\[ \t]*\n//m' \
    "$model_dir/VTestDriver_classes.mk"
  (cd "$model_dir" && "$HOST_CXX" -E -x c++ /dev/null >/dev/null)
  if grep -q 'verilated_timing' \
    "$model_dir/VTestDriver_classes.mk"; then
    echo "error: failed to remove verilated_timing from VTestDriver_classes.mk" >&2
    grep -n 'VM_TIMING\|verilated_timing' \
      "$model_dir/VTestDriver_classes.mk" >&2 || true
    exit 1
  fi
  if [ ! -f "$model_dir/verilated_timing.h" ]; then
    echo "error: failed to create local no-coroutine verilated_timing.h stub" >&2
    exit 1
  fi
}

build_simulator() {
  local config="$1"
  local build_jobs="${CHIPYARD_BUILD_JOBS:-1}"
  build_dramsim2
  patch_verilator_fcoroutines_if_needed "$config"

  make_with_sbt_retry host_build_env env CHIPYARD_DISABLE_OPTIONAL_MODULES=1 \
    make -C sims/verilator \
    CONFIG="$config" \
    VERILATOR="$VERILATOR_CMD" \
    CXX="$HOST_CXX" \
    LINK="$HOST_CXX" \
    EXTRA_SIM_LDFLAGS="-static-libstdc++ -static-libgcc -no-pie -Wl,--allow-multiple-definition -Wl,--start-group -lriscv -lfdt -lsoftfloat -ldisasm -Wl,--end-group -ldl" \
    -j"$build_jobs"
}

run_host_simulator() {
  local sim="$1"
  shift
  local conda_sysroot="${HOST_BUILD_SYSROOT:-}"
  if [ -z "$conda_sysroot" ]; then
    conda_sysroot="${CONDA_PREFIX:-$ROOT/.conda-env}/x86_64-conda-linux-gnu/sysroot"
  fi

  local sim_loader=""
  local candidate
  for candidate in \
    "$conda_sysroot/lib64/ld-linux-x86-64.so.2" \
    "$conda_sysroot/lib/ld-linux-x86-64.so.2" \
    "${CONDA_PREFIX:-$ROOT/.conda-env}/x86_64-conda-linux-gnu/sysroot/lib64/ld-linux-x86-64.so.2" \
    "${CONDA_PREFIX:-$ROOT/.conda-env}/x86_64-conda-linux-gnu/sysroot/lib/ld-linux-x86-64.so.2" \
    "${CONDA_PREFIX:-$ROOT/.conda-env}/lib/ld-linux-x86-64.so.2"; do
    if [ -x "$candidate" ]; then
      sim_loader="$candidate"
      break
    fi
  done

  if [ -n "$sim_loader" ]; then
    local sim_libpath
    sim_libpath="$conda_sysroot/lib64:$conda_sysroot/lib:$conda_sysroot/usr/lib64:$conda_sysroot/usr/lib:${CONDA_PREFIX:-$ROOT/.conda-env}/lib:${RISCV:-$ROOT/.conda-env/riscv-tools}/lib"
    echo "running simulator through conda loader: $sim_loader"
    "$sim_loader" --library-path "$sim_libpath" "$sim" "$@"
  else
    "$sim" "$@"
  fi
}

build_rocket() {
  step "Build RocketConfig Verilog and Verilator simulator"
  source_chipyard_env

  CHIPYARD_DISABLE_OPTIONAL_MODULES=1 make_with_sbt_retry make -C sims/verilator CONFIG=RocketConfig verilog
  build_simulator RocketConfig
}

build_and_run_hello() {
  step "Build and run hello smoke test on RocketConfig"
  source_chipyard_env

  "$CMAKE_BIN" -S tests -B tests/build -D CMAKE_BUILD_TYPE=Debug
  "$CMAKE_BIN" --build tests/build --target hello -j1

  mkdir -p sims/verilator/output/chipyard.harness.TestHarness.RocketConfig
  export ROOT HOST_BUILD_SYSROOT RISCV
  export -f run_host_simulator
  HELLO_TIMEOUT="${HELLO_TIMEOUT:-300s}"
  HELLO_MAX_CYCLES="${HELLO_MAX_CYCLES:-100000000}"
  file sims/verilator/simulator-chipyard.harness-RocketConfig || true
  ldd sims/verilator/simulator-chipyard.harness-RocketConfig || true
  timeout "$HELLO_TIMEOUT" bash -c 'run_host_simulator "$@"' _ \
    sims/verilator/simulator-chipyard.harness-RocketConfig \
    +permissive \
    +max-cycles="$HELLO_MAX_CYCLES" \
    +permissive-off \
    "$ROOT/tests/build/hello.riscv" \
    </dev/null 2>&1 | tee sims/verilator/output/chipyard.harness.TestHarness.RocketConfig/hello.log

  step "Smoke-test log"
  tail -20 sims/verilator/output/chipyard.harness.TestHarness.RocketConfig/hello.log
}

build_and_run_rocket64_mmul() {
  step "Build and run native RV64 matrix test on RocketConfig"
  source_chipyard_env

  "$CMAKE_BIN" -S tests -B tests/build -D CMAKE_BUILD_TYPE=Release
  "$CMAKE_BIN" --build tests/build --target rocket64-mmul -j1

  local output_dir="sims/verilator/output/chipyard.harness.TestHarness.RocketConfig"
  local log="$output_dir/rocket64-mmul.log"
  mkdir -p "$output_dir"
  export ROOT HOST_BUILD_SYSROOT RISCV
  export -f run_host_simulator
  RV64_TIMEOUT="${RV64_TIMEOUT:-600s}"
  RV64_MAX_CYCLES="${RV64_MAX_CYCLES:-100000000}"
  timeout "$RV64_TIMEOUT" bash -c 'run_host_simulator "$@"' _ \
    sims/verilator/simulator-chipyard.harness-RocketConfig \
    +permissive \
    +max-cycles="$RV64_MAX_CYCLES" \
    +permissive-off \
    "$ROOT/tests/build/rocket64-mmul.riscv" \
    </dev/null 2>&1 | tee "$log"

  if ! grep -q '^ROCKET RV64 MMUL TEST PASSED:' "$log"; then
    echo "error: native RV64 Rocket test did not report PASS" >&2
    tail -40 "$log" >&2
    exit 1
  fi

  step "Native RV64 smoke-test log"
  tail -20 "$log"
}

main() {
  require_clean_enough_checkout
  early_cleanup_for_fresh_build
  apply_local_fixes
  init_critical_submodules
  make_conda_env
  configure_host_compilers
  install_circt
  install_fesvr_and_libgloss
  build_rocket
  build_and_run_hello
  build_and_run_rocket64_mmul

  step "Done"
  ls -lh \
    sims/verilator/simulator-chipyard.harness-RocketConfig \
    tests/build/hello.riscv \
    tests/build/rocket64-mmul.riscv \
    sims/verilator/output/chipyard.harness.TestHarness.RocketConfig/hello.log \
    sims/verilator/output/chipyard.harness.TestHarness.RocketConfig/rocket64-mmul.log
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi
