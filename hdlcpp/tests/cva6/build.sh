#!/usr/bin/env bash
set -euo pipefail

die() { echo "CVA6: $*" >&2; exit 1; }

CVA6_REVISION=9c2cf60a83a0a6a7fc5dcd973a5a5f6c0f42cb75
CVA6_URL=https://github.com/openhwfoundation/cva6.git
VERILATOR_VERSION=5.050
VERILATOR_REVISION=848d926ebd4addacacd294dc84e35d9d4ae8078c
MAX_CYCLES=2000000

find_tool() {
    local override=$1 candidate
    shift
    if [[ -n "$override" ]]; then set -- "$override"; fi
    for candidate in "$@"; do
        if candidate=$(type -P -- "$candidate"); then
            # Preserve names such as clang++, which select the driver mode.
            realpath -ms -- "$candidate"
            return
        fi
    done
    return 1
}

find_benchmark_verilator() {
    local candidate version runtime
    local -a candidates
    mapfile -t candidates < <(type -aP verilator)
    candidates+=("$REPO/.conda/bin/verilator")
    if [[ -x "$VERILATOR_SOURCE/bin/verilator_bin" ]]; then
        candidates+=("$VERILATOR_SOURCE/bin/verilator")
    fi
    for candidate in "${candidates[@]}"; do
        [[ -x "$candidate" ]] || continue
        version=$("$candidate" --version 2>/dev/null) || continue
        [[ "$version" == "Verilator $VERILATOR_VERSION" || "$version" == "Verilator $VERILATOR_VERSION "* ]] || continue
        runtime=$("$candidate" --getenv VERILATOR_ROOT 2>/dev/null) || continue
        [[ -f "$runtime/include/verilated.h" ]] || continue
        realpath -ms -- "$candidate"
        return
    done
    return 1
}

