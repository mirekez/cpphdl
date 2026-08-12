#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${CVA6_SRC:-$SCRIPT_DIR/cva6}"
OUT="${CPPHDL_OUT:-$SCRIPT_DIR/cpphdl}"
SUPPORT="$SCRIPT_DIR/support/cpphdl"
TARGET="${TARGET:-cv32a6_imac_sv32}"
RISCV="${RISCV:-/home/me/riscv}"
JOBS="${JOBS:-1}"
HDLCPP_JOBS="${HDLCPP_JOBS:-1}"
# Large explicit-instantiation units make GCC retain several gigabytes of template state.
# Group small specializations by generated definition size while leaving large modules alone.
# This preserves the generated model and keeps the serial CVA6 build below the host memory limit.
HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE="${HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE:-24}"
HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE="${HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE:-200000}"
DEFAULT_HDLCPP="/home/me/cpphdl/hdlcpp/build/hdlcpp"
HDLCPP="${HDLCPP:-$DEFAULT_HDLCPP}"
CPPHDL="${CPPHDL:-/home/me/cpphdl/build/cpphdl}"
CPPHDL_CVA6_NATIVE_HARNESS="${CPPHDL_CVA6_NATIVE_HARNESS:-0}"
CPPHDL_CVA6_ONLY="${CPPHDL_CVA6_ONLY:-}"
CPPHDL_CVA6_FINALIZE_ONLY="${CPPHDL_CVA6_FINALIZE_ONLY:-0}"
CPPHDL_CVA6_RESUME_TRAITS="${CPPHDL_CVA6_RESUME_TRAITS:-0}"
CPPHDL_CVA6_KEEP_METADATA="${CPPHDL_CVA6_KEEP_METADATA:-0}"
CPPHDL_CVA6_SKIP_STALE_TRAIT_SCAN="${CPPHDL_CVA6_SKIP_STALE_TRAIT_SCAN:-0}"
CPPHDL_CVA6_SKIP_OPTIMIZE="${CPPHDL_CVA6_SKIP_OPTIMIZE:-0}"

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" && "${CPPHDL_OUT:-}" == "" ]]; then
    OUT="$SCRIPT_DIR/cpphdl_testharness"
fi

RUNNER="run_cpphdl_matrix.cpp"
if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
    RUNNER="run_cpphdl_testharness.cpp"
fi

if [[ ! -d "$SRC/core" ]]; then
    echo "missing CVA6 source core directory: $SRC/core" >&2
    exit 2
fi
if [[ ! -d "$SUPPORT/tools" ]]; then
    echo "missing conversion support directory: $SUPPORT" >&2
    exit 2
fi
hdlcpp_source_newer=0
if [[ "$HDLCPP" == "$DEFAULT_HDLCPP" && -x "$HDLCPP" ]] &&
   find /home/me/cpphdl/hdlcpp -maxdepth 1 -type f \
       \( -name '*.cc' -o -name '*.h' \) -newer "$HDLCPP" -print -quit | grep -q .; then
    hdlcpp_source_newer=1
fi
if [[ ! -x "$HDLCPP" || "$hdlcpp_source_newer" == "1" ]]; then
    make -C /home/me/cpphdl/hdlcpp/build -j"$JOBS" hdlcpp
fi
export HDLCPP

if [[ -z "$CPPHDL_CVA6_ONLY" && "$CPPHDL_CVA6_FINALIZE_ONLY" != "1" && "$CPPHDL_CVA6_RESUME_TRAITS" != "1" ]]; then
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp -a "$SUPPORT"/. "$OUT"/
    touch "$OUT/cva6_assign_suffix_code.tsv"
    python3 - "$OUT" <<'PY'
from pathlib import Path
import re
import sys

out = Path(sys.argv[1]).resolve()
for path in out.glob("*.tsv"):
    text = path.read_text()
    text = text.replace("/home/me/cva6/cpphdl", str(out))
    path.write_text(text)
PY
elif [[ ! -d "$OUT/generated" ]]; then
    echo "incremental conversion requires existing output: $OUT" >&2
    exit 2
