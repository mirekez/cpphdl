#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPPHDL_CVA6_NATIVE_HARNESS="${CPPHDL_CVA6_NATIVE_HARNESS:-0}"
CPPHDL_CVA6_COMB_MODE="${CPPHDL_CVA6_COMB_MODE:-none}"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl_testharness}"
    RUNNER="run_cpphdl_testharness_opt"
else
    OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
    RUNNER="run_cpphdl_matrix_opt"
fi
case "$CPPHDL_CVA6_COMB_MODE" in
    none) FINAL_RUNNER="$RUNNER" ;;
    optimize-combs) FINAL_RUNNER="run_cpphdl_matrix_optimize_combs" ;;
    optimize-combs-l1) FINAL_RUNNER="run_cpphdl_matrix_optimize_combs_l1" ;;
    *) echo "invalid CPPHDL_CVA6_COMB_MODE: $CPPHDL_CVA6_COMB_MODE" >&2; exit 2 ;;
esac
JOBS="${JOBS:-1}"
# Clang optimizes the multi-megabyte concrete comb partitions several times
# faster than GCC while producing the same C++ ABI. Keep a GCC fallback for
# hosts without the toolchain used by the conversion tests.
DEFAULT_CXX="/home/me/scalepnr/.conda/bin/clang++"
if [[ ! -x "$DEFAULT_CXX" ]]; then
    DEFAULT_CXX="g++"
fi
CPPHDL_CXX="${CPPHDL_CXX:-$DEFAULT_CXX}"
# Constructor-only units build the complete hierarchy but run once before the
# simulation loop. Disable Clang optimization there while all cycle code keeps
# the requested -O2 flags supplied through CXXFLAGS.
if [[ "$(basename "$CPPHDL_CXX")" == clang++* ]]; then
    CPPHDL_CONSTRUCTOR_CXXFLAGS="${CPPHDL_CONSTRUCTOR_CXXFLAGS:--O0 -fno-inline}"
else
    CPPHDL_CONSTRUCTOR_CXXFLAGS="${CPPHDL_CONSTRUCTOR_CXXFLAGS:---param ggc-min-expand=1 --param ggc-min-heapsize=4096}"
fi
DEFAULT_CXXFLAGS="-std=c++23 -O2 -g0 -w -fno-asynchronous-unwind-tables -I/home/me/cpphdl/include -I$OUT"
if [[ "$(basename "$CPPHDL_CXX")" == "g++" ]]; then
    DEFAULT_CXXFLAGS+=" -fno-var-tracking -fno-var-tracking-assignments --param ggc-min-expand=5 --param ggc-min-heapsize=32768"
fi
# The converted native harness contains a very large statically allocated SRAM.
# x86-64's small model cannot link references beyond 2 GiB, while medium keeps
# code addressing compact and permits this workload's large data section.
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    SPIKE_DIR="${CPPHDL_SPIKE_DIR:-${RISCV:-${CVA6_SRC:-$SCRIPT_DIR/cva6}/tools/spike}}"
    DEFAULT_CXXFLAGS+=" -mcmodel=medium -I$SPIKE_DIR/include"
fi
CPPHDL_CXXFLAGS="${CPPHDL_CXXFLAGS:-$DEFAULT_CXXFLAGS}"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    DEFAULT_LDFLAGS="-L$SPIKE_DIR/lib -Wl,-rpath,$SPIKE_DIR/lib -lfesvr -lriscv -ldisasm"
    if compgen -G "$SPIKE_DIR/lib/libyaml-cpp.*" >/dev/null; then
        DEFAULT_LDFLAGS+=" -lyaml-cpp"
    fi
    DEFAULT_LDFLAGS+=" -pthread -latomic -lstdc++exp"
else
    DEFAULT_LDFLAGS="-lstdc++exp"
fi
CPPHDL_LDFLAGS="${CPPHDL_LDFLAGS:-$DEFAULT_LDFLAGS}"

if [[ ! -f "$OUT/Makefile.optimize" ]]; then
    CPPHDL_CVA6_NATIVE_HARNESS="$CPPHDL_CVA6_NATIVE_HARNESS" CPPHDL_OUT="$OUT" "$SCRIPT_DIR/convert.sh"
fi

mkdir -p "$OUT/build"
flags_stamp="$OUT/build/cxxflags.optimize"
build_signature="$CPPHDL_CXX $CPPHDL_CXXFLAGS"
if [[ ! -f "$flags_stamp" ]] || [[ "$(cat "$flags_stamp")" != "$build_signature" ]]; then
    rm -rf "$OUT/build/opt" "$OUT/$RUNNER"
    printf '%s' "$build_signature" > "$flags_stamp"
fi

base_objects_ready=0
if [[ -x "$OUT/$RUNNER" && -f "$OUT/build/opt/cpphdl_optimized_main.o" && \
      -f "$OUT/build/opt/cpphdl_optimized_inst_81.o" ]]; then
    base_objects_ready=1