initialize() {
    SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
    REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
    local workspace_id
    workspace_id=$(printf '%s' "$SCRIPT_DIR" | sha256sum | cut -c1-12)
    ROOT=$(realpath -m "${CVA6_WORK_ROOT:-/tmp/cva6-bash-$UID-$workspace_id}")
    [[ "$ROOT" == /tmp/* && "$ROOT" != /tmp/ && "$ROOT" != *[[:space:]]* ]] || die 'CVA6_WORK_ROOT must be a directory below /tmp without whitespace'
    SOURCE=$(realpath -m "${CVA6_SRC:-$SCRIPT_DIR/cva6}")
    FESVR=$(realpath -m "${SPIKE_INSTALL_DIR:-$ROOT/fesvr}")
    RISCV=$(realpath -m "${RISCV:-$HOME/riscv}")
    CXX=$(find_tool "${CPPHDL_CXX:-}" clang++ "$REPO/.conda/bin/clang++") || true
    HDLCPP=$(find_tool "${HDLCPP:-}" "$REPO/build/hdlcpp/Release/hdlcpp" "$REPO/build/hdlcpp/hdlcpp" "$REPO/hdlcpp/build/hdlcpp" hdlcpp) || true
    CPPHDL=$(find_tool "${CPPHDL:-}" "$REPO/build/Release/cpphdl" "$REPO/build/cpphdl" cpphdl) || true
    if [[ -n "$HDLCPP" ]]; then HDLCPP=$(realpath -- "$HDLCPP"); fi
    if [[ -n "$CPPHDL" ]]; then CPPHDL=$(realpath -- "$CPPHDL"); fi
    VERILATOR_SOURCE="$ROOT/tools/verilator-$VERILATOR_VERSION"
    if [[ -n "${VERILATOR:-}" ]]; then
        VERILATOR=$(find_tool "$VERILATOR") || die 'VERILATOR must point to an executable'
    else
        unset VERILATOR_ROOT VERILATOR_BIN
        VERILATOR=$(find_benchmark_verilator) || true
    fi
    ELF="$ROOT/elf/matrix_multiply.riscv"
    export CVA6_WORK_ROOT="$ROOT" CVA6_SRC="$SOURCE" SPIKE_INSTALL_DIR="$FESVR" RISCV
    export HDLCPP CPPHDL VERILATOR CPPHDL_CXX="$CXX"
    export CVA6_REPO_DIR="$SOURCE" TARGET_CFG=cv32a6_imac_sv32
    export HPDCACHE_DIR="$SOURCE/core/cache_subsystem/hpdcache"
    mkdir -p "$ROOT/logs" "$ROOT/stamps" "$ROOT/tmp"
    export TMPDIR="$ROOT/tmp" CCACHE_DISABLE=1 LC_ALL=C
}

# The documentation submodule is outside the simulation's dependency tree.
simulation_submodules() {
    git -C "$SOURCE" ls-tree -r HEAD | awk '$1 == "160000" && $4 !~ /^docs\// {print $4}'
}

require_source() {
    [[ -e "$SOURCE/.git" ]] && [[ $(git -C "$SOURCE" rev-parse HEAD) == "$CVA6_REVISION" ]] ||
        die 'run .build_cva6.sh to prepare the pinned checkout'
    local -a modules
    mapfile -t modules < <(simulation_submodules)
    if git -C "$SOURCE" submodule status --recursive -- "${modules[@]}" | grep '^[+-U]' >&2; then
        die 'CVA6 simulation dependencies differ from the pinned revisions'
    fi
    git -C "$SOURCE" diff --quiet --ignore-submodules=untracked HEAD ||
        die "tracked files in $SOURCE differ from the pinned checkout"
}

checkout_source() {
    if [[ ! -e "$SOURCE" ]]; then
        # A local shallow checkout of the exact commit, independent of the
        # upstream default branch and any other CVA6 installation.
        git init -q "$SOURCE"
        git -C "$SOURCE" remote add origin "$CVA6_URL"
    fi
    [[ -e "$SOURCE/.git" ]] || die "$SOURCE already exists and is not a Git checkout"
    if ! git -C "$SOURCE" rev-parse --verify HEAD >/dev/null 2>&1; then
        git -C "$SOURCE" fetch --depth=1 origin "$CVA6_REVISION"
        git -C "$SOURCE" checkout --detach "$CVA6_REVISION"
    fi
    [[ $(git -C "$SOURCE" rev-parse HEAD) == "$CVA6_REVISION" ]] ||
        die "$SOURCE must be at $CVA6_REVISION"
    local -a modules
    mapfile -t modules < <(simulation_submodules)
    git -C "$SOURCE" submodule update --init --recursive --depth 1 --jobs 4 -- "${modules[@]}"
    require_source
}

verilator_runtime() {
    [[ -n "$VERILATOR" ]] || die 'run .build_cva6.sh to prepare Verilator'
    local runtime
    runtime=$("$VERILATOR" --getenv VERILATOR_ROOT) || return
    [[ -f "$runtime/include/verilated.h" ]] || die "cannot find Verilator headers in $runtime"
    printf '%s\n' "$runtime"
}

build_verilator_tool() {
    local tool
    for tool in autoconf gcc g++ flex bison perl; do
        command -v "$tool" >/dev/null || die "building Verilator $VERILATOR_VERSION requires $tool"
    done
    if [[ ! -e "$VERILATOR_SOURCE" ]]; then
        mkdir -p "$ROOT/tools"
        git -c advice.detachedHead=false clone --depth 1 --branch "v$VERILATOR_VERSION" \
            https://github.com/verilator/verilator.git "$VERILATOR_SOURCE"
    fi
    [[ $(git -C "$VERILATOR_SOURCE" rev-parse HEAD) == "$VERILATOR_REVISION" ]] ||
        die "unexpected source revision in $VERILATOR_SOURCE"
    cd "$VERILATOR_SOURCE"
    if [[ ! -x configure ]]; then autoconf; fi
    if [[ ! -f src/Makefile ]]; then CC=gcc CXX=g++ ./configure; fi
    # Build the optimized Verilator executable in its source tree.
    make -C src -j4 opt
}

prepare_verilator() {
    if [[ -n "$VERILATOR" ]]; then return; fi
    printf 'Building Verilator %s in %s\n' "$VERILATOR_VERSION" "$VERILATOR_SOURCE"
    stage setup-verilator bash "$SCRIPT_DIR/build.sh" verilator-tool
    VERILATOR=$(find_benchmark_verilator) || die "could not run Verilator from $VERILATOR_SOURCE"
    export VERILATOR
}

build_fesvr() {
    if [[ -f "$FESVR/include/fesvr/dtm.h" && -f "$FESVR/lib/libfesvr.a" ]]; then return; fi
    [[ "$FESVR" == "$ROOT/fesvr" ]] || die "install FESVR in $FESVR or unset SPIKE_INSTALL_DIR"
    [[ -n "$CXX" ]] || die 'install clang++ or set CPPHDL_CXX'
    local spike="$SOURCE/verif/core-v-verif/vendor/riscv/riscv-isa-sim"
    local runtime
    runtime=$(verilator_runtime)
    mkdir -p "$ROOT/fesvr-build" "$FESVR/lib"
    cd "$ROOT/fesvr-build"
    CXX="$CXX" "$spike/configure" --prefix="$FESVR"
    # The pinned Spike Makefile adds a YAML dependency to every C++ object.
    # FESVR itself needs only the DPI headers and its static archive.
    make -j1 -o "$FESVR/include/yaml-cpp" "CPPFLAGS=-I$runtime/include/vltstd -I$spike" libfesvr.a
    make -j1 install-hdrs install-config-hdrs
    install -m 644 libfesvr.a "$FESVR/lib/libfesvr.a"
}

stage() {
    local label=$1; shift
    local child status=0 available disk
    echo "+ $label (log: $ROOT/logs/$label.log)"
    setsid bash -c 'ulimit -c 0; ulimit -v 5859375; ulimit -s 262144; ulimit -f 786432; exec timeout --kill-after=5s 86400 "$@"' bash "$@" > "$ROOT/logs/$label.log" 2>&1 &
    child=$!
    trap 'kill -- -"$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true' EXIT
    while kill -0 "$child" 2>/dev/null; do
        available=$(awk '/MemAvailable:/ { print $2 }' /proc/meminfo)
        disk=$(df -Pk "$ROOT" | awk 'END {print $4}')
        if (( available < 614400 || disk < 524288 )); then
            echo 'CVA6 resource guard: less than 600 MiB available RAM or 512 MiB temporary space' >&2
            kill -- -"$child" 2>/dev/null || true
            sleep 2
            kill -KILL -- -"$child" 2>/dev/null || true
            status=1
            break
        fi
        sleep 1
    done
    wait "$child" || status=$?
    trap - EXIT
    if (( status )); then tail -30 "$ROOT/logs/$label.log" >&2; return "$status"; fi
}

compilation_inputs() {
    # Reporting and validation do not generate model code.
    case "${1:-}" in
        hdlcpp) declare -f stage ;;
        verilator*) declare -f stage verilator_model ;;
        *native-graph*) declare -f stage native_graph ;;
        *) declare -f stage native_graph verilator_model collect_group optimize_combs compile_group cpp_model ;;
    esac
}

fingerprint() {
    local mode=$1 runtime
    case "$mode" in
        verilator|verilator-4threads)
            [[ -n "$CXX" ]] || die 'install clang++ or set CPPHDL_CXX'
            runtime=$(verilator_runtime) || return
            ;;
        hdlcpp) [[ -n "$HDLCPP" ]] || die 'build hdlcpp or set HDLCPP' ;;
        cpphdl|cpphdl-optimize-combs|cpphdl-native-graph|cpphdl-native-graph-4threads)
            [[ -n "$CXX" && -n "$CPPHDL" ]] || die 'build cpphdl and install clang++, or set CPPHDL and CPPHDL_CXX'
            ;;
        *) die "unknown backend $mode" ;;
    esac
    {
        printf '%s\n' "$mode" "$SOURCE" "$CVA6_REVISION"
        compilation_inputs "$mode"
        if [[ "$mode" == hdlcpp ]]; then
            sha256sum "$HDLCPP" "$SCRIPT_DIR/convert.sh" "$SCRIPT_DIR/conversion.in" "$SCRIPT_DIR"/run_cpphdl_testharness*
        else
            sha256sum "$CXX" "$FESVR/lib/libfesvr.a"
            find "$FESVR/include" -type f -print0 | sort -z | xargs -0r sha256sum
            if [[ "$mode" == verilator* ]]; then
                sha256sum "$SCRIPT_DIR/ariane-benchmark.patch" "$VERILATOR"
                if [[ -f "${VERILATOR%/*}/verilator_bin" ]]; then
                    sha256sum "${VERILATOR%/*}/verilator_bin"
                fi
                "$VERILATOR" --version
                find "$runtime/include" -type f -print0 | sort -z | xargs -0r sha256sum
            else
                sha256sum "$HDLCPP" "$CPPHDL" "$ROOT/stamps/hdlcpp" "$SCRIPT_DIR"/*.cpp
                find "$REPO/include" -name '*.h' -type f -print0 | sort -z | xargs -0r sha256sum
                if [[ "$mode" == *native-graph* ]]; then
                    find "$REPO/tools" -maxdepth 1 -type f -print0 | sort -z | xargs -0r sha256sum
                fi
            fi
        fi
    } | sha256sum | cut -d ' ' -f1
}

prepare_elf() {
    local prefix=${CVA6_CROSS_COMPILE:-} cc cxx signature
    if [[ -z "$prefix" ]]; then
        local candidate
        for candidate in "$RISCV/bin/riscv32-unknown-elf-" "$RISCV/bin/riscv64-unknown-elf-" riscv32-unknown-elf- riscv64-unknown-elf-; do
            if find_tool "${candidate}gcc" >/dev/null && find_tool "${candidate}g++" >/dev/null; then
                prefix=$candidate
                break
            fi
        done
    fi
    cc=$(find_tool "${prefix}gcc") || true
    cxx=$(find_tool "${prefix}g++") || true
    [[ -n "$prefix" && -n "$cc" && -n "$cxx" ]] ||
        die 'install an RV32-capable RISC-V GCC/G++ toolchain or set CVA6_CROSS_COMPILE'
    local common="$SOURCE/verif/tests/custom/common"
    local linker="$SOURCE/config/gen_from_riscv_config/linker/link.ld"
    local flags=(-O2 -static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles -ffreestanding -fno-builtin -msmall-data-limit=0 -march=rv32imac_zicsr_zifencei -mabi=ilp32)
    local includes=(-I"$common" -I"$SOURCE/verif/tests/custom/env" -I"$SOURCE/verif/sim/dv/user_extension")
    signature=$({ declare -f prepare_elf; printf '%s\n' "$SOURCE" "$CVA6_REVISION"; sha256sum "$cc" "$cxx" "$SCRIPT_DIR/matrix_multiply_large.cpp" "$SCRIPT_DIR/matrix_runtime.c"; } | sha256sum | cut -d ' ' -f1)
    if [[ -f "$ROOT/stamps/elf" && $(head -1 "$ROOT/stamps/elf") == "$signature" ]] &&
        tail -n +2 "$ROOT/stamps/elf" | sha256sum --check --status; then
        echo 'matmul ELF: verified cached build'
        return
    fi
    mkdir -p "$ROOT/elf"
    "$cxx" "${flags[@]}" "${includes[@]}" -fno-exceptions -fno-rtti -DMATRIX_SIZE=32 -DMATRIX_REPEATS=1 -c "$SCRIPT_DIR/matrix_multiply_large.cpp" -o "$ROOT/elf/matrix.o"
    "$cc" "${flags[@]}" "${includes[@]}" -c "$SCRIPT_DIR/matrix_runtime.c" -o "$ROOT/elf/runtime.o"
    "$cc" "${flags[@]}" "${includes[@]}" -c "$common/crt.S" -o "$ROOT/elf/crt.o"
    "$cxx" "${flags[@]}" -T "$linker" "$ROOT/elf/matrix.o" "$ROOT/elf/runtime.o" "$ROOT/elf/crt.o" -lgcc -o "$ELF.tmp"
    mv -- "$ELF.tmp" "$ELF"
    { echo "$signature"; sha256sum "$ELF"; } > "$ROOT/stamps/elf"
    rm -- "$ROOT/elf/matrix.o" "$ROOT/elf/runtime.o" "$ROOT/elf/crt.o"
}

prepare() {
    checkout_source
    prepare_elf
    build_model verilator
}

verilator_model() {
    local mode=$1 output=$2 threads=1 runtime
    if [[ "$mode" == verilator-4threads ]]; then threads=4; fi
    runtime=$(verilator_runtime)
    patch --output="$output/ariane_tb.cpp" "$SOURCE/corev_apu/tb/ariane_tb.cpp" "$SCRIPT_DIR/ariane-benchmark.patch"
    # Use upstream's RTL source list and flags. Without Spike tandem, only
    # FESVR is needed from the Spike build.
    cat > "$output/host.mk" <<EOF
verilate_command := \$(subst corev_apu/tb/ariane_tb.cpp,$output/ariane_tb.cpp,\$(verilate_command))
verilate_command := \$(subst -lfesvr -lriscv -ldisasm -lyaml-cpp,$FESVR/lib/libfesvr.a,\$(verilate_command))
EOF
    stage "build-$mode" make -C "$SOURCE" -f Makefile -f "$output/host.mk" verilate \
        "verilator=$VERILATOR" "VL_INC_DIR=$runtime/include" "SPIKE_INSTALL_DIR=$FESVR" \
        "ver-library=$output" target=cv32a6_imac_sv32 "verilator_threads=$threads" NUM_JOBS=1 \
        "CXX=$CXX" "LINK=$CXX" TRACE_FAST= TRACE_COMPACT= PROFILE= DEBUG= SPIKE_TANDEM= spike-tandem=
    grep -Eq "::threads\(\) const \\{ return ${threads}; \\}" "$output/Variane_testharness.cpp" ||
        die 'unexpected Verilator model thread count'
}

native_graph() {
    local output=$1 threads=$2
    cp "$SCRIPT_DIR/native_graph_top.cpp" "$output/"
    "$CPPHDL" --native-graph --top elaboration_root --chunk-size=512 --split-chunks=64 --optimize-threads="$threads" --cxx "$CXX" --frontend-flag=-DSYNTHESIS --frontend-flag=-I"$ROOT/ordinary" --frontend-flag=-I"$FESVR/include" --output "$output/graph" "$output/native_graph_top.cpp"
    local lanes
    lanes=$(sed -n 's/.*__cpphdl_thread_count = \([0-9]*\);.*/\1/p' "$output/graph/model.h")
    [[ ${lanes:-1} == "$threads" ]] || die 'native graph emitted the wrong thread count'
    gawk -F '\t' '$4 ~ /\.dut\.i_sram\.i_tc_sram_wrapper\[0\]\.i_tc_sram\.sram$/ { if ($2!=64 || $3!=33554432) exit 1; print "#define CVA6_GRAPH_SRAM memory" $1; count++ } END { if(count!=1) exit 1 }' "$output/graph/memories.tsv" > "$output/native_memory.h"
    "$CXX" -std=c++23 -O2 -g0 -I"$output/graph" -I"$output" -I"$ROOT/ordinary" -I"$REPO/include" -I"$FESVR/include" "$ROOT/ordinary/run_cpphdl_testharness.cpp" "$SCRIPT_DIR/run_native_graph_model.cpp" "$output/graph/"model.part.*.cpp "$FESVR/lib/libfesvr.a" -pthread -latomic -o "$output/run"
}

build_model() {
    local mode=$1 output="$ROOT/$1" binary signature started seconds state=fresh
    require_source
    if [[ "$mode" == verilator* ]]; then prepare_verilator; fi
    if [[ "$mode" != hdlcpp && ( ! -f "$FESVR/lib/libfesvr.a" || ! -f "$FESVR/include/fesvr/dtm.h" ) ]]; then
        prepare_verilator
        stage fesvr bash "$SCRIPT_DIR/build.sh" fesvr
    fi
    if [[ "$mode" != hdlcpp && "$mode" != verilator* ]]; then
        # Build the shared conversion on demand. A forced comparison already
        # rebuilt it once before entering the backend loop.
        if ! (ready hdlcpp) 2>/dev/null; then build_model hdlcpp; fi
    fi
    signature=$(fingerprint "$mode")
    binary="$output/run"
    [[ "$mode" != verilator* ]] || binary="$output/Variane_testharness"
    if [[ ${CVA6_REBUILD:-0} != 1 && -f "$ROOT/stamps/$mode" && $(head -1 "$ROOT/stamps/$mode") == "$signature" ]]; then
        if tail -n +2 "$ROOT/stamps/$mode" | sha256sum --check --status; then echo "$mode: verified cached build"; return; fi
    fi
    rm -f -- "$ROOT/stamps/$mode"
    # A forced comparison measures compilation without reusing partial objects.
    if [[ ${CVA6_REBUILD:-0} == 1 ]]; then
        [[ ! -d "$ROOT/cache/$mode" ]] || rm -r -- "$ROOT/cache/$mode"
    elif [[ -d "$ROOT/cache/$mode/$signature" ]]; then
        state=resumed
    fi
    started=$EPOCHREALTIME
    if [[ "$mode" == hdlcpp ]]; then
        [[ ! -e "$ROOT/ordinary" ]] || rm -r -- "$ROOT/ordinary"
        stage convert bash "$SCRIPT_DIR/convert.sh"
    else
        [[ ! -e "$output" ]] || rm -r -- "$output"
        mkdir -p "$output"
        case "$mode" in
            verilator|verilator-4threads) verilator_model "$mode" "$output" ;;
            cpphdl-native-graph) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" native "$output" 1 ;;
            cpphdl-native-graph-4threads) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" native "$output" 4 ;;
            cpphdl|cpphdl-optimize-combs) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" cpp "$mode" "$signature" ;;
            *) die "unknown build $mode" ;;
        esac
    fi
    seconds=$(gawk -v start="$started" -v end="$EPOCHREALTIME" 'BEGIN {printf "%.6f",end-start}')
    [[ $(fingerprint "$mode") == "$signature" ]] || die 'build inputs changed during compilation; rerun the build'
    {
        echo "$signature"
        if [[ "$mode" == hdlcpp ]]; then find "$ROOT/ordinary" -name '*.h' -type f -print0 | sort -z | xargs -0 sha256sum; else sha256sum "$binary"; fi
    } > "$ROOT/stamps/$mode"
    # Tie timing to the exact successful build; a cache hit never becomes a new
    # compilation measurement. Resumed builds are labelled separately.
    printf '%s\t%s\t%s\n' "$signature" "$seconds" "$state" > "$ROOT/stamps/$mode.time"
    if [[ "$mode" == verilator* ]]; then
        find "$output" -mindepth 1 -maxdepth 1 ! -name Variane_testharness -exec rm -r -- {} +
    elif [[ "$mode" == *native-graph* ]]; then
        find "$output/graph" -type f ! -name '*.json' ! -name '*.log' ! -name '*.tsv' -delete
    elif [[ "$mode" != hdlcpp ]]; then
        rm -r -- "$output/work" "$ROOT/cache/$mode"
    fi
    echo "$mode: build complete"
}

