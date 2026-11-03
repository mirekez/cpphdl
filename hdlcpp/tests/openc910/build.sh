#!/usr/bin/env bash
set -euo pipefail

die() { echo "OpenC910: $*" >&2; exit 1; }
initialize() {
    SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
    REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
    ROOT=$(realpath -m "${OPENC910_WORK_ROOT:-/tmp/openc910-bash-$UID}")
    [[ "$ROOT" == /tmp/* && "$ROOT" != /tmp/ && "$ROOT" != *[[:space:]]* ]] || die 'work root must be below /tmp without whitespace'
    SOURCE="$ROOT/source"
    REVISION=b91c90914c19f114d35c8f6b73408eb241ed847c
    CXX=${CPPHDL_CXX:-$REPO/.conda/bin/clang++}
    HDLCPP=${HDLCPP:-$REPO/hdlcpp/build/hdlcpp}
    CPPHDL=${CPPHDL:-$REPO/build/cpphdl}
    VERILATOR=${VERILATOR:-$HOME/cva6/tools/verilator-new/bin/verilator}
    CROSS=${OPENC910_CROSS:-$HOME/riscv/bin/riscv32-unknown-elf-}
    export HDLCPP CPPHDL
    CPU=${OPENC910_CPU:-2}
    CPUS4=${OPENC910_CPUS4:-2,3,4,5}
    export CODE_BASE_PATH="$SOURCE/C910_RTL_FACTORY" CCACHE_DISABLE=1
    export OPENC910_WORK_ROOT="$ROOT" TMPDIR="$ROOT/tmp"
    mkdir -p "$ROOT/tmp" "$ROOT/logs" "$ROOT/stamps"
}

stage() {
    local label=$1 child status=0 resident ram_limit disk; shift
    echo "+ $label (log: $ROOT/logs/$label.log)"
    disk=$("$ROOT/tools/available-space" "$ROOT")
    (( disk >= 262144 )) || die 'less than 256 MiB writable temporary space (including user quota)'
    # Allow this build to use the machine's full physical RAM. Measure the
    # process group rather than reserving 1 GiB of system-wide free memory:
    # unrelated jobs otherwise stop a valid lowering below its own limit.
    # Address-space reservations are not resident RAM, so do not cap them.
    ram_limit=$(awk '/MemTotal:/ {print $2}' /proc/meminfo)
    echo "  resident RAM limit: $ram_limit KiB (full physical memory)"
    setsid bash -c 'ulimit -c 0; ulimit -v unlimited; ulimit -s 262144; ulimit -f 786432; exec timeout --kill-after=5s 86400 "$@"' bash "$@" > "$ROOT/logs/$label.log" 2>&1 &
    child=$!
    trap 'kill -- -"$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true' EXIT
    while kill -0 "$child" 2>/dev/null; do
        resident=$(ps -eo pgid=,rss= | awk -v group="$child" '$1 == group {sum += $2} END {print sum+0}')
        disk=$("$ROOT/tools/available-space" "$ROOT")
        if (( resident > ram_limit || disk < 262144 )); then
            printf 'OpenC910 resource guard: resident RAM=%s KiB; RAM limit=%s KiB; writable temporary space=%s KiB\n' "$resident" "$ram_limit" "$disk" >&2
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

prepare() {
    if [[ ! -d "$SOURCE/.git" ]]; then
        git clone --filter=blob:none --no-checkout --depth 1 https://github.com/XUANTIE-RV/openc910.git "$SOURCE"
        git -C "$SOURCE" sparse-checkout set C910_RTL_FACTORY/gen_rtl C910_RTL_FACTORY/setup smart_run
        git -C "$SOURCE" fetch --depth 1 origin "$REVISION"
        git -C "$SOURCE" checkout --detach "$REVISION"
    fi
    [[ $(git -C "$SOURCE" rev-parse HEAD) == "$REVISION" ]] || die 'unexpected source revision'
    # The original smart_run harness holds hart 1 in reset: one active core.
    # Preserve that RTL configuration in both toolchains.
    mkdir -p "$ROOT/elf" "$ROOT/rtl" "$ROOT/tools" "$SOURCE/smart_run/work"
    "$CXX" -std=c++17 -O2 "$SCRIPT_DIR/available_space.cpp" -o "$ROOT/tools/available-space"
    local flags=(-O2 -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -msmall-data-limit=0 -ffreestanding -fno-builtin -nostdlib -nostartfiles)
    "${CROSS}g++" "${flags[@]}" -fno-exceptions -fno-rtti -DMATRIX_SIZE=32 -DMATRIX_REPEATS=1 -c "$SCRIPT_DIR/../cva6/matrix_multiply_large.cpp" -o "$ROOT/elf/matrix.o"
    "${CROSS}gcc" "${flags[@]}" -c "$SCRIPT_DIR/runtime.c" -o "$ROOT/elf/runtime.o"
    "${CROSS}gcc" "${flags[@]}" -c "$SCRIPT_DIR/start.S" -o "$ROOT/elf/start.o"
    "${CROSS}gcc" "${flags[@]}" -Wl,--no-relax -T "$SCRIPT_DIR/link.ld" "$ROOT/elf/start.o" "$ROOT/elf/matrix.o" "$ROOT/elf/runtime.o" -o "$ROOT/elf/matrix.elf"
    "${CROSS}objcopy" -O binary "$ROOT/elf/matrix.elf" "$ROOT/elf/matrix.bin"
    # The upstream loader interprets each printed word as four bytes in file order.
    od -An -v -t x1 "$ROOT/elf/matrix.bin" | gawk -v root="$ROOT/elf" '
        {for(i=1;i<=NF;i++) bytes[count++]=$i}
        END {if(count>524288) exit 1; for(word=0;word<131072;word++) {
            file=root (word<65536 ? "/inst.pat" : "/data.pat");
            for(byte=0;byte<4;byte++) printf "%s",(word*4+byte<count ? bytes[word*4+byte] : "00") > file;
            print "" > file;
        }}'
    sha256sum "$ROOT/elf/matrix.elf" "$ROOT/elf/matrix.bin" "$ROOT/elf/inst.pat" "$ROOT/elf/data.pat" > "$ROOT/elf/image.sha256"
    rm -- "$ROOT/elf/"*.o
    # Expose observation ports. Initialize the host-only watchdog: the upstream
    # bench relies on randomized initial state, and otherwise fails at cycle 1
    # before its first reset pulse (cycle_count == 0, no retirements yet).
    gawk '
        /^  input wire clk$/ {
            print "  input wire clk,";
            print "  output wire bench_reset_n,";
            print "  output wire [2:0] bench_retire,";
            print "  output wire [39:0] bench_pc0, bench_pc1, bench_pc2"; next
        }
        /^endmodule/ {
            print "assign bench_reset_n = rst_b;";
            print "assign bench_retire = {`tb_retire2, `tb_retire1, `tb_retire0};";
            print "assign bench_pc0 = `retire0_pc;";
            print "assign bench_pc1 = `retire1_pc;";
            print "assign bench_pc2 = `retire2_pc;";
        }
        /reg \[31:0\] retire_inst_in_period;/ {sub(/;/, " = 0;")}
        /reg \[31:0\] cycle_count;/ {sub(/;/, " = 1;")}
        {print}' "$SOURCE/smart_run/logical/tb/tb_verilator.v" > "$ROOT/rtl/top.v"
    touch "$ROOT/prepared"
}

verilator_build() {
    local mode=$1 threads=1 output="$ROOT/$1"
    [[ "$mode" != *4threads ]] || threads=4
    mkdir -p "$output"
    cd "$SOURCE/smart_run/work"
    # Current Verilator uses -O3 for RTL optimization; keep upstream -Os for C++.
    "$VERILATOR" -O3 --no-timing --x-assign 0 -Wno-fatal --threads "$threads" --cc --exe --top-module top \
        -f "$SOURCE/smart_run/logical/filelists/ip.fl" -f "$SOURCE/smart_run/logical/filelists/smart.fl" \
        +incdir+"$SOURCE/smart_run/logical/tb" -y "$SOURCE/smart_run/logical/tb" +define+NO_DUMP \
        "$ROOT/rtl/top.v" "$SCRIPT_DIR/run_verilator.cpp" --Mdir "$output" --output-split 10000 --output-split-cfuncs 100 \
        -CFLAGS '-std=c++17'
    [[ -s "$output/Vtop_classes.mk" && -s "$output/Vtop__verFiles.dat" ]] || die 'Verilator generation is incomplete'
    make -C "$output" -f "$SOURCE/smart_run/logical/tb/Makefile_obj" -j1 "CXX=$CXX" Vtop
    grep -Eq "::threads\(\) const \\{ return ${threads}; \\}" "$output/Vtop.cpp" || die 'wrong Verilator thread count'
    # Keep generated units until validation succeeds so a harness fix can reuse
    # the unchanged objects. Successful runs remove these temporary files.
}

native_pch_signature() {
    {
        printf 'OpenC910 native PCH v1: C++23 SYNTHESIS, deferred templates, public observation fields\n'
        sha256sum "$CXX" "$ROOT/stamps/hdlcpp" "$SCRIPT_DIR/model_top.h" "$ROOT/ordinary/levels.tsv"
        find "$REPO/include" -type f -name '*.h' -print0 | sort -z | xargs -0 sha256sum
        # Clang validates imported header mtimes as well as their contents.
        # Regenerating identical headers must invalidate a retained PCH too.
        stat -c '%n %s %y' "$SCRIPT_DIR/model_top.h"
        find "$ROOT/ordinary/generated" "$REPO/include" -type f -name '*.h' -printf '%p %s %T@\n' | sort
    } | sha256sum
}

native_pch_stamp() {
    {
        native_pch_signature
        find "$ROOT/ordinary-pch" -type f \( -name '*.h' -o -name '*.pch' \) -print0 | sort -z | xargs -0 sha256sum
    } > "$ROOT/stamps/native-pch"
}

native_pch() {
    local output="$ROOT/ordinary-pch" signature previous= level header started=$EPOCHREALTIME
    signature=$(native_pch_signature)
    if [[ -s "$output/model.pch" && -f "$ROOT/stamps/native-pch" ]] &&
       [[ $(head -1 "$ROOT/stamps/native-pch") == "$signature" ]] &&
       tail -n +2 "$ROOT/stamps/native-pch" | sha256sum --check --status; then
        echo 'Reusing verified native frontend cache'
        return
    fi
    [[ ! -e "$output" ]] || rm -r -- "$output"
    mkdir -p "$output"
    local flags=(-std=c++23 -DSYNTHESIS -w -fno-pch-instantiate-templates -I"$REPO/include" -I"$ROOT/ordinary")
    local imported=()
    # A single OpenC910 AST retains too many unused C++ implementation bodies.
    # Chain headers in the converter's dependency order so each Clang invocation
    # sees completed children and serializes only the next level. Native graph
    # later imports the same declarations and loads bodies on demand.
    while read -r level; do
        header="$output/level-$level.h"
        {
            printf '#include "cpphdl.h"\n#define private public\n'
            gawk -F '\t' -v level="$level" '$1==level {print "#include \"generated/" $2 ".h\""}' "$ROOT/ordinary/levels.tsv"
            printf '#undef private\n'
        } > "$header"
        imported=()
        [[ -z "$previous" ]] || imported=(-include-pch "$previous")
        echo "Native frontend cache: dependency level $level"
        "$CXX" "${flags[@]}" "${imported[@]}" -x c++-header "$header" -o "$header.pch"
        previous="$header.pch"
    done < <(cut -f1 "$ROOT/ordinary/levels.tsv" | uniq)
    printf '#include "%s/model_top.h"\n' "$SCRIPT_DIR" > "$output/model.h"
    "$CXX" "${flags[@]}" -include-pch "$previous" -x c++-header "$output/model.h" -o "$output/model.pch"
    native_pch_stamp
    gawk -v start="$started" -v end="$EPOCHREALTIME" 'BEGIN {printf "%.6f\n",end-start}' > "$ROOT/stamps/native-pch.time"
}

native_graph_signature() {
    {
        printf 'OpenC910 final native graph v1: C++23 SYNTHESIS\n'
        sha256sum "$CPPHDL" "$CXX" "$ROOT/stamps/hdlcpp" "$SCRIPT_DIR/model_top.h" "$SCRIPT_DIR/native_graph_top.cpp"
        find "$REPO/include" -type f -name '*.h' -print0 | sort -z | xargs -0 sha256sum
    } | sha256sum
}

native_frontend() {
    local output="$ROOT/native-frontend" signature
    sha256sum --check --status "$ROOT/stamps/hdlcpp"
    signature=$(native_graph_signature)
    if [[ -s "$output/model.graph" && -f "$ROOT/stamps/native-frontend" ]] &&
       [[ $(head -1 "$ROOT/stamps/native-frontend") == "$signature" ]] &&
       tail -n +2 "$ROOT/stamps/native-frontend" | sha256sum --check --status; then
        echo 'Reusing verified native hardware graph'
        return
    fi
    native_pch
    [[ ! -e "$output" ]] || rm -r -- "$output"
    mkdir -p "$output"
    # Thread partitioning belongs to the standard graph emitter. Lower the same
    # C++ hierarchy once for both thread counts, retaining its RAMs and operators.
    CPPHDL_GRAPH_PROGRESS=1 "$CPPHDL" --lower-cpp-graph "$SCRIPT_DIR/native_graph_top.cpp" \
        "$output/model.graph" elaboration_root --final-native -- -std=c++23 \
        -I"$REPO/include" -DSYNTHESIS -w -I"$ROOT/ordinary" \
        -include-pch "$ROOT/ordinary-pch/model.pch"
    [[ -s "$output/model.graph" && "$signature" == "$(native_graph_signature)" ]] || die 'native frontend inputs changed or graph is missing'
    sha256sum --check --status "$ROOT/stamps/hdlcpp"
    { printf '%s\n' "$signature"; sha256sum "$output/model.graph"; } > "$ROOT/stamps/native-frontend"
    # The serialized hardware graph no longer needs Clang's AST cache. Release
    # it before emission and C++ compilation, which need the temporary quota.
    rm -r -- "$ROOT/ordinary-pch"
    rm -f -- "$ROOT/stamps/native-pch" "$ROOT/stamps/native-pch.time"
}

native_build() {
    local mode=$1 threads=1 output="$ROOT/$1" part index=0
    [[ "$mode" == cpphdl-native-graph || "$mode" == cpphdl-native-graph-4threads ]] || die 'invalid native backend'
    [[ "$mode" != *4threads ]] || threads=4
    native_frontend
    mkdir -p "$output"
    [[ ! -e "$output/graph" ]] || rm -r -- "$output/graph"
    mkdir -p "$output/graph"
    # Use the exact emitter and flags used by cpphdl --native-graph. Only the
    # serialized frontend result is shared; each backend gets its own schedule.
    "$CXX" -std=c++23 -O1 -I"$REPO/include" "$REPO/tools/cpphdl-graph-emit.cpp" -o "$output/graph/lower"
    "$output/graph/lower" "$ROOT/native-frontend/model.graph" "$output/graph/model.h" 512 "$threads" "$output/graph/memories.tsv" 64
    rm -- "$output/graph/lower"
    local lanes
    lanes=$(sed -n 's/.*__cpphdl_thread_count = \([0-9]*\);.*/\1/p' "$output/graph/model.h")
    [[ ${lanes:-1} == "$threads" ]] || die 'native graph emitted the wrong thread count'
    gawk -F '\t' '
        match($4,/\.dut\.x_axi_slave128\.x_f_spsram_large\.ram([0-9]+)\.mem$/,bank) {
            if ($2!=8 || $3!=2097152 || seen[bank[1]]++) exit 1;
            print "#define OPENC910_MEMORY_" bank[1] " memory" $1; count++
        }
        END {if(count!=16) exit 1}' "$output/graph/memories.tsv" > "$output/native_memory.h"
    local flags=(-std=c++23 -O2 -g0 -pthread -I"$SCRIPT_DIR" -I"$output/graph" -I"$output" -I"$REPO/include")
    local objects=()
    # Compile separately: linking every source in a single compiler invocation
    # retains temporary objects and obscures which unit exhausted resources.
    for part in "$SCRIPT_DIR/run_cpphdl.cpp" "$SCRIPT_DIR/model_native.cpp" "$output/graph/"model.part.*.cpp; do
        "$CXX" "${flags[@]}" -c "$part" -o "$output/part-$index.o"
        objects+=("$output/part-$index.o")
        ((index+=1))
    done
    "$CXX" "${objects[@]}" -pthread -latomic -o "$output/run"
    rm -- "${objects[@]}"
}

cpp_build() {
    local mode=$1 output="$ROOT/$1"
    [[ "$mode" == cpphdl || "$mode" == cpphdl-optimize-combs ]] || die 'invalid C++ backend'
    sha256sum --check --status "$ROOT/stamps/hdlcpp"
    [[ ! -e "$output" ]] || rm -r -- "$output"
    mkdir -p "$output"
    local flags=(-std=c++23 -O2 -g0 -pthread -I"$ROOT/ordinary" -I"$SCRIPT_DIR" -I"$output" -I"$REPO/include")
    local extra=()
    if [[ "$mode" == cpphdl-optimize-combs ]]; then
        CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK=1000 "$CPPHDL" \
            --optimize-combs OpenC910Model --generated-dir "$output" "$SCRIPT_DIR/combs_top.cpp" \
            -- -std=c++26 -w -I"$ROOT/ordinary" -I"$SCRIPT_DIR" -I"$REPO/include"
        flags+=(-DOPENC910_OPTIMIZE_COMBS)
        extra=("$output/"OpenC910Model_optimized_combs*.cpp)
    fi
    local source index=0 objects=()
    for source in "$SCRIPT_DIR/run_cpphdl.cpp" "$SCRIPT_DIR/model_default.cpp" "${extra[@]}"; do
        "$CXX" "${flags[@]}" -c "$source" -o "$output/part-$index.o"
        objects+=("$output/part-$index.o")
        ((index+=1))
    done
    "$CXX" "${objects[@]}" -pthread -latomic -o "$output/run"
    rm -- "${objects[@]}"
}

build_model() {
    local mode=$1 started=$EPOCHREALTIME seconds
    [[ -f "$ROOT/prepared" ]] || die 'run .build_openc910.sh first'
    rm -f -- "$ROOT/stamps/$mode" "$ROOT/stamps/$mode.time"
    case "$mode" in
        hdlcpp) stage conversion bash "$SCRIPT_DIR/convert.sh" ;;
        verilator|verilator-4threads) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" verilate "$mode" ;;
        cpphdl-native-graph|cpphdl-native-graph-4threads) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" native "$mode" ;;
        cpphdl|cpphdl-optimize-combs) stage "build-$mode" bash "$SCRIPT_DIR/build.sh" cpp "$mode" ;;
        *) die "unknown backend $mode" ;;
    esac
    seconds=$(gawk -v start="$started" -v end="$EPOCHREALTIME" 'BEGIN {printf "%.6f",end-start}')
    printf '%s\n' "$seconds" > "$ROOT/stamps/$mode.time"
    if [[ "$mode" == hdlcpp ]]; then
        find "$ROOT/ordinary" -name '*.h' -type f -print0 | sort -z | xargs -0 sha256sum > "$ROOT/stamps/$mode"
    elif [[ "$mode" == verilator* ]]; then
        sha256sum "$ROOT/$mode/Vtop" > "$ROOT/stamps/$mode"
    else
        sha256sum "$ROOT/$mode/run" > "$ROOT/stamps/$mode"
    fi
}

