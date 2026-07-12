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
HDLCPP="${HDLCPP:-/home/me/cpphdl/hdlcpp/build/hdlcpp}"
CPPHDL_CVA6_NATIVE_HARNESS="${CPPHDL_CVA6_NATIVE_HARNESS:-0}"

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" == "1" && "${CPPHDL_OUT:-}" == "" ]]; then
    OUT="$SCRIPT_DIR/cpphdl_native"
fi

if [[ ! -d "$SRC/core" ]]; then
    echo "missing CVA6 source core directory: $SRC/core" >&2
    exit 2
fi
if [[ ! -d "$SUPPORT/tools" ]]; then
    echo "missing conversion support directory: $SUPPORT" >&2
    exit 2
fi
if [[ ! -x "$HDLCPP" ]]; then
    make -C /home/me/cpphdl/hdlcpp/build -j"$JOBS" hdlcpp
fi
export HDLCPP

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$SUPPORT"/. "$OUT"/
touch "$OUT/cva6_assign_suffix_code.tsv"
python3 - "$OUT" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1]).resolve()
for path in out.glob("*.tsv"):
    text = path.read_text()
    text = text.replace("/home/me/cva6/cpphdl", str(out))
    path.write_text(text)
PY

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
    export HDLCPP_INLINE_COMB_MODULES="${HDLCPP_INLINE_COMB_MODULES:-rr_arb_tree}"
    export HDLCPP_INLINE_COMB_BODIES="$OUT/cva6_inline_comb_bodies.tsv"
    export HDLCPP_WORK_PRECOMB_CALLS="$OUT/cva6_work_precomb_calls.tsv"
    export HDLCPP_BEFORE_STROBE_LINE_CALLS="$OUT/cva6_strobe_hooks.tsv"
    export HDLCPP_AFTER_STROBE_LINE_CODE="$OUT/cva6_after_strobe_code.tsv"
    export HDLCPP_ASSIGN_PREFIX_CODE="$OUT/cva6_assign_prefix_code.tsv"
    export HDLCPP_ASSIGN_SUFFIX_CODE="$OUT/cva6_assign_suffix_code.tsv"
    export HDLCPP_ASSIGN_LINE_PATCHES="$OUT/cva6_assign_line_patches.tsv"
    export HDLCPP_SKIP_ASSIGN_MODULES="${HDLCPP_SKIP_ASSIGN_MODULES:-rr_arb_tree}"
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
    python3 "$HELPER_DIR/convert_cva6.py"
)

if [[ "$CPPHDL_CVA6_NATIVE_HARNESS" != "1" ]]; then
    (
        cd "$OUT"
        "$HDLCPP" --optimize run_cpphdl_matrix.cpp
    )

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