collect_group() {
    local label=$1; shift
    local aliases=("$@") target="$CACHE/$label.bin" source="$WORK/$label.cpp" status midpoint
    # Cached collections survive a failed build; their frontend source must too.
    {
            echo '#include "cpphdl_comb_optimization_context.h"'
            for alias in "${aliases[@]}"; do
                if [[ ${opaque[$alias]:-0} == 1 ]]; then
                    echo "$alias* cpphdlCombsOpaque$alias = nullptr;"
                else
                    echo "static_assert(sizeof($alias) != 0);"
                    echo "$alias* cpphdlCombsCollect$alias = nullptr;"
                fi
            done
    } > "$source"
    if [[ -f "$target.sha256" ]] && sha256sum --check --status "$target.sha256"; then collections+=("--optimize-combs-load=$target"); return; fi
    if [[ ! -f "$target.split" ]]; then
        status=0
        (if ((${#aliases[@]} > 1)); then ulimit -v 3906250; fi
         "$CPPHDL" --optimize-combs cpphdl_opt_t0 --optimize-combs-collect="$target.tmp" "$source" -- -std=c++26 -w -I"$REPO/include" -I"$WORK") > "$CACHE/$label.log" 2>&1 || status=$?
        if (( status==0 )); then
            mv "$target.tmp" "$target"
            sha256sum "$target" > "$target.sha256"
            collections+=("--optimize-combs-load=$target")
            return
        fi
        rm -f -- "$target.tmp"
        if ((${#aliases[@]} < 2)) || ! grep -qi 'out of memory' "$CACHE/$label.log"; then cat "$CACHE/$label.log" >&2; return "$status"; fi
        touch "$target.split"
    fi
    midpoint=$((${#aliases[@]} / 2))
    collect_group "$label-0" "${aliases[@]:0:midpoint}"
    collect_group "$label-1" "${aliases[@]:midpoint}"
}

optimize_combs() {
    local alias primary group=0
    local -a aliases=() batch=() collections=()
    declare -A opaque=()
    {
        echo '#pragma once'
        echo '#include "all_generated.h"'
        grep '^using cpphdl_opt_t' "$WORK/cpphdl_optimized_externs.h"
    } > "$WORK/cpphdl_comb_optimization_context.h"
    while read -r alias primary; do
        aliases+=("$alias")
        case "$primary" in cva6|cva6_rvfi|decoder|csr_regfile|trigger_module) opaque[$alias]=1 ;; esac
    done < <(sed -n 's/^using \(cpphdl_opt_t[0-9]*\) = \([a-zA-Z0-9_]*\).*/\1 \2/p' "$WORK/cpphdl_optimized_externs.h")
    [[ ${aliases[0]} == cpphdl_opt_t0 ]] || die 'missing root alias'
    for alias in "${aliases[@]:1}"; do
        if [[ ${opaque[$alias]:-0} == 1 ]]; then collect_group "opaque-$alias" "$alias"; fi
    done
    for alias in "${aliases[@]:1}"; do
        [[ ${opaque[$alias]:-0} != 1 ]] || continue
        batch+=("$alias")
        if ((${#batch[@]}==16)); then collect_group "collect-$group" "${batch[@]}"; batch=(); ((group+=1)); fi
    done
    if ((${#batch[@]})); then collect_group "collect-$group" "${batch[@]}"; fi
    collect_group root cpphdl_opt_t0
    export CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK=1000
    "$CPPHDL" --optimize-combs cpphdl_opt_t0 "${collections[@]}" --generated-dir "$WORK" "$WORK/root.cpp" -- -std=c++26 -w -I"$REPO/include" -I"$WORK"
}

compile_group() {
    local optimization=$1 label=$2; shift 2
    local pieces=("$@") source="$WORK/$label.cpp" key target status=0 midpoint
    {
        for piece in "${pieces[@]}"; do printf '#line 1 "%s"\n' "${piece##*/}"; cat "$piece"; echo; done
    } > "$source"
    key=$({ printf '%s\n' "$optimization"; sha256sum "$source"; } | sha256sum | cut -d ' ' -f1)
    target="$CACHE/$key.o.gz"
    if [[ -f "$target.sha256" ]] && sha256sum --check --status "$target.sha256"; then objects+=("$target"); return; fi
    if [[ ! -f "$target.split" ]]; then
        (if ((${#pieces[@]}>1)) || [[ "$optimization" == -O2 && "$label" == unit-* ]]; then ulimit -v 3906250; fi
         export MALLOC_ARENA_MAX=${MALLOC_ARENA_MAX:-1} MALLOC_TRIM_THRESHOLD_=${MALLOC_TRIM_THRESHOLD_:-0} MALLOC_MMAP_THRESHOLD_=${MALLOC_MMAP_THRESHOLD_:-65536}
         "$CXX" "${flags[@]}" "$optimization" -c "$source" -o "$WORK/object.o") > "$CACHE/$label.log" 2>&1 || status=$?
        if ((status==0)); then
            gzip -1c "$WORK/object.o" > "$target.tmp"
            mv "$target.tmp" "$target"
            sha256sum "$target" > "$target.sha256"
            rm -- "$WORK/object.o"
            objects+=("$target")
            return
        fi
        grep -qi 'out of memory' "$CACHE/$label.log" || { cat "$CACHE/$label.log" >&2; return "$status"; }
        touch "$target.split"
    fi
    if ((${#pieces[@]}==1)); then
        local original=${pieces[0]} declarations
        declarations=$(grep -c '^template ' "$original" || true)
        (( declarations>1 )) || { cat "$CACHE/$label.log" >&2; return 1; }
        grep -q 'cpphdl_optimized_root_' "$original" && die "cannot split root wrapper: $original"
        mkdir -p "$WORK/$label-parts"
        gawk -v directory="$WORK/$label-parts" '/^template / { seen=1; target=directory "/" count++ ".cpp"; print prefix $0 > target; close(target); next } !seen { prefix=prefix $0 "\n" }' "$original"
        pieces=("$WORK/$label-parts/"*.cpp)
    fi
    midpoint=$((${#pieces[@]} / 2))
    compile_group "$optimization" "$label-0" "${pieces[@]:0:midpoint}"
    compile_group "$optimization" "$label-1" "${pieces[@]:midpoint}"
}

cpp_model() {
    local mode=$1 signature=$2 output="$ROOT/$1" source name alias index=0
    WORK="$output/work"
    CACHE="$ROOT/cache/$mode/$signature"
    mkdir -p "$CACHE"
    cp -a "$ROOT/ordinary" "$WORK"
    cd "$WORK"
    HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE=24 HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE=200000 "$HDLCPP" --optimize run_cpphdl_testharness_optimize_seed.cpp
    local flags=(-std=c++23 -g0 -w -mcmodel=medium -fno-strict-aliasing -I"$REPO/include" -I"$WORK" -I"$FESVR/include" -fno-crash-diagnostics)
    if [[ "$mode" == cpphdl-optimize-combs ]]; then optimize_combs; fi
    {
        echo '#include "cpphdl_optimized_externs.h"'
        echo '#include <cstdio>'
        echo '#include <typeinfo>'
        echo 'int main() {'
        sed -n 's/^using \(cpphdl_opt_t[0-9]*\) = .*/std::puts(typeid(\1).name());/p' cpphdl_optimized_externs.h
        echo '}'
    } > canonical.cpp
    "$CXX" "${flags[@]}" -O0 canonical.cpp -o canonical
    ./canonical > canonical.types
    sed -n 's/^using \(cpphdl_opt_t[0-9]*\) = .*/\1/p' cpphdl_optimized_externs.h > canonical.aliases
    paste canonical.aliases canonical.types > canonical.tsv
    mkdir -p parts
    gawk -v directory="$WORK/parts" '
        ARGIND==1 { identity[$1]=$2; next }
        FNR==1 { prefix=""; source=FILENAME; sub(/^.*\//,"",source) }
        /^template / {
            match($0, /cpphdl_opt_t[0-9]+/, found)
            signature=substr($0,RSTART+RLENGTH)
            phase=($0 ~ /^template void / && $0 !~ /::_assign\(/) ? "cycle" : "init"
            key=phase identity[found[0]] signature
            if (!seen[key]++) {
                target=directory "/" phase "-" source
                if (!opened[target]++) printf "%s",prefix > target
                print > target
            }
            next
        }
        { prefix=prefix $0 "\n" }
    ' canonical.tsv $(grep -L 'cpphdl_optimized_root_' cpphdl_optimized_inst_*.cpp)
    local -a objects=() runtime=()
    for source in run_cpphdl_testharness_model_*.cpp; do
        if [[ "$mode" == cpphdl-optimize-combs && ( "$source" == *_bind.cpp || "$source" == *_cycle.cpp ) ]]; then continue; fi
        runtime+=("$WORK/$source")
    done
    if [[ "$mode" == cpphdl-optimize-combs ]]; then runtime+=("$WORK/run_cpphdl_testharness_optimized_bind.cpp" "$WORK/run_cpphdl_testharness_optimized_cycle.cpp"); fi
    compile_group -O2 runner "$WORK/run_cpphdl_testharness.cpp"
    compile_group -O2 runtime "${runtime[@]}"
    if [[ "$mode" == cpphdl-optimize-combs ]]; then
        for source in "$WORK/"cpphdl_opt_t0_optimized_combs*.cpp; do compile_group -O2 "comb-$index" "$source"; ((index+=1)); done
    fi
    for source in $(grep -l 'cpphdl_optimized_root_' cpphdl_optimized_inst_*.cpp); do
        if grep -Eq 'root_(create|assign)' "$source"; then optimization=-O0; else optimization=-O2; fi
        compile_group "$optimization" "root-${source%.cpp}" "$WORK/$source"
    done
    local phase optimization
    local -a batch=()
    for phase in init cycle; do
        optimization=-O0; [[ "$phase" == init ]] || optimization=-O2
        for source in "$WORK/parts/$phase-"*.cpp; do
            batch+=("$source")
            if ((${#batch[@]}==4)); then compile_group "$optimization" "unit-$index" "${batch[@]}"; batch=(); ((index+=1)); echo "compiled $phase batch $index"; fi
        done
        if ((${#batch[@]})); then compile_group "$optimization" "unit-$index" "${batch[@]}"; batch=(); ((index+=1)); fi
    done
    printf '%s\n' "${objects[@]}" > objects.list
    "$CXX" -std=c++17 -O2 "$SCRIPT_DIR/link_objects.cpp" -o "$WORK/link-objects"
    "$WORK/link-objects" "$WORK/objects.list" "$CXX" "${flags[@]}" "$FESVR/lib/libfesvr.a" -pthread -latomic -Wl,-s -o "$output/run"
}

buffer_output() {
    local output status=0
    # This fixed workload emits text. Capture validation through a blocking
    # pipe, then check the log write: a transient /tmp write failure must not
    # silently discard individual commit records. Timed runs stay unbuffered.
    output=$("$@" 2>&1) || status=$?
    printf '%s\n' "$output" || return 1
    return "$status"
}

check_run() {
    local mode=$1 log=$2
    gawk -v mode="$mode" '
        /^CVA6_MMUL / { workload++; if ($0!="CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5") bad=1 }
        /^PASSED\r?$/ { passed++ }
        /\*\*\* SUCCESS \*\*\*/ {
            success++
            if (!match($0,/\*\*\* SUCCESS \*\*\* \(tohost = 0\) after ([0-9]+) cycles$/,parts)) bad=1
            success_cycles=parts[1]
        }
        /^CVA6_BENCH / {
            stats++
            if (!match($0,/^CVA6_BENCH reset_cycles=10 work_cycles=([0-9]+) total_cycles=([0-9]+) work_seconds=([0-9]+\.[0-9]+)$/,parts)) bad=1
            work=parts[1]; total=parts[2]; seconds=parts[3]
        }
        /^CVA6_GRAPH_EDGES / {
            edges++
            if (!match($0,/^CVA6_GRAPH_EDGES total_cycles=([0-9]+)$/,parts)) bad=1
            graph_cycles=parts[1]
        }
        END {
            graph=(mode ~ /native-graph/)
            if (bad || workload!=1 || passed!=1 || success!=1 || stats!=1 || edges!=graph ||
                work<=0 || total!=10+work || total!=success_cycles || seconds<=0 ||
                (graph && graph_cycles!=total)) exit 1
            print seconds, total
        }
    ' "$log"
}

normalize_commits() {
    local mode=$1 log=$2
    [[ "$mode" != verilator* ]] || mode=verilator
    gawk -v mode="$mode" '
        mode=="verilator" && match($0,/^[0-3] 0x([0-9a-fA-F]+) \(0x([0-9a-fA-F]+)\)(.*)$/,fields) {
            destination=0; value=0
            if (match(fields[3],/ x[ ]*([0-9]+) 0x([0-9a-fA-F]+)/,reg)) { destination=reg[1]+0; if(destination) value=strtonum("0x" reg[2]) }
            printf "%08x %08x %d %08x\n",strtonum("0x" fields[1]),strtonum("0x" fields[2]),destination,value; count++
        }
        mode!="verilator" && /^CPPCOMMIT / {
            if (!match($0,/^CPPCOMMIT cycle=[0-9]+ ordinal=([0-9]+) lane=[0-9]+ order=[0-9]+ pc=0x([0-9a-fA-F]+) next=0x[0-9a-fA-F]+ insn=0x([0-9a-fA-F]+) trap=([0-9]+) cause=0x[0-9a-fA-F]+ rd=x([0-9]+) data=0x([0-9a-fA-F]+)$/,fields)) { bad=1; next }
            if (fields[1]!=count+1 || fields[4]!=0 || fields[5]+0>31) bad=1
            destination=fields[5]+0
            printf "%08x %08x %d %08x\n",strtonum("0x" fields[2]),strtonum("0x" fields[3]),destination,destination ? strtonum("0x" fields[6]) : 0; count++
        }
        END { if (bad || count==0) exit 1 }
    ' "$log"
}

ready() {
    local mode=$1
    [[ -f "$ROOT/stamps/$mode" ]] || die "run .build_cva6_$mode.sh first"
    [[ $(head -1 "$ROOT/stamps/$mode") == "$(fingerprint "$mode")" ]] || die "$mode build is stale"
    tail -n +2 "$ROOT/stamps/$mode" | sha256sum --check --status || die "$mode binary changed"
}

simulation_affinity() {
    local mode=$1 limit=1 override=${CVA6_CPU:-}
    if [[ "$mode" == *4threads ]]; then
        limit=4
        override=${CVA6_CPUS4:-}
    fi
    if [[ -n "$override" ]]; then
        printf '%s\n' "$override"
        return
    fi
    # Respect the caller's cpuset, including non-contiguous CPU ranges.
    awk -v limit="$limit" '/^Cpus_allowed_list:/ {
        split($2, ranges, ",")
        for (i=1; i<=length(ranges) && count<limit; i++) {
            fields=split(ranges[i], endpoints, "-")
            last=(fields==1 ? endpoints[1] : endpoints[2])
            for (cpu=endpoints[1]; cpu<=last && count<limit; cpu++) {
                printf "%s%d", (count++ ? "," : ""), cpu
            }
        }
        print ""
    }' /proc/self/status
}

simulate() {
    local mode=$1 phase=$2 affinity variable max=${3:-$MAX_CYCLES}
    affinity=$(simulation_affinity "$mode")
    mkdir -p "$ROOT/runs/$mode/$phase"
    cd "$ROOT/runs/$mode/$phase"
    for variable in "${!CPPHDL_@}" "${!HDLCPP_@}" "${!CVA6_@}" "${!NATIVE_@}"; do [[ -z "$variable" ]] || unset "$variable"; done
    unset LD_PRELOAD
    export CVA6_BENCH_STATS=1
    if [[ "$mode" == verilator* ]]; then
        exec taskset -c "$affinity" "$ROOT/$mode/Variane_testharness" --seed=1 --max-cycles="$max" "$ELF" "+elf_file=$ELF"
    fi
    [[ "$phase" != validation ]] || export CPPHDL_TRACE_COMMITS=1
    exec taskset -c "$affinity" "$ROOT/$mode/run" "$ELF" "$max"
}

run_stats() {
    local mode=$1 label=$2
    check_run "$mode" "$ROOT/logs/$label.log" ||
        die "$mode output/cycle mismatch (see $ROOT/logs/$label.log)"
}

run_model() {
    local mode=$1 seconds cycles commits stats baseline
    require_source
    if [[ "$mode" != verilator* ]]; then ready hdlcpp; fi
    ready "$mode"
    [[ -f "$ROOT/stamps/elf" ]] && tail -n +2 "$ROOT/stamps/elf" | sha256sum --check --status ||
        die 'matmul ELF missing or changed; run .build_cva6.sh'
    if [[ "$mode" == verilator ]]; then
        stage run-verilator bash "$SCRIPT_DIR/build.sh" simulate verilator timed
        stats=$(run_stats verilator run-verilator)
        read -r seconds cycles <<< "$stats"
        normalize_commits verilator "$ROOT/runs/verilator/timed/trace_rvfi_hart_00.dasm" > "$ROOT/reference.commits" || die 'Verilator commit trace is incomplete or malformed'
        printf '%s\n' "$cycles" > "$ROOT/reference.cycles"
        sha256sum "$ELF" "$ROOT/verilator/Variane_testharness" "$ROOT/reference.commits" "$ROOT/reference.cycles" > "$ROOT/reference.sha256"
    else
        ready verilator
        if [[ ! -f "$ROOT/reference.sha256" ]] || ! sha256sum --check --status "$ROOT/reference.sha256"; then run_model verilator; fi
        baseline=$(<"$ROOT/reference.cycles")
        if [[ "$mode" == verilator-4threads ]]; then
            stage "run-$mode" bash "$SCRIPT_DIR/build.sh" simulate "$mode" timed "$baseline"
            normalize_commits "$mode" "$ROOT/runs/$mode/timed/trace_rvfi_hart_00.dasm" > "$ROOT/candidate.commits" || die "$mode commit trace is incomplete or malformed"
        else
            stage "validate-$mode" bash "$SCRIPT_DIR/build.sh" buffer bash "$SCRIPT_DIR/build.sh" simulate "$mode" validation "$baseline"
            stats=$(run_stats "$mode" "validate-$mode")
            read -r seconds cycles <<< "$stats"
            [[ "$cycles" == "$baseline" ]] || die "$mode validation took $cycles cycles, Verilator took $baseline"
            normalize_commits "$mode" "$ROOT/logs/validate-$mode.log" > "$ROOT/candidate.commits" || die "$mode commit log is incomplete or malformed"
        fi
        cmp "$ROOT/reference.commits" "$ROOT/candidate.commits" || die "$mode integer commit mismatch"
        rm -- "$ROOT/candidate.commits"
        if [[ "$mode" != verilator-4threads ]]; then
            rm -- "$ROOT/logs/validate-$mode.log"
            stage "run-$mode" bash "$SCRIPT_DIR/build.sh" simulate "$mode" timed "$baseline"
        fi
        stats=$(run_stats "$mode" "run-$mode")
        read -r seconds cycles <<< "$stats"
        [[ "$cycles" == "$baseline" ]] || die "$mode took $cycles cycles, Verilator took $baseline"
    fi
    commits=$(wc -l < "$ROOT/reference.commits")
    find "$ROOT/runs/$mode" -type f \( -name '*.dasm' -o -name '*.traces' \) -delete
    printf '%-36s %12.6f s  %d cycles  %d commits  PASS\n' "$mode" "$seconds" "$cycles" "$commits" | tee "$ROOT/runs/$mode/result.txt"
}

main() {
    local action=${1:-help}; shift || true
    if [[ " $* " == *' --help '* || "$action" == help ]]; then
        echo 'Fixed CVA6 cv32a6_imac_sv32, 32x32 matmul, one repeat.'
        echo 'Run .compare.sh for Verilator versus native graph, or start with .build_cva6.sh for separate steps.'
        echo 'Overrides: CVA6_SRC, CVA6_WORK_ROOT, RISCV, CVA6_CROSS_COMPILE, SPIKE_INSTALL_DIR.'
        echo 'Tools: HDLCPP, CPPHDL, CPPHDL_CXX, VERILATOR. Optional affinity: CVA6_CPU, CVA6_CPUS4.'
        return
    fi
    initialize
    case "$action" in
        verilator-tool) build_verilator_tool; return ;;
        fesvr) build_fesvr; return ;;
        native) native_graph "$@"; return ;;
        cpp) cpp_model "$@"; return ;;
        simulate) simulate "$@"; return ;;
        buffer) buffer_output "$@"; return ;;
    esac
    exec 9> "$ROOT/.lock"
    flock -n 9 || die "another CVA6 build/run owns $ROOT"
    case "$action" in
        prepare) (($#==0)) || die 'prepare accepts no arguments'; prepare ;;
        build) (($#==1)) || die 'expected one backend'; build_model "$1" ;;
        run) (($#==1)) || die 'expected one backend'; run_model "$1" ;;
        *) die "unknown action $action" ;;
    esac
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; fi