else
    # Incremental optimization must use the maintained runner, not a stale copy
    # left in the output directory by an earlier full conversion.
    cp "$SUPPORT/$RUNNER" "$OUT/$RUNNER"
    cp "$SUPPORT/cva6_generate_param_values.tsv" "$OUT/cva6_generate_param_values.tsv"
    if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
        rm -f "$OUT/CpphdlOptimizedRoot.h" \
            "$OUT/CpphdlCombOptimizedRoot.h" \
            "$OUT/cpphdl_comb_optimized_externs.h"
        cp "$SUPPORT"/run_cpphdl_testharness_model* "$OUT"/
        cp "$SUPPORT"/run_cpphdl_testharness_optimized* "$OUT"/
        cp "$SUPPORT/run_cpphdl_testharness_optimize_seed.cpp" "$OUT"/
        cp "$SUPPORT/prepare_optimize_combs.py" "$OUT"/
        cp "$SUPPORT/run_optimize_combs.sh" "$OUT"/
        cp "$SUPPORT/run_optimize_combs_l1.sh" "$OUT"/
    fi
fi

HELPER_DIR="$SRC/.cpphdl_convert/tools"
mkdir -p "$HELPER_DIR"
cp "$SUPPORT/tools/convert_cva6.py" "$HELPER_DIR/convert_cva6.py"

SRC_CPPHDL="$SRC/cpphdl"
cleanup() {
    if [[ -L "$SRC_CPPHDL" ]]; then
        rm -f "$SRC_CPPHDL"
    fi
}
trap cleanup EXIT

if [[ -e "$SRC_CPPHDL" && ! -L "$SRC_CPPHDL" ]]; then
    echo "$SRC_CPPHDL exists and is not a symlink; remove it or set CVA6_SRC to a clean source tree" >&2
    exit 2
fi
rm -f "$SRC_CPPHDL"
ln -s "$OUT" "$SRC_CPPHDL"