fi
if [[ "$CPPHDL_CVA6_COMB_MODE" == "none" || "$base_objects_ready" == "0" ]]; then
    # Makefile.optimize normally retains its O0 constructor PCH and O2 cycle
    # PCH together (about 5.4 GiB).  Build the constructor partition first,
    # then replace the deleted O0 PCH prerequisite with an older completion
    # marker while the remaining O2 objects and runner are built.  Existing
    # constructor objects are newer than the marker, so make does not rebuild
    # them or attempt to include the marker as a PCH.
    rm -f "$OUT/build/opt/cpphdl_optimized_externs_o2.pch" \
          "$OUT/build/opt/cpphdl_optimized_externs_o0.pch" \
          "$OUT/build/opt/.cpphdl_o0_complete"
    constructor_line=$(make -s --no-print-directory -C "$OUT" \
        -f Makefile.optimize \
        --eval='cpphdl-print-constructor-objs:;@printf "%s\n" "$(CONSTRUCTOR_OBJS)"' \
        cpphdl-print-constructor-objs)
    read -r -a constructor_objects <<< "$constructor_line"
    if [[ ${#constructor_objects[@]} -eq 0 ]]; then
        echo "Makefile.optimize has no constructor objects" >&2
        exit 2
    fi
    make -C "$OUT" -f Makefile.optimize -j"$JOBS" \
        CXX="$CPPHDL_CXX" CXXFLAGS="$CPPHDL_CXXFLAGS" \
        CONSTRUCTOR_CXXFLAGS="$CPPHDL_CONSTRUCTOR_CXXFLAGS" \
        LDFLAGS="$CPPHDL_LDFLAGS" "${constructor_objects[@]}"
    rm -f "$OUT/build/opt/cpphdl_optimized_externs_o0.pch"
    o0_complete="$OUT/build/opt/.cpphdl_o0_complete"
    pch_reference="$OUT/cpphdl_optimized_externs.h"
    if [[ "$OUT/all_generated.h" -nt "$pch_reference" ]]; then
        pch_reference="$OUT/all_generated.h"
    fi
    touch -r "$pch_reference" "$o0_complete"
    make -C "$OUT" -f Makefile.optimize -j"$JOBS" \
        CXX="$CPPHDL_CXX" CXXFLAGS="$CPPHDL_CXXFLAGS" \
        CONSTRUCTOR_CXXFLAGS="$CPPHDL_CONSTRUCTOR_CXXFLAGS" \
        LDFLAGS="$CPPHDL_LDFLAGS" \
        PCH_O0=build/opt/.cpphdl_o0_complete "$RUNNER"
    rm -f "$o0_complete"
else
    echo "reusing completed hdlcpp concrete objects for $CPPHDL_CVA6_COMB_MODE"
fi

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "0" && \
      "$CPPHDL_CVA6_COMB_MODE" != "none" ]]; then
    CPPHDL_TOOL="${CPPHDL:-/home/me/cpphdl/build/cpphdl}"
    if [[ ! -x "$CPPHDL_TOOL" ]]; then
        echo "missing cpphdl optimizer: $CPPHDL_TOOL" >&2
        exit 2
    fi
    # The two baseline PCH variants consume roughly 5.4 GiB.  The concrete
    # objects and linked baseline are already complete, so release these
    # rebuildable caches before creating the comb optimizer's large temporary
    # context.  The mode build below recreates only its required O2 PCH.
    rm -f "$OUT/build/opt/cpphdl_optimized_externs_o2.pch" \
          "$OUT/build/opt/cpphdl_optimized_externs_o0.pch"
    cp "$SCRIPT_DIR/support/cpphdl/prepare_optimize_combs.py" "$OUT/"
    cp "$SCRIPT_DIR/support/cpphdl/run_optimize_combs.sh" "$OUT/"
    python3 "$OUT/prepare_optimize_combs.py" "$OUT" \
        --collection-chunk-size "${CPPHDL_COMB_COLLECTION_CHUNK_SIZE:-4}" \
        --collection-max-definition-bytes "${CPPHDL_COMB_COLLECTION_MAX_DEFINITION_BYTES:-250000}" \
        --collection-isolate-definition-bytes "${CPPHDL_COMB_COLLECTION_ISOLATE_DEFINITION_BYTES:-1000000000}"
    find "$OUT" -maxdepth 1 -type f \
        \( -name 'cpphdl_opt_t0_optimized_combs*' -o \
           -name 'CpphdlOptimizedRoot_optimized_combs*' \) -delete
    if [[ "$CPPHDL_CVA6_COMB_MODE" == "optimize-combs" ]]; then
        optimizer_mode=full
        mode_suffix=optimize_combs
    else
        optimizer_mode=l1
        mode_suffix=optimize_combs_l1
    fi
    CPPHDL="$CPPHDL_TOOL" CPPHDL_COMB_OPTIMIZER_MODE="$optimizer_mode" \
        bash "$OUT/run_optimize_combs.sh" "$OUT"

    python3 - "$OUT" "$mode_suffix" "$FINAL_RUNNER" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1])