simulate() {
    local mode=$1 phase=$2 affinity=$CPU
    [[ "$mode" != *4threads ]] || affinity=$CPUS4
    mkdir -p "$ROOT/runs/$mode/$phase"
    cd "$ROOT/runs/$mode/$phase"
    # A failed or incomplete runner must not inherit an earlier PASS or trace.
    rm -f -- run_case.report retire.log
    ln -sf "$ROOT/elf/inst.pat" inst.pat
    ln -sf "$ROOT/elf/data.pat" data.pat
    unset LD_PRELOAD OPENC910_TRACE
    [[ "$phase" != validation ]] || export OPENC910_TRACE=1
    if [[ "$mode" == verilator* ]]; then
        exec taskset -c "$affinity" "$ROOT/$mode/Vtop"
    else
        exec taskset -c "$affinity" "$ROOT/$mode/run" "$ROOT/elf/matrix.bin"
    fi
}

run_model() {
    local mode=$1
    sha256sum --check --status "$ROOT/stamps/$mode" "$ROOT/elf/image.sha256"
    if [[ "$mode" != verilator ]]; then
        sha256sum --check --status "$ROOT/runs/verilator/reference.sha256" || die 'rerun original Verilator to establish a current reference'
    fi
    stage "validate-$mode" bash "$SCRIPT_DIR/build.sh" simulate "$mode" validation
    grep -qx 'TEST PASS' "$ROOT/runs/$mode/validation/run_case.report"
    grep -qx 'CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5' "$ROOT/logs/validate-$mode.log"
    stage "run-$mode" bash "$SCRIPT_DIR/build.sh" simulate "$mode" timed
    grep -qx 'TEST PASS' "$ROOT/runs/$mode/timed/run_case.report"
    grep -qx 'CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5' "$ROOT/logs/run-$mode.log"
    local validation timed
    validation=$(sed -n 's/^OPENC910_BENCH \(total_cycles=[0-9]* commits=[0-9]*\) work_seconds=.*/\1/p' "$ROOT/logs/validate-$mode.log")
    timed=$(sed -n 's/^OPENC910_BENCH \(total_cycles=[0-9]* commits=[0-9]*\) work_seconds=.*/\1/p' "$ROOT/logs/run-$mode.log")
    [[ -n "$validation" && "$validation" == "$timed" ]] || die 'traced and timed runs disagree'
    [[ -s "$ROOT/runs/$mode/validation/retire.log" ]] || die 'missing retirement trace'
    if [[ "$mode" != verilator ]]; then
        cmp "$ROOT/runs/verilator/validation/retire.log" "$ROOT/runs/$mode/validation/retire.log" || die 'retirement stream differs from original Verilator'
        [[ "$timed" == "$(cat "$ROOT/runs/verilator/work.txt")" ]] || die 'cycle or commit count differs from original Verilator'
    fi
    printf '%s\n' "$timed" > "$ROOT/runs/$mode/work.txt"
    if [[ "$mode" == verilator ]]; then
        sha256sum "$ROOT/verilator/Vtop" "$ROOT/elf/matrix.elf" "$ROOT/elf/matrix.bin" \
            "$ROOT/elf/inst.pat" "$ROOT/elf/data.pat" "$ROOT/runs/verilator/validation/retire.log" \
            "$ROOT/runs/verilator/work.txt" > "$ROOT/runs/verilator/reference.sha256"
    fi
    if [[ "$mode" == verilator* ]]; then
        find "$ROOT/$mode" -mindepth 1 -maxdepth 1 ! -name Vtop -exec rm -r -- {} +
    fi
    grep '^OPENC910_BENCH ' "$ROOT/logs/run-$mode.log"
}

main() {
    local action=${1:-help}; shift || true
    if [[ "$action" == help || " $* " == *' --help '* ]]; then
        echo 'Pinned one-core OpenC910, unchanged CVA6 32x32 integer matmul source.'
        echo 'Artifacts: OPENC910_WORK_ROOT=/tmp/openc910-bash-$UID. Build with .build_openc910.sh first.'
        return
    fi
    initialize
    case "$action" in
        verilate) verilator_build "$@"; return ;;
        native) native_build "$@"; return ;;
        cpp) cpp_build "$@"; return ;;
        simulate) simulate "$@"; return ;;
        compare-build) build_model "$@"; return ;;
        compare-run) run_model "$@"; return ;;
    esac
    exec 9> "$ROOT/.lock"
    flock -n 9 || die 'another OpenC910 build/run owns this work root'
    case "$action" in
        prepare) prepare ;;
        build) build_model "$@" ;;
        run) run_model "$@" ;;
        *) die "unknown action $action" ;;
    esac
}
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; exit; fi