(
    cd "$SRC"
    export HDLCPP_MODULE_PARAMS="$OUT/cva6_module_params.tsv"
    export HDLCPP_PORT_TYPES="$OUT/cva6_port_types.tsv"
    export HDLCPP_LINE_PATCHES="$OUT/cva6_line_patches.tsv"
    export HDLCPP_AGGREGATE_DEFAULTS="$OUT/cva6_aggregate_defaults.tsv"
    export HDLCPP_QUALIFIED_CALLS="$OUT/cva6_qualified_calls.tsv"
    export HDLCPP_TYPE_DECL_OVERRIDES="$OUT/cva6_type_decl_overrides.tsv"
    export HDLCPP_TYPE_PARAM_DEFAULTS="$OUT/cva6_type_param_defaults.tsv"
    export HDLCPP_TYPE_ALIAS_OVERRIDES="$OUT/cva6_type_alias_overrides.tsv"
    export HDLCPP_METHOD_BODY_OVERRIDES="$OUT/cva6_method_body_overrides.tsv"
    export HDLCPP_SKIP_PARAMS="${HDLCPP_SKIP_PARAMS:-cva6.AccCfg}"
    export HDLCPP_LOCAL_TYPE_MODULES="${HDLCPP_LOCAL_TYPE_MODULES:-cva6}"
    export HDLCPP_LOCAL_TYPE_NAMES="${HDLCPP_LOCAL_TYPE_NAMES:-branchpredict_sbe_t,exception_t,icache_areq_t,icache_arsp_t,icache_dreq_t,fetch_entry_t,jvt_t,scoreboard_entry_t,writeback_t,bp_resolve_t,irq_ctrl_t,lsu_ctrl_t,cbo_t,fu_data_t,icache_req_t,icache_rtrn_t,dcache_req_i_t,dcache_req_o_t,accelerator_req_t,accelerator_resp_t,acc_mmu_req_t,acc_mmu_resp_t,acc_cfg_t}"
    export HDLCPP_ADDRESSABLE_PACKED_ARRAY_TYPES="${HDLCPP_ADDRESSABLE_PACKED_ARRAY_TYPES:-branchpredict_sbe_t,exception_t,icache_areq_t,icache_arsp_t,icache_dreq_t,icache_drsp_t,fetch_entry_t,jvt_t,bht_prediction_t,scoreboard_entry_t,writeback_t,bp_resolve_t,irq_ctrl_t,lsu_ctrl_t,cbo_t,fu_data_t,icache_req_t,icache_rtrn_t,dcache_req_i_t,dcache_req_o_t,accelerator_req_t,accelerator_resp_t,acc_mmu_req_t,acc_mmu_resp_t,acc_cfg_t,forwarding_t,hpdcache_req_t,hpdcache_rsp_t,hpdcache_mem_req_t,hpdcache_mem_req_w_t,hpdcache_mem_resp_r_t,hpdcache_mem_resp_w_t,hpdcache_dir_entry_t,hpdcache_pma_t,hpdcache_rtab_deps_t,rtab_entry_t,hwpf_stride_base_t,hwpf_stride_param_t,hwpf_stride_throttle_t,hwpf_stride_status_t,pte_cva6_t,tlb_update_cva6_t,pmpcfg_t,instruction_t,ras_t}"
    export HDLCPP_FALSE_CONSTANT_TYPES="${HDLCPP_FALSE_CONSTANT_TYPES:-cva6.acc_cfg_t}"
    export HDLCPP_CONSTEXPR_GENERATE_MODULES="${HDLCPP_CONSTEXPR_GENERATE_MODULES:-cva6_fifo_v3,cva6_shared_tlb}"
    export HDLCPP_NAMED_PARAM_ORDER="$OUT/cva6_named_param_order.tsv"
    export HDLCPP_QUALIFY_IMPORTED_TYPE_PACKAGES="${HDLCPP_QUALIFY_IMPORTED_TYPE_PACKAGES:-cvxif_instr_pkg}"
    export HDLCPP_ENUM_WIDTH_PREFIXES="$OUT/cva6_enum_width_prefixes.tsv"
    export HDLCPP_SKIP_USING_NAMESPACE_IMPORTS="${HDLCPP_SKIP_USING_NAMESPACE_IMPORTS:-cvxif_instr_pkg}"
    export HDLCPP_VAR_TYPE_PATCHES="$OUT/cva6_var_type_patches.tsv"
    export HDLCPP_INLINE_COMB_MODULES="${HDLCPP_INLINE_COMB_MODULES:-}"
    export HDLCPP_INLINE_COMB_BODIES="$OUT/cva6_inline_comb_bodies.tsv"
    export HDLCPP_WORK_PRECOMB_CALLS="$OUT/cva6_work_precomb_calls.tsv"
    export HDLCPP_BEFORE_STROBE_LINE_CALLS="$OUT/cva6_strobe_hooks.tsv"
    export HDLCPP_AFTER_STROBE_LINE_CODE="$OUT/cva6_after_strobe_code.tsv"
    export HDLCPP_ASSIGN_PREFIX_CODE="$OUT/cva6_assign_prefix_code.tsv"
    export HDLCPP_ASSIGN_SUFFIX_CODE="$OUT/cva6_assign_suffix_code.tsv"
    export HDLCPP_ASSIGN_LINE_PATCHES="$OUT/cva6_assign_line_patches.tsv"
    export HDLCPP_SKIP_ASSIGN_MODULES="${HDLCPP_SKIP_ASSIGN_MODULES:-}"
    export HDLCPP_SKIP_ASSIGN_LINE_PREFIXES="${HDLCPP_SKIP_ASSIGN_LINE_PREFIXES:-issue_stage|issue_instr_o_out,issue_stage|issue_instr_hs_o_out}"
    export HDLCPP_SKIP_UNKNOWN_INSTANCE_TYPES="${HDLCPP_SKIP_UNKNOWN_INSTANCE_TYPES:-instr_tracer}"
    export HDLCPP_UNKNOWN_INPUTLESS_INSTANCE_TYPES="${HDLCPP_UNKNOWN_INPUTLESS_INSTANCE_TYPES:-acc_dispatcher,fpu_wrap}"
    export HDLCPP_UNKNOWN_OUTPUT_PORTS="${HDLCPP_UNKNOWN_OUTPUT_PORTS:-orig_instr_aes_bits}"
    export HDLCPP_COMB_RETURN_INJECTIONS="$OUT/cva6_comb_return_injections.tsv"
    export HDLCPP_PACKAGE_METHOD_OVERRIDES="$OUT/cva6_package_method_overrides.tsv"
    export HDLCPP_FUNCTION_WIDTHS="$OUT/cva6_function_widths.tsv"
    export HDLCPP_NUMERIC_ARG_INDICES="$OUT/cva6_numeric_arg_indices.tsv"
    export HDLCPP_GENERATE_PARAM_VALUES="$OUT/cva6_generate_param_values.tsv"
    export HDLCPP_REMOVE_TARGETED_MASK_TOKENS="${HDLCPP_REMOVE_TARGETED_MASK_TOKENS:-instr_comb_func().}"
    export HDLCPP_REMOVE_ONE_BIT_MASK_TOKENS="${HDLCPP_REMOVE_ONE_BIT_MASK_TOKENS:-instruction_i_in().bits}"
    export HDLCPP_PACKED_INPUT_ELEMENT_WIDTH="${HDLCPP_PACKED_INPUT_ELEMENT_WIDTH:-miss_paddr_i=CVA6Cfg.PLEN;tx_paddr_i=CVA6Cfg.PLEN}"
    export HDLCPP_REVERSE_PACKED_TYPES="${HDLCPP_REVERSE_PACKED_TYPES:-cva6_mmu.pte_cva6_t}"
    export HDLCPP_NOCACHE_COMB_METHODS="${HDLCPP_NOCACHE_COMB_METHODS:-id_stage|instruction_cvxif_i_comb_func,id_stage|is_illegal_cvxif_i_comb_func,id_stage|is_compressed_cvxif_i_comb_func,cva6|fetch_entry_if_id_comb_func,cva6|fetch_valid_if_id_comb_func}"
    export HDLCPP_READONLY_COMB_OUTPUTS="${HDLCPP_READONLY_COMB_OUTPUTS:-cva6_fifo_v3|data_o,fifo_v3|data_o}"
    export HDLCPP_READONLY_COMB_STOP_TOKENS="$OUT/cva6_readonly_comb_stop_tokens.tsv"
    TARGET="$TARGET" \
    RISCV="$RISCV" \
    HDLCPP="$HDLCPP" \
    HDLCPP_JOBS="$HDLCPP_JOBS" \
    CPPHDL_CVA6_NATIVE_HARNESS="$CPPHDL_CVA6_NATIVE_HARNESS" \
    CPPHDL_CVA6_ONLY="$CPPHDL_CVA6_ONLY" \
    CPPHDL_CVA6_FINALIZE_ONLY="$CPPHDL_CVA6_FINALIZE_ONLY" \
    CPPHDL_CVA6_RESUME_TRAITS="$CPPHDL_CVA6_RESUME_TRAITS" \
    CPPHDL_CVA6_KEEP_METADATA="$CPPHDL_CVA6_KEEP_METADATA" \
    CPPHDL_CVA6_SKIP_STALE_TRAIT_SCAN="$CPPHDL_CVA6_SKIP_STALE_TRAIT_SCAN" \
    python3 "$HELPER_DIR/convert_cva6.py"
)