suffix = sys.argv[2]
runner = sys.argv[3]
source = (out / "cpphdl_optimized_main.cpp").read_text()
marker = '#include "cpphdl_optimized_externs.h"\n'
if marker not in source:
    raise SystemExit("optimized runner include layout changed")
declarations = (
    "cpphdl_opt_t0* cpphdl_optimized_root_create();\n"
    "extern \"C\" void cpphdl_optimized_root_assign_abi(void*);\n"
    "const ariane_axi::req_t& cpphdl_optimized_root_noc_req(cpphdl_opt_t0&);\n"
    "void calc_all(cpphdl_opt_t0&, bool);\n"
    "void commit_optimized_regs(cpphdl_opt_t0&);\n"
    "extern \"C\" void cpphdl_optimized_bind_ports_abi(void*);\n"
)
source = source.replace(
    marker,
    '#include "cpphdl_opt_t0_optimized_combs_internal.h"\n' + declarations,
    1,
)
replacements = {
    "cpphdl_optimized_root_strobe(dut);": "commit_optimized_regs(dut);",
    "cpphdl_optimized_root_work(dut, !bool(reset_n));":
        "calc_all(dut, !bool(reset_n));",
}
for old, new in replacements.items():
    if old not in source:
        raise SystemExit(f"optimized runner lifecycle call changed: {old}")
    source = source.replace(old, new, 1)
assign = "cpphdl_optimized_root_assign_abi(&dut);"
if assign not in source:
    raise SystemExit("optimized runner root assignment changed")
source = source.replace(
    assign,
    assign + "\n    cpphdl_optimized_bind_ports_abi(&dut);",
    1,
)
(out / f"cpphdl_mode_main_{suffix}.cpp").write_text(source)

makefile = f'''include Makefile.optimize

MODE_SUFFIX := {suffix}
MODE_RUNNER := {runner}
MODE_BUILD := build/$(MODE_SUFFIX)
MODE_MAIN_SOURCE := cpphdl_mode_main_$(MODE_SUFFIX).cpp
MODE_MAIN_OBJ := $(MODE_BUILD)/cpphdl_mode_main.o
MODE_PCH := $(MODE_BUILD)/cpphdl_opt_t0_optimized_combs_internal.pch
MODE_PCH_USE := -DCPPHDL_USE_GENERATED_PCH -include-pch $(MODE_PCH)
MODE_COMB_SOURCES := $(sort $(wildcard cpphdl_opt_t0_optimized_combs*.cpp))
MODE_COMB_OBJS := $(patsubst %.cpp,$(MODE_BUILD)/%.o,$(MODE_COMB_SOURCES))
MODE_BASE_OBJS := $(filter-out build/opt/cpphdl_optimized_main.o,$(OBJS))

$(MODE_MAIN_OBJ): $(MODE_MAIN_SOURCE) cpphdl_opt_t0_optimized_combs.h $(MODE_PCH)
\t@mkdir -p $(dir $@)
\t$(CXX) $(CXXFLAGS) $(MODE_PCH_USE) $(DEPFLAGS) -c $< -o $@

$(MODE_PCH): cpphdl_opt_t0_optimized_combs_internal.h
\t@mkdir -p $(dir $@)
\t$(CXX) $(CXXFLAGS) -x c++-header $< -o $@

$(MODE_BUILD)/%.o: %.cpp cpphdl_opt_t0_optimized_combs.h $(MODE_PCH)
\t@mkdir -p $(dir $@)
\t$(CXX) $(CXXFLAGS) $(MODE_PCH_USE) $(DEPFLAGS) -c $< -o $@

$(MODE_RUNNER): $(MODE_MAIN_OBJ) $(MODE_COMB_OBJS)
\t@test -n "$(MODE_BASE_OBJS)"
\t@test -f $(firstword $(MODE_BASE_OBJS))
\t$(CXX) $(CXXFLAGS) $^ $(MODE_BASE_OBJS) -o $@ $(LDFLAGS)
'''
(out / f"Makefile.{suffix}").write_text(makefile)
PY
    rm -rf "$OUT/build/$mode_suffix" "$OUT/$FINAL_RUNNER"
    make -C "$OUT" -f "Makefile.$mode_suffix" -j"$JOBS" \
        CXX="$CPPHDL_CXX" CXXFLAGS="$CPPHDL_CXXFLAGS" \
        CONSTRUCTOR_CXXFLAGS="$CPPHDL_CONSTRUCTOR_CXXFLAGS" \
        LDFLAGS="$CPPHDL_LDFLAGS" "$FINAL_RUNNER"
fi