rename_cpp_global_collisions() {
    python3 - "$OUT" <<'PY'
from pathlib import Path
import re
import sys

out = Path(sys.argv[1])
for path in out.rglob("*"):
    if path.suffix not in {".h", ".hh", ".hpp", ".cc", ".cpp"} or not path.is_file():
        continue
    text = path.read_text()
    updated = re.sub(r"\bsync\b", "cpphdl_sv_sync", text)
    updated = updated.replace("cpphdl_sv_sync.h\"", "sync.h\"")
    if updated != text:
        path.write_text(updated)
PY
}

rename_cpp_global_collisions

# Trait convergence and generated-header debugging do not need concrete template
# specialization or comb collection. Keep this CVA6-only control in the fixture so
# the generic hdlcpp converter remains free of test-design workflow policy.
if [[ "$CPPHDL_CVA6_SKIP_OPTIMIZE" == "1" ]]; then
    echo "skipped CppHDL specialization and comb optimization"
    exit 0
fi

(
    cd "$OUT"
    if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
        cp "$RUNNER" "$RUNNER.runtime"
        cp "run_cpphdl_testharness_optimize_seed.cpp" "$RUNNER"
        trap 'if [[ -f "$RUNNER.runtime" ]]; then mv "$RUNNER.runtime" "$RUNNER"; fi' EXIT
    fi
    HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE="$HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE" \
    HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE="$HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE" \
        "$HDLCPP" --optimize "$RUNNER"
    if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" ]]; then
        mv "$RUNNER.runtime" "$RUNNER"
        trap - EXIT
        cp "$RUNNER" cpphdl_optimized_main.cpp
        if [[ ! -x "$CPPHDL" ]]; then
            echo "missing cpphdl executable: $CPPHDL" >&2
            exit 2
        fi
        python3 prepare_optimize_combs.py . \
            --collection-chunk-size "${CPPHDL_COMB_COLLECTION_CHUNK_SIZE:-64}" \
            --collection-max-definition-bytes "${CPPHDL_COMB_COLLECTION_MAX_DEFINITION_BYTES:-4000000}" \
            --collection-isolate-definition-bytes "${CPPHDL_COMB_COLLECTION_ISOLATE_DEFINITION_BYTES:-1000000000}"
        python3 - <<'PY'
from pathlib import Path

for pattern in ("CpphdlOptimizedRoot_optimized_combs*", "cpphdl_opt_t0_optimized_combs*"):
    for path in Path(".").glob(pattern):
        if path.is_file():
            path.unlink()
PY
        CPPHDL="$CPPHDL" CPPHDL_COMB_OPTIMIZER_MODE="${CPPHDL_COMB_OPTIMIZER_MODE:-full}" \
            bash run_optimize_combs.sh .
        python3 - <<'PY'
from pathlib import Path
import re

# Keep the runner, narrow model bridges, and global comb schedule in separate
# translation units. A Make wildcard follows every chunk emitted by either comb
# optimizer mode without baking the chunk count from one conversion into it.
makefile = Path("Makefile.optimize")
text = makefile.read_text()
model_sources = [
    "run_cpphdl_testharness_model_create.cpp",
    "run_cpphdl_testharness_optimized_bind.cpp",
    "run_cpphdl_testharness_optimized_cycle.cpp",
    "run_cpphdl_testharness_model_memory.cpp",
    "run_cpphdl_testharness_model_observe.cpp",
]
comb_sources = sorted(Path(".").glob("cpphdl_opt_t0_optimized_combs*.cpp"))
if not comb_sources:
    raise SystemExit("cpphdl comb optimizer generated no implementation sources")
model_objects = [f"build/opt/{Path(source).stem}.o" for source in model_sources]
objects = " \\\n        ".join(model_objects)
text = text.replace(
    "OBJS := ",
    "COMB_SOURCES := $(sort $(wildcard cpphdl_opt_t0_optimized_combs*.cpp))\n"
    "COMB_OBJS := $(patsubst %.cpp,build/opt/%.o,$(COMB_SOURCES))\n"
    f"MODEL_OBJS := {objects} $(COMB_OBJS)\nOBJS := $(MODEL_OBJS) ",
    1,
)

# A complete flattened schedule replaces recursive _work/_strobe throughout
# the known hierarchy. Remove their dead explicit instantiations only after the
# generated sources confirm that no opaque subtree still calls either method.
legacy_cycle_call = re.compile(r"(?:\.|->)_(?:work|strobe)\s*\(")
fully_flattened = not any(
    legacy_cycle_call.search(source.read_text()) for source in comb_sources
)
cycle_instantiation = re.compile(
    r"^template void cpphdl_opt_t\d+::_(?:work\(bool\)|strobe\(\));\s*$",
    re.MULTILINE,
)
elaboration_objects = []
for source in Path(".").glob("cpphdl_optimized_inst_*.cpp"):
    source_text = source.read_text()
    root_cycle = ("template void cpphdl_opt_t0::_work(bool);" in source_text or
                  "template void cpphdl_opt_t0::_strobe();" in source_text)
    if not fully_flattened and not root_cycle:
        continue
    filtered = cycle_instantiation.sub("", source_text)
    if filtered == source_text:
        continue
    object_path = f"build/opt/{source.stem}.o"
    if "cpphdl_optimized_root_work" in filtered or "cpphdl_optimized_root_strobe" in filtered:
        text = text.replace(f" \\\n        {object_path}", "")
        text = text.replace(f" {object_path}", "")
        continue
    source.write_text(filtered)
    if not re.search(r"^template ", filtered, re.MULTILINE):
        text = text.replace(f" \\\n        {object_path}", "")
        text = text.replace(f" {object_path}", "")
    else:
        elaboration_objects.append(object_path)

# Different aliases can resolve to the same concrete C++ specialization. Keep
# one primary model type per translation unit while packing up to twenty small
# elaboration models together to amortize the generated-header parse.
alias_types = {}
for match in re.finditer(
        r"^using (cpphdl_opt_t\d+) = ([^;]+);$",
        Path("cpphdl_optimized_externs.h").read_text(), re.MULTILINE):
    alias_types[match.group(1)] = match.group(2).split("<", 1)[0].strip()
declarations = {}
for object_path in elaboration_objects:
    source = Path(Path(object_path).stem + ".cpp")
    for line in source.read_text().splitlines():
        match = re.match(r"^template (?:void )?(cpphdl_opt_t\d+)::", line)
        if match:
            declarations.setdefault(match.group(1), []).append(line)

packed_units = []
for alias, lines in declarations.items():
    primary_type = alias_types[alias]
    for unit in packed_units:
        if len(unit["aliases"]) < 20 and primary_type not in unit["types"]:
            break
    else:
        unit = {"aliases": [], "types": set()}
        packed_units.append(unit)
    unit["aliases"].append((alias, lines))
    unit["types"].add(primary_type)

for object_path in elaboration_objects:
    text = text.replace(f" \\\n        {object_path}", "")
    text = text.replace(f" {object_path}", "")
repacked_objects = []
for unit, target_object in zip(packed_units, elaboration_objects):
    target_source = Path(Path(target_object).stem + ".cpp")
    body = "\n\n".join("\n".join(lines) for _, lines in unit["aliases"])
    target_source.write_text('#include "cpphdl_optimized_externs.h"\n\n' + body + "\n")
    marker = "\nDEPS := $(OBJS:.o=.d)"
    text = text.replace(marker, f" \\\n        {target_object}" + marker, 1)
    repacked_objects.append(target_object)
elaboration_objects = repacked_objects

# After lifecycle removal, mixed small-model units contain only constructors
# and _assign bindings. Give them the same low-cost flags as isolated
# elaboration units while preserving -O2 for every generated cycle partition.
rules = "".join(
    f"{obj}: override CXXFLAGS += $(CONSTRUCTOR_CXXFLAGS)\n"
    for obj in elaboration_objects
    if f"{obj}: override CXXFLAGS" not in text
)
if rules:
    text = text.replace("build/opt/%.o: %.cpp", rules + "\nbuild/opt/%.o: %.cpp", 1)
makefile.write_text(text)
PY
    fi
)

rename_cpp_global_collisions

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" != "1" ]]; then
    python3 - "$OUT/generated/core/cache_subsystem/hpdcache/rtl/src/hpdcache_miss_handler.h" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace(
    "__cpphdl_value = cpphdl::sv_cast<__cpphdl_cast_t>(refill_data_comb_func());",
    "__cpphdl_value = cpphdl::unpack_value<__cpphdl_cast_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_cast_t>()>(refill_data_comb_func()));",
)
text = text.replace(
    "__cpphdl_value = cpphdl::sv_cast<__cpphdl_cast_t>(clean_data_comb_func());",
    "__cpphdl_value = cpphdl::unpack_value<__cpphdl_cast_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_cast_t>()>(clean_data_comb_func()));",
)
path.write_text(text)
PY
fi

echo "converted CVA6 source: $SRC/core"
echo "cpphdl output: $OUT"
