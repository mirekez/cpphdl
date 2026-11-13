`default_nettype none

import Predef_pkg::*;
import TribeSbiDecodeState_pkg::*;
import Mem_pkg::*;
import Wb_pkg::*;
import Csr_pkg::*;
import Sys_pkg::*;
import Trap_pkg::*;
import Amo_pkg::*;
import TribeCoreDebug_pkg::*;
import TribeMmuDebug_pkg::*;
import TribeCacheDebug_pkg::*;
import TribeWritebackDebug_pkg::*;
import TribeCsrDebug_pkg::*;
import TribeIrqDebug_pkg::*;
import TribeRegsDebug_pkg::*;
import TribeBranchDebug_pkg::*;
import TribeDecodeDebug_pkg::*;
import TribeSbiDebug_pkg::*;
import Br_pkg::*;
import L1CachePerf_pkg::*;
import TribePerf_pkg::*;
import State_pkg::*;
import Zicsr_pkg::*;
import Rv32ia_pkg::*;
import Rv32im_pkg::*;
import Rv32ic_pkg::*;
import Rv32i_pkg::*;
import Alu_pkg::*;


module Tribe (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   output wire external_cache_invalidate_ready_out
,   output wire dmem_write_out
,   output wire[31:0] dmem_write_data_out
,   output wire[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output wire[31:0] dmem_addr_out
,   output wire[31:0] imem_read_addr_out
,   output wire TribeCoreDebug debug_core_out
,   output wire TribeMmuDebug debug_mmu_out
,   output wire TribeCacheDebug debug_cache_out
,   output wire TribeWritebackDebug debug_wb_out
,   output wire TribeCsrDebug debug_csr_out
,   output wire TribeIrqDebug debug_irq_out
,   output wire TribeRegsDebug debug_regs_out
,   output wire TribeBranchDebug debug_branch_out
,   output wire TribeDecodeDebug debug_decode_out
,   output wire sbi_set_timer_out
,   output wire[31:0] sbi_timer_lo_out
,   output wire[31:0] sbi_timer_hi_out
,   output wire TribeSbiDebug debug_sbi_out
,   input wire[31:0] reset_pc_in
,   input wire[31:0] boot_hartid_in
,   input wire[31:0] boot_dtb_addr_in
,   input wire[2-1:0] boot_priv_in
,   input wire external_cache_invalidate_in
,   input wire[31:0] memory_base_in
,   input wire[31:0] memory_size_in
,   input wire[31:0] mem_region_size_in[4]
,   output wire i_mem_out__read_out
,   output wire i_mem_out__write_out
,   output wire[31:0] i_mem_out__addr_out
,   output wire[31:0] i_mem_out__write_data_out
,   output wire[7:0] i_mem_out__write_mask_out
,   output wire i_mem_out__cache_disable_out
,   input wire[256-1:0] i_mem_out__read_data_in
,   input wire i_mem_out__wait_in
,   output wire d_mem_out__read_out
,   output wire d_mem_out__write_out
,   output wire[31:0] d_mem_out__addr_out
,   output wire[31:0] d_mem_out__write_data_out
,   output wire[7:0] d_mem_out__write_mask_out
,   output wire d_mem_out__cache_disable_out
,   input wire[256-1:0] d_mem_out__read_data_in
,   input wire d_mem_out__wait_in
,   input wire clint_msip_in
,   input wire clint_mtip_in
,   input wire[31:0] time_lo_in
,   input wire[31:0] time_hi_in
,   input wire external_irq_in
,   output wire TribePerf perf_out
,   input wire debugen_in
);
    localparam  L2_PTW_OWNER_NONE = 'h0;
    localparam  L2_PTW_OWNER_DMMU = 'h1;
    localparam  L2_PTW_OWNER_IMMU = 'h2;
    localparam  SBI_EXT_BASE = 'h10;
    localparam  SBI_EXT_TIME = 'h54494D45;
    localparam  SBI_EXT_RFENCE = 'h52464E43;
    localparam  SBI_SUCCESS = 'h0;


    // regs and combs
    reg icache_invalidate_issued_reg;
    reg sfence_vma_issued_reg;
    reg sbi_arg_wait_reg;
    TribeSbiDecodeState sbi_decode_reg;
    TribeCoreDebug debug_core_comb;
    TribeMmuDebug debug_mmu_comb;
    TribeCacheDebug debug_cache_comb;
    TribeWritebackDebug debug_wb_comb;
    TribeCsrDebug debug_csr_comb;
    TribeIrqDebug debug_irq_comb;
    TribeRegsDebug debug_regs_comb;
    TribeBranchDebug debug_branch_comb;
    TribeDecodeDebug debug_decode_comb;
    TribeSbiDebug debug_sbi_comb;
    reg[32-1:0] pc;
    reg valid;
    reg fetch_buffer_valid_reg;
    reg[32-1:0] fetch_instr_reg;
    reg[32-1:0] fetch_pc_reg;
    reg icache_flush_reg;
    (* extract_reset = "no" *)
    State decode_state_reg;
    reg[32-1:0] decode_fallthrough_reg;
    reg load_result_pending_reg;
    reg load_retire_ready_reg;
    reg atomic_write_accepted_reg;
    (* extract_reset = "no" *)
    State csr_commit_state_reg;
    reg csr_commit_fire_reg;
    reg csr_commit_serialized_reg;
    reg csr_commit_interrupt_valid_reg;
    reg[32-1:0] csr_commit_interrupt_cause_reg;
    reg csr_commit_interrupt_to_supervisor_reg;
    (* extract_reset = "no" *)
    logic[32-1:0] alu_result_reg;
    (* extract_reset = "no" *)
    State[2-1:0] state_reg;
    reg[2-1:0][32-1:0] predicted_next_reg;
    reg[2-1:0][32-1:0] fallthrough_reg;
    reg[2-1:0] predicted_taken_reg;
    reg[32-1:0] debug_alu_a_reg;
    reg[32-1:0] debug_alu_b_reg;
    reg[32-1:0] debug_branch_target_reg;
    reg debug_branch_taken_reg;
    reg output_write_active_reg;
    reg interrupt_entry_guard_reg;
    reg interrupt_accept_reg;
    reg[32-1:0] interrupt_cause_reg;
    reg interrupt_to_supervisor_reg;
    reg sbi_ret_a1_valid_reg;
    reg[32-1:0] sbi_ret_a1_reg;
    reg immu_result_valid_reg;
    reg[32-1:0] immu_vaddr_reg;
    reg[32-1:0] immu_paddr_reg;
    reg immu_fault_reg;
    reg dmmu_result_valid_reg;
    reg[32-1:0] dmmu_vaddr_reg;
    reg[32-1:0] dmmu_paddr_reg;
    reg dmmu_fault_reg;
    reg[2-1:0] l2_ptw_owner_reg;
    logic hazard_stall_comb;
    logic branch_stall_comb;
    logic branch_flush_comb;
    logic stall_comb;
    TribePerf perf_comb;
    logic[31:0] l2_data_addr_comb;
    logic dmmu_ptw_selected_comb;
    logic immu_ptw_selected_comb;
    logic[31:0] mmu_l2_read_word_comb;
    logic immu_result_match_comb;
    logic immu_active_fault_comb;
    logic dmmu_access_ready_comb;
    logic[31:0] dmmu_vaddr_comb;
    logic dmmu_result_match_comb;
    logic csr_commit_serializing_comb;
    logic csr_commit_wait_comb;
    logic xret_redirect_comb;
    logic sync_trap_redirect_comb;
    logic fence_i_registered_store_pending_comb;
    logic memory_wait_comb;
    logic icache_response_match_comb;
    logic fetch_valid_comb;
    logic[31:0] decode_fallthrough_comb;
    logic decode_branch_valid_comb;
    logic decode_indirect_branch_valid_comb;
    logic[31:0] decode_branch_target_comb;
    logic[31:0] branch_actual_next_comb;
    logic branch_mispredict_comb;
    logic[31:0] fetch_addr_comb;
    logic sbi_legacy_ecall_comb;
    logic sbi_decode_active_comb;
    logic sbi_decode_wait_comb;
    logic sbi_decode_handled_comb;
    logic register_write_commit_comb;
    logic sbi_arg_hazard_comb;
    logic sbi_noop_comb;
    logic sbi_base_comb;
    logic sbi_set_timer_comb;
    logic[31:0] sbi_ret_value_comb;
    logic sbi_writes_a1_comb;
    logic sbi_handled_comb;
    logic[31:0] sbi_timer_lo_comb;
    logic[31:0] sbi_timer_hi_comb;
    logic sbi_registered_noop_comb;
    logic sbi_registered_base_comb;
    logic sbi_registered_set_timer_comb;
    logic sbi_registered_writes_a1_comb;
    logic sbi_registered_handled_comb;
    logic[31:0] sbi_registered_ret_value_comb;
    logic sbi_ecall_debug_comb;
    logic[31:0] sbi_a7_debug_comb;
    logic[31:0] sbi_a6_debug_comb;
    logic[31:0] sbi_a0_debug_comb;
    logic sbi_set_timer_output_comb;
    logic[31:0] sbi_timer_lo_output_comb;
    logic[31:0] sbi_timer_hi_output_comb;
    logic interrupt_capture_comb;
    logic interrupt_accept_comb;
    logic memory_stage_transport_wait_comb;
    logic interrupt_retire_wait_comb;
    logic interrupt_entry_wait_comb;
    State exe_state_comb;
    logic[31:0] inst_page_fault_vector_comb;
    logic[31:0] data_page_fault_vector_comb;
    logic dmmu_active_fault_comb;
    State csr_state_comb;
    logic icache_invalidate_comb;
    logic sfence_vma_comb;

    // members
    wire[31:0] dec__pc_in;
    wire dec__instr_valid_in;
    wire[31:0] dec__instr_in;
    wire[31:0] dec__regs_data0_in;
    wire[31:0] dec__regs_data1_in;
    wire[5-1:0] dec__rs1_out;
    wire[5-1:0] dec__rs2_out;
    wire State dec__state_out;
    Decode      dec (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .pc_in(dec__pc_in)
,       .instr_valid_in(dec__instr_valid_in)
,       .instr_in(dec__instr_in)
,       .regs_data0_in(dec__regs_data0_in)
,       .regs_data1_in(dec__regs_data1_in)
,       .rs1_out(dec__rs1_out)
,       .rs2_out(dec__rs2_out)
,       .state_out(dec__state_out)
    );
    wire State exe__state_in;
    wire State exe__multicycle_state_in;
    wire[31:0] exe__alu_result_out;
    wire[31:0] exe__mem_addr_out;
    wire[31:0] exe__debug_alu_a_out;
    wire[31:0] exe__debug_alu_b_out;
    wire exe__branch_taken_out;
    wire[31:0] exe__branch_target_out;
    wire exe__multicycle_wait_out;
    Execute      exe (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .state_in(exe__state_in)
,       .multicycle_state_in(exe__multicycle_state_in)
,       .alu_result_out(exe__alu_result_out)
,       .mem_addr_out(exe__mem_addr_out)
,       .debug_alu_a_out(exe__debug_alu_a_out)
,       .debug_alu_b_out(exe__debug_alu_b_out)
,       .branch_taken_out(exe__branch_taken_out)
,       .branch_target_out(exe__branch_target_out)
,       .multicycle_wait_out(exe__multicycle_wait_out)
    );
    wire State exe_mem__state_in;
    wire[31:0] exe_mem__alu_result_in;
    wire exe_mem__dcache_read_valid_in;
    wire[31:0] exe_mem__dcache_read_addr_in;
    wire[31:0] exe_mem__dcache_read_expected_addr_in;
    wire[31:0] exe_mem__dcache_read_data_in;
    wire exe_mem__mem_stall_in;
    wire exe_mem__hold_in;
    wire exe_mem__transaction_owner_valid_in;
    wire exe_mem__mem_write_out;
    wire[31:0] exe_mem__mem_write_addr_out;
    wire[31:0] exe_mem__mem_write_data_out;
    wire[7:0] exe_mem__mem_write_mask_out;
    wire exe_mem__mem_read_out;
    wire[31:0] exe_mem__mem_read_addr_out;
    wire exe_mem__mem_split_out;
    wire exe_mem__mem_split_busy_out;
    wire exe_mem__split_load_out;
    wire[31:0] exe_mem__split_load_low_out;
    wire[31:0] exe_mem__split_load_high_out;
    wire exe_mem__atomic_busy_out;
    wire[31:0] exe_mem__atomic_sc_result_out;
    ExecuteMem      exe_mem (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .state_in(exe_mem__state_in)
,       .alu_result_in(exe_mem__alu_result_in)
,       .dcache_read_valid_in(exe_mem__dcache_read_valid_in)
,       .dcache_read_addr_in(exe_mem__dcache_read_addr_in)
,       .dcache_read_expected_addr_in(exe_mem__dcache_read_expected_addr_in)
,       .dcache_read_data_in(exe_mem__dcache_read_data_in)
,       .mem_stall_in(exe_mem__mem_stall_in)
,       .hold_in(exe_mem__hold_in)
,       .transaction_owner_valid_in(exe_mem__transaction_owner_valid_in)
,       .mem_write_out(exe_mem__mem_write_out)
,       .mem_write_addr_out(exe_mem__mem_write_addr_out)
,       .mem_write_data_out(exe_mem__mem_write_data_out)
,       .mem_write_mask_out(exe_mem__mem_write_mask_out)
,       .mem_read_out(exe_mem__mem_read_out)
,       .mem_read_addr_out(exe_mem__mem_read_addr_out)
,       .mem_split_out(exe_mem__mem_split_out)
,       .mem_split_busy_out(exe_mem__mem_split_busy_out)
,       .split_load_out(exe_mem__split_load_out)
,       .split_load_low_out(exe_mem__split_load_low_out)
,       .split_load_high_out(exe_mem__split_load_high_out)
,       .atomic_busy_out(exe_mem__atomic_busy_out)
,       .atomic_sc_result_out(exe_mem__atomic_sc_result_out)
    );
    wire State wb__state_in;
    wire[31:0] wb__alu_result_in;
    wire[31:0] wb__mem_data_in;
    wire[31:0] wb__mem_data_hi_in;
    wire[31:0] wb__mem_addr_in;
    wire wb__mem_split_in;
    wire[31:0] wb__regs_data_out;
    wire[7:0] wb__regs_wr_id_out;
    wire wb__regs_write_out;
    Writeback      wb (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .state_in(wb__state_in)
,       .alu_result_in(wb__alu_result_in)
,       .mem_data_in(wb__mem_data_in)
,       .mem_data_hi_in(wb__mem_data_hi_in)
,       .mem_addr_in(wb__mem_addr_in)
,       .mem_split_in(wb__mem_split_in)
,       .regs_data_out(wb__regs_data_out)
,       .regs_wr_id_out(wb__regs_wr_id_out)
,       .regs_write_out(wb__regs_write_out)
    );
    wire State wb_mem__state_in;
    wire[31:0] wb_mem__alu_result_in;
    wire wb_mem__split_load_in;
    wire[31:0] wb_mem__split_load_low_addr_in;
    wire[31:0] wb_mem__split_load_high_addr_in;
    wire wb_mem__dcache_read_valid_in;
    wire[31:0] wb_mem__dcache_read_addr_in;
    wire[31:0] wb_mem__dcache_read_data_in;
    wire wb_mem__dcache_write_valid_in;
    wire[31:0] wb_mem__dcache_write_addr_in;
    wire[31:0] wb_mem__dcache_write_data_in;
    wire[7:0] wb_mem__dcache_write_mask_in;
    wire wb_mem__store_forward_enable_in;
    wire wb_mem__retire_in;
    wire wb_mem__load_ready_out;
    wire[31:0] wb_mem__load_raw_out;
    wire[31:0] wb_mem__load_result_out;
    wire[31:0] wb_mem__wb_mem_data_out;
    wire[31:0] wb_mem__wb_mem_data_hi_out;
    wire wb_mem__debug_load_data_valid_out;
    wire[31:0] wb_mem__debug_load_addr_out;
    wire wb_mem__debug_split_low_valid_out;
    wire wb_mem__debug_split_high_valid_out;
    wire wb_mem__debug_held_load_valid_out;
    WritebackMem      wb_mem (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .state_in(wb_mem__state_in)
,       .alu_result_in(wb_mem__alu_result_in)
,       .split_load_in(wb_mem__split_load_in)
,       .split_load_low_addr_in(wb_mem__split_load_low_addr_in)
,       .split_load_high_addr_in(wb_mem__split_load_high_addr_in)
,       .dcache_read_valid_in(wb_mem__dcache_read_valid_in)
,       .dcache_read_addr_in(wb_mem__dcache_read_addr_in)
,       .dcache_read_data_in(wb_mem__dcache_read_data_in)
,       .dcache_write_valid_in(wb_mem__dcache_write_valid_in)
,       .dcache_write_addr_in(wb_mem__dcache_write_addr_in)
,       .dcache_write_data_in(wb_mem__dcache_write_data_in)
,       .dcache_write_mask_in(wb_mem__dcache_write_mask_in)
,       .store_forward_enable_in(wb_mem__store_forward_enable_in)
,       .retire_in(wb_mem__retire_in)
,       .load_ready_out(wb_mem__load_ready_out)
,       .load_raw_out(wb_mem__load_raw_out)
,       .load_result_out(wb_mem__load_result_out)
,       .wb_mem_data_out(wb_mem__wb_mem_data_out)
,       .wb_mem_data_hi_out(wb_mem__wb_mem_data_hi_out)
,       .debug_load_data_valid_out(wb_mem__debug_load_data_valid_out)
,       .debug_load_addr_out(wb_mem__debug_load_addr_out)
,       .debug_split_low_valid_out(wb_mem__debug_split_low_valid_out)
,       .debug_split_high_valid_out(wb_mem__debug_split_high_valid_out)
,       .debug_held_load_valid_out(wb_mem__debug_held_load_valid_out)
    );
    wire State csr__state_in;
    wire csr__commit_in;
    wire State csr__read_state_in;
    wire State csr__trap_check_state_in;
    wire State csr__legality_state_in;
    wire csr__legality_out;
    wire State csr__redirect_state_in;
    wire[2-1:0] csr__reset_priv_in;
    wire[31:0] csr__hartid_in;
    wire csr__interrupt_valid_in;
    wire[31:0] csr__interrupt_cause_in;
    wire csr__interrupt_to_supervisor_in;
    wire csr__redirect_interrupt_valid_in;
    wire[31:0] csr__redirect_interrupt_cause_in;
    wire csr__redirect_interrupt_to_supervisor_in;
    wire[31:0] csr__irq_pending_bits_in;
    wire csr__software_irq_set_in;
    wire csr__write_pending_out;
    wire[31:0] csr__read_data_out;
    wire[31:0] csr__time_lo_in;
    wire[31:0] csr__time_hi_in;
    wire[31:0] csr__trap_vector_out;
    wire[31:0] csr__epc_out;
    wire[31:0] csr__mepc_out;
    wire[31:0] csr__mtvec_out;
    wire[31:0] csr__mcause_out;
    wire[31:0] csr__mtval_out;
    wire[31:0] csr__sepc_out;
    wire[31:0] csr__stvec_out;
    wire[31:0] csr__scause_out;
    wire[31:0] csr__stval_out;
    wire csr__illegal_trap_out;
    wire[31:0] csr__mstatus_out;
    wire[31:0] csr__mie_out;
    wire[31:0] csr__mideleg_out;
    wire[31:0] csr__medeleg_out;
    wire[31:0] csr__mip_sw_out;
    wire[31:0] csr__satp_out;
    wire[2-1:0] csr__priv_out;
    CSR      csr (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .state_in(csr__state_in)
,       .commit_in(csr__commit_in)
,       .read_state_in(csr__read_state_in)
,       .trap_check_state_in(csr__trap_check_state_in)
,       .legality_state_in(csr__legality_state_in)
,       .legality_out(csr__legality_out)
,       .redirect_state_in(csr__redirect_state_in)
,       .reset_priv_in(csr__reset_priv_in)
,       .hartid_in(csr__hartid_in)
,       .interrupt_valid_in(csr__interrupt_valid_in)
,       .interrupt_cause_in(csr__interrupt_cause_in)
,       .interrupt_to_supervisor_in(csr__interrupt_to_supervisor_in)
,       .redirect_interrupt_valid_in(csr__redirect_interrupt_valid_in)
,       .redirect_interrupt_cause_in(csr__redirect_interrupt_cause_in)
,       .redirect_interrupt_to_supervisor_in(csr__redirect_interrupt_to_supervisor_in)
,       .irq_pending_bits_in(csr__irq_pending_bits_in)
,       .software_irq_set_in(csr__software_irq_set_in)
,       .write_pending_out(csr__write_pending_out)
,       .read_data_out(csr__read_data_out)
,       .time_lo_in(csr__time_lo_in)
,       .time_hi_in(csr__time_hi_in)
,       .trap_vector_out(csr__trap_vector_out)
,       .epc_out(csr__epc_out)
,       .mepc_out(csr__mepc_out)
,       .mtvec_out(csr__mtvec_out)
,       .mcause_out(csr__mcause_out)
,       .mtval_out(csr__mtval_out)
,       .sepc_out(csr__sepc_out)
,       .stvec_out(csr__stvec_out)
,       .scause_out(csr__scause_out)
,       .stval_out(csr__stval_out)
,       .illegal_trap_out(csr__illegal_trap_out)
,       .mstatus_out(csr__mstatus_out)
,       .mie_out(csr__mie_out)
,       .mideleg_out(csr__mideleg_out)
,       .medeleg_out(csr__medeleg_out)
,       .mip_sw_out(csr__mip_sw_out)
,       .satp_out(csr__satp_out)
,       .priv_out(csr__priv_out)
    );
    wire[31:0] irq__mstatus_in;
    wire[31:0] irq__mie_in;
    wire[31:0] irq__mideleg_in;
    wire[31:0] irq__mip_sw_in;
    wire[2-1:0] irq__priv_in;
    wire irq__clint_msip_in;
    wire irq__clint_mtip_in;
    wire irq__external_irq_in;
    wire[31:0] irq__mip_out;
    wire irq__interrupt_valid_out;
    wire[31:0] irq__interrupt_cause_out;
    wire irq__interrupt_to_supervisor_out;
    InterruptController      irq (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .mstatus_in(irq__mstatus_in)
,       .mie_in(irq__mie_in)
,       .mideleg_in(irq__mideleg_in)
,       .mip_sw_in(irq__mip_sw_in)
,       .priv_in(irq__priv_in)
,       .clint_msip_in(irq__clint_msip_in)
,       .clint_mtip_in(irq__clint_mtip_in)
,       .external_irq_in(irq__external_irq_in)
,       .mip_out(irq__mip_out)
,       .interrupt_valid_out(irq__interrupt_valid_out)
,       .interrupt_cause_out(irq__interrupt_cause_out)
,       .interrupt_to_supervisor_out(irq__interrupt_to_supervisor_out)
    );
    wire[31:0] immu__vaddr_in;
    wire immu__read_in;
    wire immu__write_in;
    wire immu__execute_in;
    wire[31:0] immu__satp_in;
    wire[2-1:0] immu__priv_in;
    wire immu__sum_in;
    wire immu__mxr_in;
    wire[31:0] immu__direct_base_in;
    wire[31:0] immu__direct_size_in;
    wire immu__fill_in;
    wire[unsigned'(64'($clog2(unsigned'(32'(8)))))-1:0] immu__fill_index_in;
    wire[31:0] immu__fill_vpn_in;
    wire[31:0] immu__fill_ppn_in;
    wire[7:0] immu__fill_flags_in;
    wire immu__sfence_in;
    wire immu__mem_read_out;
    wire[31:0] immu__mem_addr_out;
    wire[31:0] immu__mem_read_data_in;
    wire immu__mem_wait_in;
    wire[31:0] immu__paddr_out;
    wire immu__translated_out;
    wire immu__hit_out;
    wire immu__fault_out;
    wire immu__miss_out;
    wire immu__busy_out;
    wire[31:0] immu__debug_last_pte_out;
    wire[31:0] immu__debug_last_addr_out;
    MMU_TLB #(
        8
    ) immu (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .vaddr_in(immu__vaddr_in)
,       .read_in(immu__read_in)
,       .write_in(immu__write_in)
,       .execute_in(immu__execute_in)
,       .satp_in(immu__satp_in)
,       .priv_in(immu__priv_in)
,       .sum_in(immu__sum_in)
,       .mxr_in(immu__mxr_in)
,       .direct_base_in(immu__direct_base_in)
,       .direct_size_in(immu__direct_size_in)
,       .fill_in(immu__fill_in)
,       .fill_index_in(immu__fill_index_in)
,       .fill_vpn_in(immu__fill_vpn_in)
,       .fill_ppn_in(immu__fill_ppn_in)
,       .fill_flags_in(immu__fill_flags_in)
,       .sfence_in(immu__sfence_in)
,       .mem_read_out(immu__mem_read_out)
,       .mem_addr_out(immu__mem_addr_out)
,       .mem_read_data_in(immu__mem_read_data_in)
,       .mem_wait_in(immu__mem_wait_in)
,       .paddr_out(immu__paddr_out)
,       .translated_out(immu__translated_out)
,       .hit_out(immu__hit_out)
,       .fault_out(immu__fault_out)
,       .miss_out(immu__miss_out)
,       .busy_out(immu__busy_out)
,       .debug_last_pte_out(immu__debug_last_pte_out)
,       .debug_last_addr_out(immu__debug_last_addr_out)
    );
    wire[31:0] dmmu__vaddr_in;
    wire dmmu__read_in;
    wire dmmu__write_in;
    wire dmmu__execute_in;
    wire[31:0] dmmu__satp_in;
    wire[2-1:0] dmmu__priv_in;
    wire dmmu__sum_in;
    wire dmmu__mxr_in;
    wire[31:0] dmmu__direct_base_in;
    wire[31:0] dmmu__direct_size_in;
    wire dmmu__fill_in;
    wire[unsigned'(64'($clog2(unsigned'(32'(8)))))-1:0] dmmu__fill_index_in;
    wire[31:0] dmmu__fill_vpn_in;
    wire[31:0] dmmu__fill_ppn_in;
    wire[7:0] dmmu__fill_flags_in;
    wire dmmu__sfence_in;
    wire dmmu__mem_read_out;
    wire[31:0] dmmu__mem_addr_out;
    wire[31:0] dmmu__mem_read_data_in;
    wire dmmu__mem_wait_in;
    wire[31:0] dmmu__paddr_out;
    wire dmmu__translated_out;
    wire dmmu__hit_out;
    wire dmmu__fault_out;
    wire dmmu__miss_out;
    wire dmmu__busy_out;
    wire[31:0] dmmu__debug_last_pte_out;
    wire[31:0] dmmu__debug_last_addr_out;
    MMU_TLB #(
        8
    ) dmmu (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .vaddr_in(dmmu__vaddr_in)
,       .read_in(dmmu__read_in)
,       .write_in(dmmu__write_in)
,       .execute_in(dmmu__execute_in)
,       .satp_in(dmmu__satp_in)
,       .priv_in(dmmu__priv_in)
,       .sum_in(dmmu__sum_in)
,       .mxr_in(dmmu__mxr_in)
,       .direct_base_in(dmmu__direct_base_in)
,       .direct_size_in(dmmu__direct_size_in)
,       .fill_in(dmmu__fill_in)
,       .fill_index_in(dmmu__fill_index_in)
,       .fill_vpn_in(dmmu__fill_vpn_in)
,       .fill_ppn_in(dmmu__fill_ppn_in)
,       .fill_flags_in(dmmu__fill_flags_in)
,       .sfence_in(dmmu__sfence_in)
,       .mem_read_out(dmmu__mem_read_out)
,       .mem_addr_out(dmmu__mem_addr_out)
,       .mem_read_data_in(dmmu__mem_read_data_in)
,       .mem_wait_in(dmmu__mem_wait_in)
,       .paddr_out(dmmu__paddr_out)
,       .translated_out(dmmu__translated_out)
,       .hit_out(dmmu__hit_out)
,       .fault_out(dmmu__fault_out)
,       .miss_out(dmmu__miss_out)
,       .busy_out(dmmu__busy_out)
,       .debug_last_pte_out(dmmu__debug_last_pte_out)
,       .debug_last_addr_out(dmmu__debug_last_addr_out)
    );
    wire[7:0] regs__write_addr_in;
    wire regs__write_in;
    wire[31:0] regs__write_data_in;
    wire[7:0] regs__write2_addr_in;
    wire regs__write2_in;
    wire[31:0] regs__write2_data_in;
    wire[7:0] regs__read_addr0_in;
    wire[7:0] regs__read_addr1_in;
    wire regs__read_in;
    wire[31:0] regs__read_data0_out;
    wire[31:0] regs__read_data1_out;
    wire[31:0] regs__reset_x10_in;
    wire[31:0] regs__reset_x11_in;
    wire[31:0] regs__x1_out;
    wire[31:0] regs__x10_out;
    wire[31:0] regs__x11_out;
    wire[31:0] regs__x16_out;
    wire[31:0] regs__x17_out;
    wire regs__debugen_in;
    File #(
        32
,       32
,       0
    ) regs (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .write_addr_in(regs__write_addr_in)
,       .write_in(regs__write_in)
,       .write_data_in(regs__write_data_in)
,       .write2_addr_in(regs__write2_addr_in)
,       .write2_in(regs__write2_in)
,       .write2_data_in(regs__write2_data_in)
,       .read_addr0_in(regs__read_addr0_in)
,       .read_addr1_in(regs__read_addr1_in)
,       .read_in(regs__read_in)
,       .read_data0_out(regs__read_data0_out)
,       .read_data1_out(regs__read_data1_out)
,       .reset_x10_in(regs__reset_x10_in)
,       .reset_x11_in(regs__reset_x11_in)
,       .x1_out(regs__x1_out)
,       .x10_out(regs__x10_out)
,       .x11_out(regs__x11_out)
,       .x16_out(regs__x16_out)
,       .x17_out(regs__x17_out)
,       .debugen_in(regs__debugen_in)
    );
    wire icache__write_in;
    wire[31:0] icache__write_data_in;
    wire[7:0] icache__write_mask_in;
    wire icache__read_in;
    wire[31:0] icache__addr_in;
    wire[31:0] icache__read_data_out;
    wire[31:0] icache__read_addr_out;
    wire icache__read_valid_out;
    wire icache__busy_out;
    wire icache__stall_in;
    wire icache__flush_in;
    wire icache__invalidate_in;
    wire icache__invalidate_line_in;
    wire[31:0] icache__invalidate_addr_in;
    wire icache__cache_disable_in;
    wire icache__mem_out__read_out;
    wire icache__mem_out__write_out;
    wire[31:0] icache__mem_out__addr_out;
    wire[31:0] icache__mem_out__write_data_out;
    wire[7:0] icache__mem_out__write_mask_out;
    wire icache__mem_out__cache_disable_out;
    wire[256-1:0] icache__mem_out__read_data_in;
    wire icache__mem_out__wait_in;
    wire L1CachePerf icache__perf_out;
    wire icache__debugen_in;
    L1Cache #(
        2048
,       32
,       2
,       0
,       32
,       256
    ) icache (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .write_in(icache__write_in)
,       .write_data_in(icache__write_data_in)
,       .write_mask_in(icache__write_mask_in)
,       .read_in(icache__read_in)
,       .addr_in(icache__addr_in)
,       .read_data_out(icache__read_data_out)
,       .read_addr_out(icache__read_addr_out)
,       .read_valid_out(icache__read_valid_out)
,       .busy_out(icache__busy_out)
,       .stall_in(icache__stall_in)
,       .flush_in(icache__flush_in)
,       .invalidate_in(icache__invalidate_in)
,       .invalidate_line_in(icache__invalidate_line_in)
,       .invalidate_addr_in(icache__invalidate_addr_in)
,       .cache_disable_in(icache__cache_disable_in)
,       .mem_out__read_out(icache__mem_out__read_out)
,       .mem_out__write_out(icache__mem_out__write_out)
,       .mem_out__addr_out(icache__mem_out__addr_out)
,       .mem_out__write_data_out(icache__mem_out__write_data_out)
,       .mem_out__write_mask_out(icache__mem_out__write_mask_out)
,       .mem_out__cache_disable_out(icache__mem_out__cache_disable_out)
,       .mem_out__read_data_in(icache__mem_out__read_data_in)
,       .mem_out__wait_in(icache__mem_out__wait_in)
,       .perf_out(icache__perf_out)
,       .debugen_in(icache__debugen_in)
    );
    wire dcache__write_in;
    wire[31:0] dcache__write_data_in;
    wire[7:0] dcache__write_mask_in;
    wire dcache__read_in;
    wire[31:0] dcache__addr_in;
    wire[31:0] dcache__read_data_out;
    wire[31:0] dcache__read_addr_out;
    wire dcache__read_valid_out;
    wire dcache__busy_out;
    wire dcache__stall_in;
    wire dcache__flush_in;
    wire dcache__invalidate_in;
    wire dcache__invalidate_line_in;
    wire[31:0] dcache__invalidate_addr_in;
    wire dcache__cache_disable_in;
    wire dcache__mem_out__read_out;
    wire dcache__mem_out__write_out;
    wire[31:0] dcache__mem_out__addr_out;
    wire[31:0] dcache__mem_out__write_data_out;
    wire[7:0] dcache__mem_out__write_mask_out;
    wire dcache__mem_out__cache_disable_out;
    wire[256-1:0] dcache__mem_out__read_data_in;
    wire dcache__mem_out__wait_in;
    wire L1CachePerf dcache__perf_out;
    wire dcache__debugen_in;
    L1Cache #(
        1024
,       32
,       2
,       1
,       32
,       256
    ) dcache (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .write_in(dcache__write_in)
,       .write_data_in(dcache__write_data_in)
,       .write_mask_in(dcache__write_mask_in)
,       .read_in(dcache__read_in)
,       .addr_in(dcache__addr_in)
,       .read_data_out(dcache__read_data_out)
,       .read_addr_out(dcache__read_addr_out)
,       .read_valid_out(dcache__read_valid_out)
,       .busy_out(dcache__busy_out)
,       .stall_in(dcache__stall_in)
,       .flush_in(dcache__flush_in)
,       .invalidate_in(dcache__invalidate_in)
,       .invalidate_line_in(dcache__invalidate_line_in)
,       .invalidate_addr_in(dcache__invalidate_addr_in)
,       .cache_disable_in(dcache__cache_disable_in)
,       .mem_out__read_out(dcache__mem_out__read_out)
,       .mem_out__write_out(dcache__mem_out__write_out)
,       .mem_out__addr_out(dcache__mem_out__addr_out)
,       .mem_out__write_data_out(dcache__mem_out__write_data_out)
,       .mem_out__write_mask_out(dcache__mem_out__write_mask_out)
,       .mem_out__cache_disable_out(dcache__mem_out__cache_disable_out)
,       .mem_out__read_data_in(dcache__mem_out__read_data_in)
,       .mem_out__wait_in(dcache__mem_out__wait_in)
,       .perf_out(dcache__perf_out)
,       .debugen_in(dcache__debugen_in)
    );
    wire bp__lookup_valid_in;
    wire[31:0] bp__lookup_pc_in;
    wire[31:0] bp__lookup_target_in;
    wire[31:0] bp__lookup_fallthrough_in;
    wire[4-1:0] bp__lookup_br_op_in;
    wire bp__predict_taken_out;
    wire[31:0] bp__predict_next_out;
    wire bp__update_valid_in;
    wire[31:0] bp__update_pc_in;
    wire bp__update_taken_in;
    wire[31:0] bp__update_target_in;
    BranchPredictor #(
        16
,       2
    ) bp (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .lookup_valid_in(bp__lookup_valid_in)
,       .lookup_pc_in(bp__lookup_pc_in)
,       .lookup_target_in(bp__lookup_target_in)
,       .lookup_fallthrough_in(bp__lookup_fallthrough_in)
,       .lookup_br_op_in(bp__lookup_br_op_in)
,       .predict_taken_out(bp__predict_taken_out)
,       .predict_next_out(bp__predict_next_out)
,       .update_valid_in(bp__update_valid_in)
,       .update_pc_in(bp__update_pc_in)
,       .update_taken_in(bp__update_taken_in)
,       .update_target_in(bp__update_target_in)
    );

    // tmp variables
    logic icache_invalidate_issued_reg_tmp;
    logic sfence_vma_issued_reg_tmp;
    logic sbi_arg_wait_reg_tmp;
    TribeSbiDecodeState sbi_decode_reg_tmp;
    logic[32-1:0] pc_tmp;
    logic valid_tmp;
    logic fetch_buffer_valid_reg_tmp;
    logic[32-1:0] fetch_instr_reg_tmp;
    logic[32-1:0] fetch_pc_reg_tmp;
    logic icache_flush_reg_tmp;
    State decode_state_reg_tmp;
    logic[32-1:0] decode_fallthrough_reg_tmp;
    logic load_result_pending_reg_tmp;
    logic load_retire_ready_reg_tmp;
    logic atomic_write_accepted_reg_tmp;
    State csr_commit_state_reg_tmp;
    logic csr_commit_fire_reg_tmp;
    logic csr_commit_serialized_reg_tmp;
    logic csr_commit_interrupt_valid_reg_tmp;
    logic[32-1:0] csr_commit_interrupt_cause_reg_tmp;
    logic csr_commit_interrupt_to_supervisor_reg_tmp;
    logic[32-1:0] alu_result_reg_tmp;
    State[2-1:0] state_reg_tmp;
    logic[2-1:0][32-1:0] predicted_next_reg_tmp;
    logic[2-1:0][32-1:0] fallthrough_reg_tmp;
    logic[2-1:0] predicted_taken_reg_tmp;
    logic[32-1:0] debug_alu_a_reg_tmp;
    logic[32-1:0] debug_alu_b_reg_tmp;
    logic[32-1:0] debug_branch_target_reg_tmp;
    logic debug_branch_taken_reg_tmp;
    logic output_write_active_reg_tmp;
    logic interrupt_entry_guard_reg_tmp;
    logic interrupt_accept_reg_tmp;
    logic[32-1:0] interrupt_cause_reg_tmp;
    logic interrupt_to_supervisor_reg_tmp;
    logic sbi_ret_a1_valid_reg_tmp;
    logic[32-1:0] sbi_ret_a1_reg_tmp;
    logic immu_result_valid_reg_tmp;
    logic[32-1:0] immu_vaddr_reg_tmp;
    logic[32-1:0] immu_paddr_reg_tmp;
    logic immu_fault_reg_tmp;
    logic dmmu_result_valid_reg_tmp;
    logic[32-1:0] dmmu_vaddr_reg_tmp;
    logic[32-1:0] dmmu_paddr_reg_tmp;
    logic dmmu_fault_reg_tmp;
    logic[2-1:0] l2_ptw_owner_reg_tmp;


    always_comb begin : dmmu_vaddr_comb_func  // dmmu_vaddr_comb_func
        dmmu_vaddr_comb=(exe_mem__mem_read_out) ? (unsigned'(32'(exe_mem__mem_read_addr_out))) : (unsigned'(32'(exe_mem__mem_write_addr_out)));
    end

    always_comb begin : dmmu_result_match_comb_func  // dmmu_result_match_comb_func
        logic live_request;
        logic memory_owner;
        live_request=((state_reg[64'h1].valid) != '0) && ((exe_mem__mem_read_out || exe_mem__mem_write_out));
        memory_owner=((state_reg[64'h1].valid) != '0) && (((live_request || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))) || (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))));
        dmmu_result_match_comb=(memory_owner && ((dmmu_result_valid_reg) != '0)) && ((!live_request || (unsigned'(32'(dmmu_vaddr_reg)) == dmmu_vaddr_comb)));
    end

    always_comb begin : csr_commit_serializing_comb_func  // csr_commit_serializing_comb_func
        csr_commit_serializing_comb=((csr_commit_state_reg.valid) != '0) && ((((signed'(32'(csr_commit_state_reg.csr_op)) != signed'(32'(Csr_pkg::CNONE))) || (signed'(32'(csr_commit_state_reg.sys_op)) != signed'(32'(Sys_pkg::SNONE)))) || (signed'(32'(csr_commit_state_reg.trap_op)) != signed'(32'(Trap_pkg::TNONE)))));
    end

    always_comb begin : csr_commit_wait_comb_func  // csr_commit_wait_comb_func
        csr_commit_wait_comb=csr__write_pending_out || ((csr_commit_serializing_comb && !((csr_commit_serialized_reg) != '0)));
    end

    always_comb begin : memory_stage_transport_wait_comb_func  // memory_stage_transport_wait_comb_func
        logic data_mem_access;
        logic dmmu_faulted_access;
        data_mem_access=((state_reg[64'h1].valid) != '0) && ((((exe_mem__mem_read_out || exe_mem__mem_write_out) || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))) || (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))));
        dmmu_faulted_access=0;
        dmmu_faulted_access=dmmu_result_match_comb && ((dmmu_fault_reg) != '0);
        memory_stage_transport_wait_comb=((((((data_mem_access && !dmmu_faulted_access) && exe_mem__atomic_busy_out)) || (((data_mem_access && !dmmu_faulted_access) && !dmmu_result_match_comb))) || (((data_mem_access && !dmmu_faulted_access) && exe_mem__mem_split_busy_out))) || ((((data_mem_access && !dmmu_faulted_access) && dcache__mem_out__read_out) && d_mem_out__wait_in))) || ((((data_mem_access && !dmmu_faulted_access) && ((exe_mem__mem_write_out || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))))) && d_mem_out__wait_in));
    end

    always_comb begin : interrupt_retire_wait_comb_func  // interrupt_retire_wait_comb_func
        logic dmmu_faulted_access;
        dmmu_faulted_access=0;
        dmmu_faulted_access=dmmu_result_match_comb && ((dmmu_fault_reg) != '0);
        interrupt_retire_wait_comb=memory_stage_transport_wait_comb || ((((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))) && !dmmu_faulted_access) && !((load_retire_ready_reg) != '0)));
    end

    always_comb begin : interrupt_entry_wait_comb_func  // interrupt_entry_wait_comb_func
        logic registered_store_pending;
        registered_store_pending=((state_reg[64'h1].valid) != '0) && ((exe_mem__mem_write_out || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))));
        interrupt_entry_wait_comb=(interrupt_retire_wait_comb || registered_store_pending) || csr_commit_wait_comb;
    end

    always_comb begin : interrupt_capture_comb_func  // interrupt_capture_comb_func
        logic trap_redirect;
        trap_redirect=((state_reg[64'h0].valid) != '0) && ((((((((signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::MRET))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::SRET)))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::ECALL)))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::EBREAK)))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::TRAP)))) || (signed'(32'(state_reg[64'h0].trap_op)) != signed'(32'(Trap_pkg::TNONE)))) || csr__illegal_trap_out));
        interrupt_capture_comb=(((((((state_reg[64'h0].valid) != '0) && irq__interrupt_valid_out) && !((interrupt_entry_guard_reg) != '0)) && !((interrupt_accept_reg) != '0)) && !trap_redirect) && (signed'(32'(state_reg[64'h0].amo_op)) == signed'(32'(Amo_pkg::AMONONE)))) && !interrupt_entry_wait_comb;
    end

    always_comb begin : immu_result_match_comb_func  // immu_result_match_comb_func
        immu_result_match_comb=(((valid) != '0) && ((immu_result_valid_reg) != '0)) && (unsigned'(32'(immu_vaddr_reg)) == unsigned'(32'(pc)));
    end

    always_comb begin : sbi_legacy_ecall_comb_func  // sbi_legacy_ecall_comb_func
        sbi_legacy_ecall_comb=(((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::ECALL)))) && (csr__priv_out == 2'(64'h1));
    end

    always_comb begin : register_write_commit_comb_func  // register_write_commit_comb_func
        register_write_commit_comb=wb__regs_write_out && (((signed'(32'(state_reg[64'h1].wb_op)) != signed'(32'(Wb_pkg::MEM))) || ((load_retire_ready_reg) != '0)));
    end

    always_comb begin : sbi_arg_hazard_comb_func  // sbi_arg_hazard_comb_func
        logic[31:0] rd;
        logic writes_argument;
        rd=wb__regs_wr_id_out;
        writes_argument=(((rd == 32'hA) || (rd == 32'hB)) || (rd == 32'h10)) || (rd == 32'h11);
        sbi_arg_hazard_comb=((sbi_legacy_ecall_comb && !((sbi_arg_wait_reg) != '0)) && register_write_commit_comb) && writes_argument;
    end

    always_comb begin : sbi_decode_wait_comb_func  // sbi_decode_wait_comb_func
        sbi_decode_wait_comb=sbi_legacy_ecall_comb && !((sbi_decode_reg.valid) != '0);
    end

    always_comb begin : fence_i_registered_store_pending_comb_func  // fence_i_registered_store_pending_comb_func
        fence_i_registered_store_pending_comb=((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::FENCEI)))) && ((state_reg[64'h1].valid) != '0)) && ((exe_mem__mem_write_out || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))));
    end

    always_comb begin : memory_wait_comb_func  // memory_wait_comb_func
        logic data_mem_access;
        logic next_data_mem_access;
        logic dmmu_faulted_access;
        data_mem_access=((state_reg[64'h1].valid) != '0) && ((((exe_mem__mem_read_out || exe_mem__mem_write_out) || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))) || (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))));
        next_data_mem_access=((state_reg[64'h0].valid) != '0) && (((signed'(32'(state_reg[64'h0].mem_op)) == signed'(32'(Mem_pkg::LOAD))) || (signed'(32'(state_reg[64'h0].mem_op)) == signed'(32'(Mem_pkg::STORE)))));
        dmmu_faulted_access=0;
        dmmu_faulted_access=dmmu_result_match_comb && ((dmmu_fault_reg) != '0);
        memory_wait_comb=((((((((((((((csr_commit_wait_comb || interrupt_capture_comb) || (((data_mem_access && !dmmu_faulted_access) && exe_mem__atomic_busy_out))) || (((((valid) != '0) && !immu_result_match_comb) && !data_mem_access))) || (((data_mem_access && !dmmu_faulted_access) && !dmmu_result_match_comb))) || sbi_arg_hazard_comb) || sbi_decode_wait_comb) || exe__multicycle_wait_out) || ((next_data_mem_access && dcache__busy_out))) || (((data_mem_access && !dmmu_faulted_access) && dcache__busy_out))) || (((data_mem_access && !dmmu_faulted_access) && exe_mem__mem_split_busy_out))) || ((((data_mem_access && !dmmu_faulted_access) && dcache__mem_out__read_out) && d_mem_out__wait_in))) || ((((data_mem_access && !dmmu_faulted_access) && ((exe_mem__mem_write_out || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))))) && d_mem_out__wait_in))) || (((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::FENCE)))) && (((dcache__busy_out || d_mem_out__wait_in) || i_mem_out__wait_in))))) || (((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::FENCEI)))) && (((fence_i_registered_store_pending_comb || !((icache_invalidate_issued_reg) != '0)) || icache__busy_out))))) || ((((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))) && !dmmu_faulted_access) && !((load_retire_ready_reg) != '0)));
    end

    always_comb begin : fetch_valid_comb_func  // fetch_valid_comb_func
        fetch_valid_comb=(((valid) != '0) && ((fetch_buffer_valid_reg) != '0)) && (unsigned'(32'(fetch_pc_reg)) == unsigned'(32'(pc)));
    end

    always_comb begin : debug_core_comb_func  // debug_core_comb_func
        debug_core_comb.pc=pc;
        debug_core_comb.fetch_valid=fetch_valid_comb;
        debug_core_comb.memory_wait=memory_wait_comb;
    end

    always_comb begin : dmmu_ptw_selected_comb_func  // dmmu_ptw_selected_comb_func
        dmmu_ptw_selected_comb=l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_DMMU));
    end

    always_comb begin : mmu_l2_read_word_comb_func  // mmu_l2_read_word_comb_func
        logic[31:0] addr;
        logic[31:0] lane;
        addr=(dmmu_ptw_selected_comb) ? (unsigned'(32'(dmmu__mem_addr_out))) : (unsigned'(32'(immu__mem_addr_out)));
        lane=((addr % 'h20))/'h4;
        mmu_l2_read_word_comb=d_mem_out__read_data_in[lane*32'h20 +:32];
    end

    always_comb begin : debug_mmu_comb_func  // debug_mmu_comb_func
        debug_mmu_comb.immu_ptw_read=immu__mem_read_out;
        debug_mmu_comb.immu_ptw_addr=immu__mem_addr_out;
        debug_mmu_comb.immu_busy=immu__busy_out;
        debug_mmu_comb.immu_fault=immu__fault_out;
        debug_mmu_comb.immu_paddr=immu__paddr_out;
        debug_mmu_comb.immu_last_addr=immu__debug_last_addr_out;
        debug_mmu_comb.immu_last_pte=immu__debug_last_pte_out;
        debug_mmu_comb.dmmu_ptw_read=dmmu__mem_read_out;
        debug_mmu_comb.dmmu_ptw_addr=dmmu__mem_addr_out;
        debug_mmu_comb.dmmu_busy=dmmu__busy_out;
        debug_mmu_comb.dmmu_fault=dmmu__fault_out;
        debug_mmu_comb.ptw_word=mmu_l2_read_word_comb;
    end

    always_comb begin : debug_cache_comb_func  // debug_cache_comb_func
        debug_cache_comb.icache_read_valid=icache__read_valid_out;
        debug_cache_comb.icache_read_addr=icache__read_addr_out;
        debug_cache_comb.icache_read_in=icache__read_in;
        debug_cache_comb.icache_stall_in=icache__stall_in;
        debug_cache_comb.dcache_read_valid=dcache__read_valid_out;
        debug_cache_comb.dcache_read_addr=dcache__read_addr_out;
        debug_cache_comb.dcache_read_data=dcache__read_data_out;
        debug_cache_comb.dcache_cpu_read=dcache__read_in;
        debug_cache_comb.dcache_cpu_write=dcache__write_in;
        debug_cache_comb.dcache_cpu_addr=dcache__addr_in;
        debug_cache_comb.dcache_cpu_wdata=dcache__write_data_in;
        debug_cache_comb.dcache_cpu_wmask=dcache__write_mask_in;
    end

    always_comb begin : debug_wb_comb_func  // debug_wb_comb_func
        debug_wb_comb.load_ready=wb_mem__load_ready_out;
        debug_wb_comb.mem_wait=(((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))) && !wb_mem__load_ready_out;
        debug_wb_comb.load_data_valid=wb_mem__debug_load_data_valid_out;
        debug_wb_comb.load_addr=wb_mem__debug_load_addr_out;
        debug_wb_comb.split_low_valid=wb_mem__debug_split_low_valid_out;
        debug_wb_comb.split_high_valid=wb_mem__debug_split_high_valid_out;
        debug_wb_comb.held_load_valid=wb_mem__debug_held_load_valid_out;
        debug_wb_comb.split_load_in=exe_mem__split_load_out;
        debug_wb_comb.alu_addr=wb_mem__alu_result_in;
        debug_wb_comb.state_pc=state_reg[64'h1].pc;
        debug_wb_comb.state_wb_op=state_reg[64'h1].wb_op;
        debug_wb_comb.state_mem_op=state_reg[64'h1].mem_op;
        debug_wb_comb.state_rd=state_reg[64'h1].rd;
        debug_wb_comb.state_funct3=state_reg[64'h1].funct3;
    end

    always_comb begin : debug_csr_comb_func  // debug_csr_comb_func
        debug_csr_comb.satp=csr__satp_out;
        debug_csr_comb.mstatus=csr__mstatus_out;
        debug_csr_comb.mtvec=csr__mtvec_out;
        debug_csr_comb.mepc=csr__mepc_out;
        debug_csr_comb.mcause=csr__mcause_out;
        debug_csr_comb.mtval=csr__mtval_out;
        debug_csr_comb.sepc=csr__sepc_out;
        debug_csr_comb.stvec=csr__stvec_out;
        debug_csr_comb.scause=csr__scause_out;
        debug_csr_comb.stval=csr__stval_out;
        debug_csr_comb.priv = csr__priv_out;
    end

    always_comb begin : debug_irq_comb_func  // debug_irq_comb_func
        debug_irq_comb.valid=irq__interrupt_valid_out;
        debug_irq_comb.cause=irq__interrupt_cause_out;
        debug_irq_comb.to_supervisor=irq__interrupt_to_supervisor_out;
        debug_irq_comb.mip=irq__mip_out;
        debug_irq_comb.mie=csr__mie_out;
        debug_irq_comb.mideleg=csr__mideleg_out;
    end

    always_comb begin : debug_regs_comb_func  // debug_regs_comb_func
        debug_regs_comb.ra=regs__x1_out;
        debug_regs_comb.write=wb__regs_write_out;
        debug_regs_comb.write_actual=register_write_commit_comb;
        debug_regs_comb.wr_id=wb__regs_wr_id_out;
        debug_regs_comb.data=wb__regs_data_out;
    end

    always_comb begin : debug_branch_comb_func  // debug_branch_comb_func
        debug_branch_comb.taken_now=exe__branch_taken_out;
        debug_branch_comb.target_now=exe__branch_target_out;
    end

    always_comb begin : debug_decode_comb_func  // debug_decode_comb_func
        debug_decode_comb.instr=fetch_instr_reg;
        debug_decode_comb.pc=dec__state_out.pc;
        debug_decode_comb.br=dec__state_out.br_op;
        debug_decode_comb.imm=dec__state_out.imm;
    end

    always_comb begin : sbi_decode_active_comb_func  // sbi_decode_active_comb_func
        sbi_decode_active_comb=((sbi_decode_reg.valid) != '0) && sbi_legacy_ecall_comb;
    end

    always_comb begin : sbi_set_timer_output_comb_func  // sbi_set_timer_output_comb_func
        sbi_set_timer_output_comb=sbi_decode_active_comb && ((sbi_decode_reg.set_timer) != '0);
    end

    always_comb begin : sbi_timer_lo_output_comb_func  // sbi_timer_lo_output_comb_func
        sbi_timer_lo_output_comb=sbi_decode_reg.a0;
    end

    always_comb begin : sbi_timer_hi_output_comb_func  // sbi_timer_hi_output_comb_func
        sbi_timer_hi_output_comb=sbi_decode_reg.a1;
    end

    always_comb begin : sbi_ecall_debug_comb_func  // sbi_ecall_debug_comb_func
        sbi_ecall_debug_comb=((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::ECALL)));
    end

    always_comb begin : sbi_decode_handled_comb_func  // sbi_decode_handled_comb_func
        sbi_decode_handled_comb=sbi_decode_active_comb && ((sbi_decode_reg.handled) != '0);
    end

    always_comb begin : debug_sbi_comb_func  // debug_sbi_comb_func
        debug_sbi_comb.ecall=sbi_ecall_debug_comb;
        debug_sbi_comb.a7=sbi_decode_reg.a7;
        debug_sbi_comb.a6=sbi_decode_reg.a6;
        debug_sbi_comb.a0=sbi_decode_reg.a0;
        debug_sbi_comb.base=sbi_decode_active_comb && ((sbi_decode_reg.base) != '0);
        debug_sbi_comb.noop=sbi_decode_active_comb && ((sbi_decode_reg.noop) != '0);
        debug_sbi_comb.handled=sbi_decode_handled_comb;
    end

    always_comb begin : hazard_stall_comb_func  // hazard_stall_comb_func
        hazard_stall_comb=0;
        if (((((decode_state_reg.valid) != '0) && ((state_reg[64'h0].valid) != '0)) && (((signed'(32'(state_reg[64'h0].wb_op)) == signed'(32'(Wb_pkg::MEM))) || (signed'(32'(state_reg[64'h0].wb_op)) == signed'(32'(Wb_pkg::ALU)))))) && (signed'(32'(state_reg[64'h0].rd)) != 32'sh0)) begin
            if (signed'(32'(state_reg[64'h0].rd)) == signed'(32'(decode_state_reg.rs1))) begin
                hazard_stall_comb=1;
            end
            if (signed'(32'(state_reg[64'h0].rd)) == signed'(32'(decode_state_reg.rs2))) begin
                hazard_stall_comb=1;
            end
        end
        if (exe_mem__mem_split_out || exe_mem__mem_split_busy_out) begin
            hazard_stall_comb=1;
        end
        if (exe_mem__atomic_busy_out) begin
            hazard_stall_comb=1;
        end
    end

    always_comb begin : exe_state_comb_func  // exe_state_comb_func
        exe_state_comb = state_reg[64'h0];
    end

    always_comb begin : branch_mispredict_comb_func  // branch_mispredict_comb_func
        branch_mispredict_comb=(((state_reg[64'h0].valid) != '0) && (signed'(32'(exe_state_comb.br_op)) != signed'(32'(Br_pkg::BNONE)))) && exe__branch_taken_out;
    end

    always_comb begin : branch_stall_comb_func  // branch_stall_comb_func
        branch_stall_comb=branch_mispredict_comb;
    end

    always_comb begin : perf_comb_func  // perf_comb_func
        perf_comb.hazard_stall=hazard_stall_comb;
        perf_comb.branch_stall=branch_stall_comb;
        perf_comb.dcache_wait=dcache__busy_out;
        perf_comb.icache_wait=icache__busy_out;
        perf_comb.icache = icache__perf_out;
        perf_comb.dcache = dcache__perf_out;
    end

    always_comb begin : interrupt_accept_comb_func  // interrupt_accept_comb_func
        interrupt_accept_comb=((interrupt_accept_reg) != '0) && ((state_reg[64'h0].valid) != '0);
    end

    always_comb begin : sfence_vma_comb_func  // sfence_vma_comb_func
        sfence_vma_comb=(((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::SFENCE_VMA)))) && !interrupt_retire_wait_comb) && !((sfence_vma_issued_reg) != '0));
    end

    always_comb begin : immu_ptw_selected_comb_func  // immu_ptw_selected_comb_func
        immu_ptw_selected_comb=l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_IMMU));
    end

    always_comb begin : decode_branch_target_comb_func  // decode_branch_target_comb_func
        decode_branch_target_comb=32'h0;
        if (signed'(32'(dec__state_out.br_op)) == signed'(32'(Br_pkg::JAL))) begin
            decode_branch_target_comb=dec__state_out.pc + dec__state_out.imm;
        end
        else begin
            if ((signed'(32'(dec__state_out.br_op)) == signed'(32'(Br_pkg::JALR))) || (signed'(32'(dec__state_out.br_op)) == signed'(32'(Br_pkg::JR)))) begin
                decode_branch_target_comb=((dec__state_out.rs1_val + dec__state_out.imm)) & ~'h1;
            end
            else begin
                decode_branch_target_comb=dec__state_out.pc + dec__state_out.imm;
            end
        end
    end

    always_comb begin : decode_fallthrough_comb_func  // decode_fallthrough_comb_func
        decode_fallthrough_comb=fetch_pc_reg + unsigned'(64'(signed'(32'((((((dec__instr_in & 32'h3)) == 32'h3)) ? ('h4) : ('h2))))));
    end

    always_comb begin : icache_invalidate_comb_func  // icache_invalidate_comb_func
        icache_invalidate_comb=((((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::FENCEI)))) && !interrupt_retire_wait_comb) && !fence_i_registered_store_pending_comb) && !((icache_invalidate_issued_reg) != '0));
    end

    always_comb begin : l2_data_addr_comb_func  // l2_data_addr_comb_func
        l2_data_addr_comb=(dmmu_ptw_selected_comb) ? (unsigned'(32'(dmmu__mem_addr_out))) : (((immu_ptw_selected_comb) ? (unsigned'(32'(immu__mem_addr_out))) : (unsigned'(32'(dcache__mem_out__addr_out)))));
    end

    generate  // _assign
        assign dec__pc_in = pc;
        assign dec__instr_in = fetch_instr_reg;
        assign dec__instr_valid_in = ((fetch_valid_comb) != '0);
        assign dec__regs_data0_in = 32'h0;
        assign dec__regs_data1_in = 32'h0;
        assign exe__state_in = exe_state_comb;
        assign exe__multicycle_state_in = state_reg[64'h0];
        assign exe_mem__state_in = exe_state_comb;
        assign exe_mem__alu_result_in = exe__mem_addr_out;
        assign exe_mem__transaction_owner_valid_in = ((state_reg[64'h1].valid) != '0);
        assign exe_mem__dcache_read_expected_addr_in = dmmu_paddr_reg;
        assign exe_mem__hold_in = ((memory_wait_comb) != '0);
        assign wb_mem__state_in = state_reg[64'h1];
        assign wb_mem__alu_result_in = (dcache__read_valid_out) ? (unsigned'(32'(dcache__read_addr_out))) : ((((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM))))) ? (unsigned'(32'(dmmu_paddr_reg))) : (unsigned'(32'(alu_result_reg)))));
        assign wb_mem__split_load_in = exe_mem__split_load_out;
        assign wb_mem__split_load_low_addr_in = exe_mem__split_load_low_out;
        assign wb_mem__split_load_high_addr_in = exe_mem__split_load_high_out;
        assign wb_mem__store_forward_enable_in = ((unsigned'(32'(wb_mem__alu_result_in)) < (((memory_base_in + mem_region_size_in['h0]) + mem_region_size_in['h1]) + mem_region_size_in['h2])) != '0);
        assign wb_mem__retire_in = load_retire_ready_reg;
        assign irq__mstatus_in = csr__mstatus_out;
        assign irq__mie_in = csr__mie_out;
        assign irq__mideleg_in = csr__mideleg_out;
        assign irq__mip_sw_in = csr__mip_sw_out;
        assign irq__priv_in = csr__priv_out;
        assign irq__clint_msip_in = clint_msip_in;
        assign irq__clint_mtip_in = clint_mtip_in;
        assign irq__external_irq_in = external_irq_in;
        assign csr__state_in = csr_commit_state_reg;
        assign csr__commit_in = csr_commit_fire_reg;
        assign csr__read_state_in = state_reg[64'h0];
        assign csr__trap_check_state_in = state_reg[64'h0];
        assign csr__legality_state_in = dec__state_out;
        assign csr__redirect_state_in = state_reg[64'h0];
        assign csr__reset_priv_in = boot_priv_in;
        assign csr__hartid_in = boot_hartid_in;
        assign csr__time_lo_in = time_lo_in;
        assign csr__time_hi_in = time_hi_in;
        assign csr__interrupt_valid_in = csr_commit_interrupt_valid_reg;
        assign csr__interrupt_cause_in = csr_commit_interrupt_cause_reg;
        assign csr__interrupt_to_supervisor_in = csr_commit_interrupt_to_supervisor_reg;
        assign csr__redirect_interrupt_valid_in = interrupt_accept_comb;
        assign csr__redirect_interrupt_cause_in = interrupt_cause_reg;
        assign csr__redirect_interrupt_to_supervisor_in = interrupt_to_supervisor_reg;
        assign csr__irq_pending_bits_in = irq__mip_out;
        assign immu__vaddr_in = unsigned'(32'(pc));
        assign immu__read_in = ((0) != '0);
        assign immu__write_in = ((0) != '0);
        assign immu__execute_in = ((((valid) != '0) && !immu_result_match_comb) != '0);
        assign immu__satp_in = csr__satp_out;
        assign immu__priv_in = csr__priv_out;
        assign immu__sum_in = ((0) != '0);
        assign immu__mxr_in = ((0) != '0);
        assign immu__direct_base_in = 32'h0;
        assign immu__direct_size_in = 32'h0;
        assign immu__fill_in = ((0) != '0);
        assign immu__fill_index_in = 3'(64'h0);
        assign immu__fill_vpn_in = 32'h0;
        assign immu__fill_ppn_in = 32'h0;
        assign immu__fill_flags_in = 8'h0;
        assign immu__sfence_in = ((sfence_vma_comb) != '0);
        assign immu__mem_read_data_in = mmu_l2_read_word_comb;
        assign immu__mem_wait_in = ((!immu_ptw_selected_comb || d_mem_out__wait_in) != '0);
        assign dmmu__vaddr_in = (exe_mem__mem_read_out) ? (unsigned'(32'(exe_mem__mem_read_addr_out))) : (unsigned'(32'(exe_mem__mem_write_addr_out)));
        assign dmmu__read_in = ((((state_reg[64'h1].valid) != '0) && exe_mem__mem_read_out) != '0);
        assign dmmu__write_in = ((((state_reg[64'h1].valid) != '0) && exe_mem__mem_write_out) != '0);
        assign dmmu__execute_in = ((0) != '0);
        assign dmmu__satp_in = csr__satp_out;
        assign dmmu__priv_in = csr__priv_out;
        assign dmmu__sum_in = ((((unsigned'(32'(csr__mstatus_out)) & (('h1 <<< 'h12)))) != 32'h0) != '0);
        assign dmmu__mxr_in = ((((unsigned'(32'(csr__mstatus_out)) & (('h1 <<< 'h13)))) != 32'h0) != '0);
        assign dmmu__direct_base_in = ((memory_base_in + mem_region_size_in['h0]) + mem_region_size_in['h1]) + mem_region_size_in['h2];
        assign dmmu__direct_size_in = mem_region_size_in['h3];
        assign dmmu__fill_in = ((0) != '0);
        assign dmmu__fill_index_in = 3'(64'h0);
        assign dmmu__fill_vpn_in = 32'h0;
        assign dmmu__fill_ppn_in = 32'h0;
        assign dmmu__fill_flags_in = 8'h0;
        assign dmmu__sfence_in = ((sfence_vma_comb) != '0);
        assign dmmu__mem_read_data_in = mmu_l2_read_word_comb;
        assign dmmu__mem_wait_in = ((!dmmu_ptw_selected_comb || d_mem_out__wait_in) != '0);
        assign wb__state_in = state_reg[64'h1];
        assign wb__mem_data_in = wb_mem__load_raw_out;
        assign wb__mem_data_hi_in = 32'h0;
        assign wb__mem_addr_in = ((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM))))) ? (unsigned'(32'(dmmu_paddr_reg))) : (unsigned'(32'(alu_result_reg)));
        assign wb__mem_split_in = ((0) != '0);
        assign wb__alu_result_in = alu_result_reg;
        assign regs__read_addr0_in = decode_state_reg.rs1;
        assign regs__read_addr1_in = decode_state_reg.rs2;
        assign regs__write_in = register_write_commit_comb;
        assign regs__write_addr_in = wb__regs_wr_id_out;
        assign regs__write_data_in = wb__regs_data_out;
        assign regs__write2_in = ((sbi_ret_a1_valid_reg) != '0);
        assign regs__write2_addr_in = 8'hB;
        assign regs__write2_data_in = unsigned'(32'(sbi_ret_a1_reg));
        assign regs__reset_x10_in = boot_hartid_in;
        assign regs__reset_x11_in = boot_dtb_addr_in;
        assign regs__debugen_in=debugen_in;
        assign dcache__read_in = (((((((((state_reg[64'h1].valid) != '0) && exe_mem__mem_read_out) && !dcache__busy_out) && !wb_mem__load_ready_out) && ((exe_mem__split_load_out) ? (!(((((unsigned'(32'(dcache__addr_in)) == unsigned'(32'(exe_mem__split_load_low_out))) && wb_mem__debug_split_low_valid_out)) || (((unsigned'(32'(dcache__addr_in)) == unsigned'(32'(exe_mem__split_load_high_out))) && wb_mem__debug_split_high_valid_out))))) : (!wb_mem__debug_held_load_valid_out))) && dmmu_result_match_comb) && !((dmmu_fault_reg) != '0)) != '0);
        assign dcache__write_in = ((((((((state_reg[64'h1].valid) != '0) && exe_mem__mem_write_out) && !dcache__busy_out) && !(((signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM))) && ((atomic_write_accepted_reg) != '0)))) && dmmu_result_match_comb) && !((dmmu_fault_reg) != '0)) != '0);
        assign dcache__addr_in = dmmu_paddr_reg;
        assign dcache__write_data_in = exe_mem__mem_write_data_out;
        assign dcache__write_mask_in = exe_mem__mem_write_mask_out;
        assign dcache__stall_in = ((0) != '0);
        assign dcache__flush_in = ((0) != '0);
        assign dcache__invalidate_in = external_cache_invalidate_in;
        assign dcache__cache_disable_in = ((dcache__addr_in>=(((memory_base_in + mem_region_size_in['h0]) + mem_region_size_in['h1]) + mem_region_size_in['h2]) && (unsigned'(32'(dcache__addr_in)) < (memory_base_in + memory_size_in))) != '0);
        assign dcache__debugen_in=debugen_in;
        assign exe_mem__dcache_read_valid_in = dcache__read_valid_out;
        assign exe_mem__dcache_read_addr_in = dcache__read_addr_out;
        assign exe_mem__dcache_read_data_in = dcache__read_data_out;
        assign exe_mem__mem_stall_in = ((dcache__busy_out) != '0);
        assign wb_mem__dcache_read_valid_in = dcache__read_valid_out;
        assign wb_mem__dcache_read_addr_in = dcache__read_addr_out;
        assign wb_mem__dcache_read_data_in = dcache__read_data_out;
        assign wb_mem__dcache_write_valid_in = dcache__mem_out__write_out;
        assign wb_mem__dcache_write_addr_in = dcache__mem_out__addr_out;
        assign wb_mem__dcache_write_data_in = dcache__mem_out__write_data_out;
        assign wb_mem__dcache_write_mask_in = dcache__mem_out__write_mask_out;
        assign bp__lookup_valid_in = ((0) != '0);
        assign bp__lookup_pc_in = dec__state_out.pc;
        assign bp__lookup_target_in = decode_branch_target_comb;
        assign bp__lookup_fallthrough_in = decode_fallthrough_comb;
        assign bp__lookup_br_op_in = 4'(64'(dec__state_out.br_op));
        assign bp__update_valid_in = (((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].br_op)) != signed'(32'(Br_pkg::BNONE)))) && !memory_wait_comb) != '0);
        assign bp__update_pc_in = state_reg[64'h0].pc;
        assign bp__update_taken_in = ((exe__branch_taken_out) != '0);
        assign bp__update_target_in = exe__branch_target_out;
        assign icache__read_in = (((((valid) != '0) && immu_result_match_comb) && !((immu_fault_reg) != '0)) != '0);
        assign icache__addr_in = immu_paddr_reg;
        assign icache__write_in = ((0) != '0);
        assign icache__write_data_in = 32'h0;
        assign icache__write_mask_in = 8'h0;
        assign icache__stall_in = ((0) != '0);
        assign icache__flush_in = icache_flush_reg;
        assign icache__invalidate_in = icache_invalidate_comb;
        assign icache__cache_disable_in = ((0) != '0);
        assign icache__debugen_in=debugen_in;
        assign i_mem_out__read_out = icache__mem_out__read_out;
        assign i_mem_out__write_out = ((0) != '0);
        assign i_mem_out__addr_out = icache__mem_out__addr_out;
        assign i_mem_out__write_data_out = 32'h0;
        assign i_mem_out__write_mask_out = 8'h0;
        assign i_mem_out__cache_disable_out = ((0) != '0);
        assign d_mem_out__read_out = (((dmmu_ptw_selected_comb) ? (dmmu__mem_read_out) : (((immu_ptw_selected_comb) ? (immu__mem_read_out) : (dcache__mem_out__read_out)))) != '0);
        assign d_mem_out__write_out = (((l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_NONE))) && dcache__mem_out__write_out) != '0);
        assign d_mem_out__addr_out = l2_data_addr_comb;
        assign d_mem_out__write_data_out = dcache__mem_out__write_data_out;
        assign d_mem_out__write_mask_out = dcache__mem_out__write_mask_out;
        assign d_mem_out__cache_disable_out = ((l2_data_addr_comb>=(((memory_base_in + mem_region_size_in['h0]) + mem_region_size_in['h1]) + mem_region_size_in['h2]) && (l2_data_addr_comb < (memory_base_in + memory_size_in))) != '0);
        assign dcache__mem_out__read_data_in = d_mem_out__read_data_in;
        assign dcache__mem_out__wait_in = (((l2_ptw_owner_reg != unsigned'(64'(L2_PTW_OWNER_NONE))) || d_mem_out__wait_in) != '0);
        assign icache__mem_out__read_data_in = i_mem_out__read_data_in;
        assign icache__mem_out__wait_in = i_mem_out__wait_in;
        assign dmem_write_out = dcache__mem_out__write_out;
        assign dmem_write_data_out = dcache__mem_out__write_data_out;
        assign dmem_write_mask_out = dcache__mem_out__write_mask_out;
        assign dmem_read_out = dcache__mem_out__read_out;
        assign dmem_addr_out = dcache__mem_out__addr_out;
        assign imem_read_addr_out = icache__mem_out__addr_out;
    endgenerate

    always_comb begin : branch_flush_comb_func  // branch_flush_comb_func
        branch_flush_comb=branch_mispredict_comb;
    end

    always_comb begin : stall_comb_func  // stall_comb_func
        stall_comb=hazard_stall_comb || branch_stall_comb;
    end

    always_comb begin : immu_active_fault_comb_func  // immu_active_fault_comb_func
        immu_active_fault_comb=immu_result_match_comb && ((immu_fault_reg) != '0);
    end

    always_comb begin : dmmu_access_ready_comb_func  // dmmu_access_ready_comb_func
        logic access;
        access=((state_reg[64'h1].valid) != '0) && ((exe_mem__mem_read_out || exe_mem__mem_write_out));
        dmmu_access_ready_comb=((!access || !dmmu__translated_out) || dmmu__hit_out) || dmmu__fault_out;
    end

    always_comb begin : xret_redirect_comb_func  // xret_redirect_comb_func
        xret_redirect_comb=((((state_reg[64'h0].valid) != '0) && (((signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::MRET))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::SRET)))))) && !interrupt_retire_wait_comb) && !csr_commit_wait_comb;
    end

    always_comb begin : sync_trap_redirect_comb_func  // sync_trap_redirect_comb_func
        sync_trap_redirect_comb=((((((state_reg[64'h0].valid) != '0) && !csr_commit_wait_comb) && ((!interrupt_retire_wait_comb || interrupt_accept_comb))) && !sbi_decode_wait_comb) && !sbi_decode_handled_comb) && ((((((interrupt_accept_comb || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::ECALL)))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::EBREAK)))) || (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::TRAP)))) || (signed'(32'(state_reg[64'h0].trap_op)) != signed'(32'(Trap_pkg::TNONE)))) || csr__illegal_trap_out));
    end

    always_comb begin : icache_response_match_comb_func  // icache_response_match_comb_func
        icache_response_match_comb=((((valid) != '0) && icache__read_valid_out) && immu_result_match_comb) && (icache__read_addr_out == unsigned'(32'(immu_paddr_reg)));
    end

    always_comb begin : decode_branch_valid_comb_func  // decode_branch_valid_comb_func
        decode_branch_valid_comb=((((fetch_valid_comb && ((dec__state_out.valid) != '0)) && (signed'(32'(dec__state_out.br_op)) != signed'(32'(Br_pkg::BNONE)))) && (signed'(32'(dec__state_out.br_op)) != signed'(32'(Br_pkg::JALR)))) && (signed'(32'(dec__state_out.br_op)) != signed'(32'(Br_pkg::JR)))) && !stall_comb;
    end

    always_comb begin : decode_indirect_branch_valid_comb_func  // decode_indirect_branch_valid_comb_func
        decode_indirect_branch_valid_comb=((fetch_valid_comb && ((dec__state_out.valid) != '0)) && (((signed'(32'(dec__state_out.br_op)) == signed'(32'(Br_pkg::JALR))) || (signed'(32'(dec__state_out.br_op)) == signed'(32'(Br_pkg::JR)))))) && !stall_comb;
    end

    always_comb begin : branch_actual_next_comb_func  // branch_actual_next_comb_func
        branch_actual_next_comb=(exe__branch_taken_out) ? (exe__branch_target_out) : (unsigned'(32'(fallthrough_reg[64'h0])));
    end

    always_comb begin : fetch_addr_comb_func  // fetch_addr_comb_func
        fetch_addr_comb=pc;
    end

    function logic[31:0] sbi_arg_value (input logic[7:0] reg_id);
        if (signed'(32'(reg_id)) == 32'shA) begin
            return regs__x10_out;
        end
        if (signed'(32'(reg_id)) == 32'shB) begin
            return regs__x11_out;
        end
        if (signed'(32'(reg_id)) == 32'sh10) begin
            return regs__x16_out;
        end
        if (signed'(32'(reg_id)) == 32'sh11) begin
            return regs__x17_out;
        end
        return 32'h0;
    endfunction

    always_comb begin : sbi_noop_comb_func  // sbi_noop_comb_func
        logic[31:0] ext;
        ext=sbi_arg_value(8'h11);
        sbi_noop_comb=sbi_legacy_ecall_comb && (((((ext == 32'h5) || (ext == 32'h6)) || (ext == 32'h7)) || (ext == unsigned'(32'(SBI_EXT_RFENCE)))));
    end

    always_comb begin : sbi_base_comb_func  // sbi_base_comb_func
        sbi_base_comb=sbi_legacy_ecall_comb && (sbi_arg_value(8'h11) == unsigned'(32'(SBI_EXT_BASE)));
    end

    always_comb begin : sbi_set_timer_comb_func  // sbi_set_timer_comb_func
        logic[31:0] ext;
        ext=sbi_arg_value(8'h11);
        sbi_set_timer_comb=sbi_legacy_ecall_comb && (((ext == 32'h0) || (((ext == unsigned'(32'(SBI_EXT_TIME))) && (sbi_arg_value(8'h10) == 32'h0)))));
    end

    always_comb begin : sbi_ret_value_comb_func  // sbi_ret_value_comb_func
        logic[31:0] fid;
        logic[31:0] ext;
        logic[31:0] probe_ext;
        fid=sbi_arg_value(8'h10);
        ext=sbi_arg_value(8'h11);
        probe_ext=sbi_arg_value(8'hA);
        sbi_ret_value_comb=32'h0;
        if (ext == unsigned'(32'(SBI_EXT_BASE))) begin
            if (fid == 32'h0) begin
                sbi_ret_value_comb=32'h2;
            end
            else begin
                if (fid == 32'h1) begin
                    sbi_ret_value_comb=32'h0;
                end
                else begin
                    if (fid == 32'h2) begin
                        sbi_ret_value_comb=32'h1;
                    end
                    else begin
                        if (fid == 32'h3) begin
                            sbi_ret_value_comb=((((probe_ext == unsigned'(32'(SBI_EXT_BASE))) || (probe_ext == unsigned'(32'(SBI_EXT_TIME)))) || (probe_ext == unsigned'(32'(SBI_EXT_RFENCE))))) ? ('h1) : ('h0);
                        end
                        else begin
                            sbi_ret_value_comb=32'h0;
                        end
                    end
                end
            end
        end
    end

    always_comb begin : sbi_writes_a1_comb_func  // sbi_writes_a1_comb_func
        sbi_writes_a1_comb=sbi_base_comb || sbi_set_timer_comb;
    end

    always_comb begin : sbi_handled_comb_func  // sbi_handled_comb_func
        sbi_handled_comb=(sbi_set_timer_comb || sbi_noop_comb) || sbi_base_comb;
    end

    always_comb begin : sbi_timer_lo_comb_func  // sbi_timer_lo_comb_func
        sbi_timer_lo_comb=sbi_arg_value(8'hA);
    end

    always_comb begin : sbi_timer_hi_comb_func  // sbi_timer_hi_comb_func
        sbi_timer_hi_comb=sbi_arg_value(8'hB);
    end

    always_comb begin : sbi_registered_noop_comb_func  // sbi_registered_noop_comb_func
        logic[31:0] ext;
        ext=sbi_decode_reg.a7;
        sbi_registered_noop_comb=(((ext == 32'h5) || (ext == 32'h6)) || (ext == 32'h7)) || (ext == unsigned'(32'(SBI_EXT_RFENCE)));
    end

    always_comb begin : sbi_registered_base_comb_func  // sbi_registered_base_comb_func
        sbi_registered_base_comb=sbi_decode_reg.a7 == unsigned'(64'(SBI_EXT_BASE));
    end

    always_comb begin : sbi_registered_set_timer_comb_func  // sbi_registered_set_timer_comb_func
        sbi_registered_set_timer_comb=(sbi_decode_reg.a7 == 64'h0) || (((sbi_decode_reg.a7 == unsigned'(64'(SBI_EXT_TIME))) && (sbi_decode_reg.a6 == 64'h0)));
    end

    always_comb begin : sbi_registered_writes_a1_comb_func  // sbi_registered_writes_a1_comb_func
        sbi_registered_writes_a1_comb=sbi_registered_base_comb || sbi_registered_set_timer_comb;
    end

    always_comb begin : sbi_registered_handled_comb_func  // sbi_registered_handled_comb_func
        sbi_registered_handled_comb=(sbi_registered_set_timer_comb || sbi_registered_noop_comb) || sbi_registered_base_comb;
    end

    always_comb begin : sbi_registered_ret_value_comb_func  // sbi_registered_ret_value_comb_func
        logic[31:0] value;
        value=32'h0;
        if (sbi_decode_reg.a7 == unsigned'(64'(SBI_EXT_BASE))) begin
            if (sbi_decode_reg.a6 == 64'h0) begin
                value=32'h2;
            end
            else begin
                if (sbi_decode_reg.a6 == 64'h1) begin
                    value=32'h0;
                end
                else begin
                    if (sbi_decode_reg.a6 == 64'h2) begin
                        value=32'h1;
                    end
                    else begin
                        if (sbi_decode_reg.a6 == 64'h3) begin
                            value=((((sbi_decode_reg.a0 == unsigned'(64'(SBI_EXT_BASE))) || (sbi_decode_reg.a0 == unsigned'(64'(SBI_EXT_TIME)))) || (sbi_decode_reg.a0 == unsigned'(64'(SBI_EXT_RFENCE))))) ? ('h1) : ('h0);
                        end
                    end
                end
            end
        end
        sbi_registered_ret_value_comb=value;
    end

    always_comb begin : sbi_a7_debug_comb_func  // sbi_a7_debug_comb_func
        sbi_a7_debug_comb=sbi_arg_value(8'h11);
    end

    always_comb begin : sbi_a6_debug_comb_func  // sbi_a6_debug_comb_func
        sbi_a6_debug_comb=sbi_arg_value(8'h10);
    end

    always_comb begin : sbi_a0_debug_comb_func  // sbi_a0_debug_comb_func
        sbi_a0_debug_comb=sbi_arg_value(8'hA);
    end

    always_comb begin : inst_page_fault_vector_comb_func  // inst_page_fault_vector_comb_func
        logic[31:0] tvec;
        tvec=(((unsigned'(32'(csr__priv_out)) != 'h3) && ((((((unsigned'(32'(csr__medeleg_out)) >>> 32'shC)) & 'h1))) != '0))) ? (unsigned'(32'(csr__stvec_out))) : (unsigned'(32'(csr__mtvec_out)));
        inst_page_fault_vector_comb=tvec & ~'h3;
    end

    always_comb begin : data_page_fault_vector_comb_func  // data_page_fault_vector_comb_func
        logic[31:0] cause;
        logic[31:0] tvec;
        cause=(exe_mem__mem_write_out) ? ('hF) : ('hD);
        tvec=(((unsigned'(32'(csr__priv_out)) != 'h3) && ((((((unsigned'(32'(csr__medeleg_out)) >>> cause)) & 'h1))) != '0))) ? (unsigned'(32'(csr__stvec_out))) : (unsigned'(32'(csr__mtvec_out)));
        data_page_fault_vector_comb=tvec & ~'h3;
    end

    always_comb begin : dmmu_active_fault_comb_func  // dmmu_active_fault_comb_func
        dmmu_active_fault_comb=dmmu_result_match_comb && ((dmmu_fault_reg) != '0);
    end

    always_comb begin : csr_state_comb_func  // csr_state_comb_func
        csr_state_comb = exe_state_comb;
        if (sbi_decode_wait_comb || sbi_decode_handled_comb) begin
            csr_state_comb.sys_op=Sys_pkg::SNONE;
            csr_state_comb.trap_op=Trap_pkg::TNONE;
            csr_state_comb.csr_op=Csr_pkg::CNONE;
        end
        if ((immu_active_fault_comb && !((state_reg[64'h0].valid) != '0)) && !((state_reg[64'h1].valid) != '0)) begin
            csr_state_comb = 0;
            csr_state_comb.valid=1;
            csr_state_comb.pc=fetch_addr_comb;
            csr_state_comb.imm=fetch_addr_comb;
            csr_state_comb.sys_op=Sys_pkg::TRAP;
            csr_state_comb.trap_op=Trap_pkg::INST_PAGE_FAULT;
            csr_state_comb.csr_op=Csr_pkg::CNONE;
            csr_state_comb.mem_op=Mem_pkg::MNONE;
            csr_state_comb.wb_op=Wb_pkg::WNONE;
            csr_state_comb.br_op=Br_pkg::JR;
        end
        if (dmmu_active_fault_comb) begin
            csr_state_comb = 0;
            csr_state_comb.valid=1;
            csr_state_comb.pc=state_reg[64'h1].pc;
            csr_state_comb.imm=(exe_mem__mem_read_out) ? (unsigned'(32'(exe_mem__mem_read_addr_out))) : (unsigned'(32'(exe_mem__mem_write_addr_out)));
            csr_state_comb.sys_op=Sys_pkg::TRAP;
            csr_state_comb.trap_op=(exe_mem__mem_write_out) ? (Trap_pkg::STORE_PAGE_FAULT) : (Trap_pkg::LOAD_PAGE_FAULT);
            csr_state_comb.csr_op=Csr_pkg::CNONE;
            csr_state_comb.mem_op=Mem_pkg::MNONE;
            csr_state_comb.wb_op=Wb_pkg::WNONE;
            csr_state_comb.br_op=Br_pkg::JR;
        end
        if (interrupt_accept_comb || csr__illegal_trap_out) begin
            csr_state_comb = state_reg[64'h0];
            if (interrupt_accept_comb) begin
                csr_state_comb.imm=32'h0;
            end
            csr_state_comb.sys_op=Sys_pkg::TRAP;
            csr_state_comb.trap_op=(interrupt_accept_comb) ? (Trap_pkg::TNONE) : (Trap_pkg::ILLEGAL_INST);
            csr_state_comb.csr_op=Csr_pkg::CNONE;
            csr_state_comb.mem_op=Mem_pkg::MNONE;
            csr_state_comb.wb_op=Wb_pkg::WNONE;
            csr_state_comb.br_op=Br_pkg::JR;
        end
        if (((interrupt_retire_wait_comb && !interrupt_accept_comb) && !immu_active_fault_comb) && !dmmu_active_fault_comb) begin
            csr_state_comb.valid=0;
        end
    end

    task forward ();
    begin: forward
        if ((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::ALU)))) && (signed'(32'(state_reg[64'h1].rd)) != 32'sh0)) begin
            if (signed'(32'(decode_state_reg.rs1)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs1_val=alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(alu_result_reg)));
                end
            end
            if (signed'(32'(decode_state_reg.rs2)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs2_val=alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(alu_result_reg)));
                end
            end
        end
        if (wb_mem__load_ready_out && (signed'(32'(state_reg[64'h1].rd)) != 32'sh0)) begin
            if (signed'(32'(decode_state_reg.rs1)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs1_val=wb_mem__load_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from MEM to RS1\n", unsigned'(32'(wb_mem__load_result_out)));
                end
            end
            if (signed'(32'(decode_state_reg.rs2)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs2_val=wb_mem__load_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from MEM to RS2\n", unsigned'(32'(wb_mem__load_result_out)));
                end
            end
        end
        if ((((state_reg[64'h1].valid) != '0) && (((signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::PC2))) || (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::PC4)))))) && (signed'(32'(state_reg[64'h1].rd)) != 32'sh0)) begin
            logic[31:0] link_value; link_value = state_reg[64'h1].pc + unsigned'(32'((((signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::PC2)))) ? ('h2) : ('h4))));
            if (signed'(32'(decode_state_reg.rs1)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs1_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS1\n", link_value);
                end
            end
            if (signed'(32'(decode_state_reg.rs2)) == signed'(32'(state_reg[64'h1].rd))) begin
                state_reg_tmp[64'h0].rs2_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS2\n", link_value);
                end
            end
        end
        if ((((state_reg[64'h0].valid) != '0) && (((signed'(32'(state_reg[64'h0].wb_op)) == signed'(32'(Wb_pkg::PC2))) || (signed'(32'(state_reg[64'h0].wb_op)) == signed'(32'(Wb_pkg::PC4)))))) && (signed'(32'(state_reg[64'h0].rd)) != 32'sh0)) begin
            logic[31:0] link_value; link_value = state_reg[64'h0].pc + unsigned'(32'((((signed'(32'(state_reg[64'h0].wb_op)) == signed'(32'(Wb_pkg::PC2)))) ? ('h2) : ('h4))));
            if (signed'(32'(decode_state_reg.rs1)) == signed'(32'(state_reg[64'h0].rd))) begin
                state_reg_tmp[64'h0].rs1_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS1\n", link_value);
                end
            end
            if (signed'(32'(decode_state_reg.rs2)) == signed'(32'(state_reg[64'h0].rd))) begin
                state_reg_tmp[64'h0].rs2_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS2\n", link_value);
                end
            end
        end
    end
    endtask

    function logic signed[31:0] Rv32i___sext (
        input Rv32i _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        logic signed[31:0] m;
        m = signed'(32'('h1 <<< ((bits - 32'h1))));
        return signed'(32'(((val ^ unsigned'(32'(m)))) - unsigned'(32'(m))));
    endfunction

    function logic signed[31:0] Rv32i___imm_I (input Rv32i _this);
        return Rv32i___sext(_this, 32'(_this._.i.imm11_0), 32'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_S (input Rv32i _this);
        return Rv32i___sext(_this, unsigned'(32'(signed'(32'(_this._.s.imm4_0)) | ((signed'(32'(_this._.s.imm11_5)) <<< 'h5)))), 32'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_B (input Rv32i _this);
        return Rv32i___sext(_this, unsigned'(32'(((((signed'(32'(_this._.b.imm4_1)) <<< 'h1)) | ((signed'(32'(_this._.b.imm11)) <<< 'hB))) | ((signed'(32'(_this._.b.imm10_5)) <<< 'h5))) | ((signed'(32'(_this._.b.imm12)) <<< 'hC)))), 32'hD);
    endfunction

    function logic signed[31:0] Rv32i___imm_J (input Rv32i _this);
        return Rv32i___sext(_this, unsigned'(32'(((((signed'(32'(_this._.j.imm10_1)) <<< 'h1)) | ((signed'(32'(_this._.j.imm11)) <<< 'hB))) | ((signed'(32'(_this._.j.imm19_12)) <<< 'hC))) | ((signed'(32'(_this._.j.imm20)) <<< 'h14)))), 32'h15);
    endfunction

    function logic signed[31:0] Rv32i___imm_U (input Rv32i _this);
        return signed'(32'(signed'(32'(_this._.u.imm31_12)) <<< 'hC));
    endfunction

    task Rv32i___decode (
        input Rv32i _this
,       output State state_out
    );
    begin: Rv32i___decode
        state_out = 0;
        if (signed'(32'(_this._.r.opcode)) == 32'sh3) begin
            state_out.rd=_this._.i.rd;
            state_out.imm=Rv32i___imm_I(_this);
            state_out.mem_op=Mem_pkg::LOAD;
            state_out.alu_op=Alu_pkg::ADD;
            state_out.wb_op=Wb_pkg::MEM;
            state_out.funct3=_this._.i.funct3;
            state_out.rs1=_this._.i.rs1;
        end
        else begin
            if (signed'(32'(_this._.r.opcode)) == 32'sh23) begin
                state_out.imm=Rv32i___imm_S(_this);
                state_out.mem_op=Mem_pkg::STORE;
                state_out.alu_op=Alu_pkg::ADD;
                state_out.funct3=_this._.s.funct3;
                state_out.rs1=_this._.s.rs1;
                state_out.rs2=_this._.s.rs2;
            end
            else begin
                if (signed'(32'(_this._.r.opcode)) == 32'sh13) begin
                    state_out.rd=_this._.i.rd;
                    state_out.imm=Rv32i___imm_I(_this);
                    state_out.wb_op=Wb_pkg::ALU;
                    case (signed'(32'(_this._.i.funct3)))
                    'h0: begin
                        state_out.alu_op=Alu_pkg::ADD;
                    end
                    'h2: begin
                        state_out.alu_op=Alu_pkg::SLT;
                    end
                    'h3: begin
                        state_out.alu_op=Alu_pkg::SLTU;
                    end
                    'h4: begin
                        state_out.alu_op=Alu_pkg::XOR;
                    end
                    'h6: begin
                        state_out.alu_op=Alu_pkg::OR;
                    end
                    'h7: begin
                        state_out.alu_op=Alu_pkg::AND;
                    end
                    'h1: begin
                        state_out.alu_op=Alu_pkg::SLL;
                    end
                    'h5: begin
                        state_out.alu_op=(((((signed'(32'(_this._.i.imm11_0)) >>> 32'shA)) & 'h1) != '0)) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                    end
                    endcase
                    state_out.funct3=_this._.i.funct3;
                    state_out.rs1=_this._.i.rs1;
                end
                else begin
                    if (signed'(32'(_this._.r.opcode)) == 32'sh33) begin
                        state_out.rd=_this._.r.rd;
                        state_out.wb_op=Wb_pkg::ALU;
                        case (signed'(32'(_this._.r.funct3)))
                        'h0: begin
                            state_out.alu_op=((signed'(32'(_this._.r.funct7)) == 32'sh20)) ? (Alu_pkg::SUB) : (Alu_pkg::ADD);
                        end
                        'h7: begin
                            state_out.alu_op=((signed'(32'(_this._.r.funct7)) == 32'sh1)) ? (Alu_pkg::REM) : (Alu_pkg::AND);
                        end
                        'h6: begin
                            state_out.alu_op=Alu_pkg::OR;
                        end
                        'h4: begin
                            state_out.alu_op=Alu_pkg::XOR;
                        end
                        'h1: begin
                            state_out.alu_op=Alu_pkg::SLL;
                        end
                        'h5: begin
                            state_out.alu_op=((signed'(32'(_this._.r.funct7)) == 32'sh20)) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                        end
                        'h2: begin
                            state_out.alu_op=Alu_pkg::SLT;
                        end
                        'h3: begin
                            state_out.alu_op=Alu_pkg::SLTU;
                        end
                        endcase
                        state_out.funct3=_this._.r.funct3;
                        state_out.rs1=_this._.r.rs1;
                        state_out.rs2=_this._.r.rs2;
                    end
                    else begin
                        if (signed'(32'(_this._.r.opcode)) == 32'sh63) begin
                            state_out.imm=Rv32i___imm_B(_this);
                            state_out.br_op=Br_pkg::BNONE;
                            case (signed'(32'(_this._.b.funct3)))
                            'h0: begin
                                state_out.br_op=Br_pkg::BEQ;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h1: begin
                                state_out.br_op=Br_pkg::BNE;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h4: begin
                                state_out.br_op=Br_pkg::BLT;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h5: begin
                                state_out.br_op=Br_pkg::BGE;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h6: begin
                                state_out.br_op=Br_pkg::BLTU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h7: begin
                                state_out.br_op=Br_pkg::BGEU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            endcase
                            state_out.funct3=_this._.b.funct3;
                            state_out.rs1=_this._.b.rs1;
                            state_out.rs2=_this._.b.rs2;
                        end
                        else begin
                            if (signed'(32'(_this._.r.opcode)) == 32'sh6F) begin
                                state_out.rd=_this._.j.rd;
                                state_out.imm=Rv32i___imm_J(_this);
                                state_out.br_op=Br_pkg::JAL;
                                state_out.wb_op=Wb_pkg::PC4;
                            end
                            else begin
                                if (signed'(32'(_this._.r.opcode)) == 32'sh67) begin
                                    state_out.rd=_this._.i.rd;
                                    state_out.imm=Rv32i___imm_I(_this);
                                    state_out.br_op=Br_pkg::JALR;
                                    state_out.wb_op=Wb_pkg::PC4;
                                    state_out.rs1=_this._.i.rs1;
                                end
                                else begin
                                    if (signed'(32'(_this._.r.opcode)) == 32'sh37) begin
                                        state_out.rd=_this._.u.rd;
                                        state_out.imm=Rv32i___imm_U(_this);
                                        state_out.alu_op=Alu_pkg::PASS;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (signed'(32'(_this._.r.opcode)) == 32'sh17) begin
                                            state_out.rd=_this._.u.rd;
                                            state_out.imm=Rv32i___imm_U(_this);
                                            state_out.alu_op=Alu_pkg::ADD;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (signed'(32'(_this._.r.opcode)) == 32'shF) begin
                                                if (signed'(32'(_this._.i.funct3)) == 32'sh1) begin
                                                    state_out.sys_op=Sys_pkg::FENCEI;
                                                    state_out.br_op=Br_pkg::BNONE;
                                                end
                                                else begin
                                                    if (signed'(32'(_this._.i.funct3)) == 32'sh0) begin
                                                        state_out.sys_op=Sys_pkg::FENCE;
                                                        state_out.br_op=Br_pkg::BNONE;
                                                    end
                                                    else begin
                                                        state_out.sys_op=Sys_pkg::TRAP;
                                                        state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                                        state_out.imm=_this._.raw;
                                                        state_out.br_op=Br_pkg::BNONE;
                                                    end
                                                end
                                            end
                                            else begin
                                                state_out.sys_op=Sys_pkg::TRAP;
                                                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                                state_out.imm=_this._.raw;
                                                state_out.br_op=Br_pkg::BNONE;
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    endtask

    function logic[31:0] Rv32ic___bits (
        input Rv32ic _this
,       input logic signed[31:0] hi
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & (((('h1 <<< (((hi - lo) + 'h1)))) - 32'h1));
    endfunction

    function logic[31:0] Rv32ic___bit (
        input Rv32ic _this
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & 32'h1;
    endfunction

    task Rv32ic___decode (
        input Rv32ic _this
,       output State state_out
    );
    begin: Rv32ic___decode
        logic signed[31:0] imm_tmp;
        logic[31:0] opcode;
        logic[31:0] funct3;
        logic[31:0] rd_p;
        logic[31:0] rs1_p;
        logic[31:0] bits6_5;
        logic[31:0] bits11_10;
        logic[31:0] b12;
        logic[31:0] rd_rs1;
        logic[31:0] rs2;
        state_out = 0;
        if (((_this._.raw & 32'h3)) == 32'h3) begin
            Rv32i___decode(_this, state_out);
            disable Rv32ic___decode;
        end
        opcode=Rv32ic___bits(_this, 'h1, 'h0);
        funct3=Rv32ic___bits(_this, 'hF, 'hD);
        rd_p=Rv32ic___bits(_this, 'h4, 'h2);
        rs1_p=Rv32ic___bits(_this, 'h9, 'h7);
        bits6_5=Rv32ic___bits(_this, 'h6, 'h5);
        bits11_10=Rv32ic___bits(_this, 'hB, 'hA);
        b12=Rv32ic___bit(_this, 'hC);
        rd_rs1=Rv32ic___bits(_this, 'hB, 'h7);
        rs2=Rv32ic___bits(_this, 'h6, 'h2);
        state_out.funct3=8'h2;
        if (opcode == 32'h0) begin
            if (funct3 == 32'h0) begin
                state_out.rd=rd_p + 32'h8;
                state_out.rs1=8'h2;
                state_out.imm=((((Rv32ic___bits(_this, 'hA, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hB) <<< 'h4))) | ((Rv32ic___bit(_this, 'h5) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                state_out.alu_op=Alu_pkg::ADD;
                state_out.wb_op=Wb_pkg::ALU;
            end
            else begin
                if (funct3 == 32'h2) begin
                    state_out.rd=rd_p + 32'h8;
                    state_out.rs1=rs1_p + 32'h8;
                    state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
                else begin
                    if (funct3 == 32'h6) begin
                        state_out.rs1=rs1_p + 32'h8;
                        state_out.rs2=rd_p + 32'h8;
                        state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                        state_out.alu_op=Alu_pkg::ADD;
                        state_out.mem_op=Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (opcode == 32'h1) begin
                if (funct3 == 32'h0) begin
                    state_out.rd=rd_rs1;
                    state_out.rs1=rd_rs1;
                    imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                    imm_tmp=signed'(32'(((imm_tmp <<< 'h1A)))) >>> 32'sh1A;
                    state_out.imm=imm_tmp;
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.wb_op=Wb_pkg::ALU;
                end
                else begin
                    if (funct3 == 32'h1) begin
                        state_out.rd=8'h1;
                        state_out.wb_op=Wb_pkg::PC2;
                        state_out.br_op=Br_pkg::JAL;
                        state_out.imm=Rv32i___sext(_this, ((((((((b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 32'hC);
                    end
                    else begin
                        if (funct3 == 32'h2) begin
                            state_out.rd=rd_rs1;
                            imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                            imm_tmp=signed'(32'(((imm_tmp <<< 'h1A)))) >>> 32'sh1A;
                            state_out.imm=imm_tmp;
                            state_out.alu_op=Alu_pkg::PASS;
                            state_out.wb_op=Wb_pkg::ALU;
                        end
                        else begin
                            if (funct3 == 32'h3) begin
                                if (rd_rs1 == 32'h2) begin
                                    state_out.rd=8'h2;
                                    state_out.rs1=8'h2;
                                    imm_tmp=((((((Rv32ic___bit(_this, 'hC) <<< 'h9)) | ((Rv32ic___bit(_this, 'h4) <<< 'h8))) | ((Rv32ic___bit(_this, 'h3) <<< 'h7))) | ((Rv32ic___bit(_this, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'h6) <<< 'h4));
                                    state_out.imm=Rv32i___sext(_this, unsigned'(32'(imm_tmp)), 32'hA);
                                    state_out.alu_op=Alu_pkg::ADD;
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                                else begin
                                    state_out.rd=rd_rs1;
                                    imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                                    imm_tmp=signed'(32'(((imm_tmp <<< 'h1A)))) >>> 32'shE;
                                    state_out.imm=imm_tmp;
                                    state_out.alu_op=Alu_pkg::PASS;
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                            end
                            else begin
                                if (funct3 == 32'h4) begin
                                    if (bits11_10 == 32'h0) begin
                                        state_out.rd=rs1_p + 32'h8;
                                        state_out.rs1=rs1_p + 32'h8;
                                        state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                        state_out.alu_op=Alu_pkg::SRL;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (bits11_10 == 32'h1) begin
                                            state_out.rd=rs1_p + 32'h8;
                                            state_out.rs1=rs1_p + 32'h8;
                                            state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                            state_out.alu_op=Alu_pkg::SRA;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (bits11_10 == 32'h2) begin
                                                state_out.rd=rs1_p + 32'h8;
                                                state_out.rs1=rs1_p + 32'h8;
                                                imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                                                imm_tmp=signed'(32'(((imm_tmp <<< 'h1A)))) >>> 32'sh1A;
                                                state_out.imm=imm_tmp;
                                                state_out.alu_op=Alu_pkg::AND;
                                                state_out.wb_op=Wb_pkg::ALU;
                                            end
                                            else begin
                                                if ((bits11_10 == 32'h3) && (b12 == 32'h0)) begin
                                                    state_out.rd=rs1_p + 32'h8;
                                                    state_out.rs1=rs1_p + 32'h8;
                                                    state_out.rs2=rd_p + 32'h8;
                                                    state_out.alu_op=(bits6_5 == 32'h0) ? (Alu_pkg::SUB) : (((bits6_5 == 32'h1) ? (Alu_pkg::XOR) : (((bits6_5 == 32'h2) ? (Alu_pkg::OR) : (Alu_pkg::AND)))));
                                                    state_out.wb_op=Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (funct3 == 32'h5) begin
                                        state_out.rd=8'h0;
                                        state_out.br_op=Br_pkg::JAL;
                                        state_out.imm=Rv32i___sext(_this, ((((((((b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 32'hC);
                                    end
                                    else begin
                                        if (funct3 == 32'h6) begin
                                            state_out.rs1=rs1_p + 32'h8;
                                            state_out.br_op=Br_pkg::BEQZ;
                                            state_out.alu_op=Alu_pkg::SLTU;
                                            state_out.imm=(((((b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                            if (((b12) != '0)) begin
                                                state_out.imm|=32'(~'h1FF);
                                            end
                                        end
                                        else begin
                                            if (funct3 == 32'h7) begin
                                                state_out.rs1=rs1_p + 32'h8;
                                                state_out.br_op=Br_pkg::BNEZ;
                                                state_out.alu_op=Alu_pkg::SLTU;
                                                state_out.imm=(((((b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                                if (((b12) != '0)) begin
                                                    state_out.imm|=32'(~'h1FF);
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
            else begin
                if (opcode == 32'h2) begin
                    if (funct3 == 32'h0) begin
                        state_out.rd=rd_rs1;
                        state_out.rs1=rd_rs1;
                        state_out.imm=((b12 <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                        state_out.alu_op=Alu_pkg::SLL;
                        state_out.wb_op=Wb_pkg::ALU;
                    end
                    else begin
                        if (funct3 == 32'h2) begin
                            state_out.rd=rd_rs1;
                            state_out.rs1=8'h2;
                            state_out.imm=(((b12 <<< 'h5)) | ((Rv32ic___bits(_this, 'h6, 'h4) <<< 'h2))) | ((Rv32ic___bits(_this, 'h3, 'h2) <<< 'h6));
                            state_out.alu_op=Alu_pkg::ADD;
                            state_out.mem_op=Mem_pkg::LOAD;
                            state_out.wb_op=Wb_pkg::MEM;
                        end
                        else begin
                            if (funct3 == 32'h4) begin
                                if (((rs2 == 32'h0) && (rd_rs1 == 32'h0)) && (b12 == 32'h1)) begin
                                    state_out.sys_op=Sys_pkg::EBREAK;
                                    state_out.trap_op=Trap_pkg::BREAKPOINT;
                                end
                                else begin
                                    if (rs2 != 32'h0) begin
                                        state_out.rd=rd_rs1;
                                        state_out.rs2=rs2;
                                        if (b12 == 32'h0) begin
                                            state_out.alu_op=Alu_pkg::PASS;
                                        end
                                        else begin
                                            state_out.rs1=rd_rs1;
                                            state_out.alu_op=Alu_pkg::ADD;
                                        end
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if ((rs2 == 32'h0) && (b12 == 32'h0)) begin
                                            state_out.rs1=rd_rs1;
                                            state_out.br_op=Br_pkg::JR;
                                            state_out.wb_op=Wb_pkg::PC2;
                                        end
                                        else begin
                                            if ((rs2 == 32'h0) && (b12 == 32'h1)) begin
                                                state_out.rs1=rd_rs1;
                                                state_out.rd=8'h1;
                                                state_out.br_op=Br_pkg::JALR;
                                                state_out.wb_op=Wb_pkg::PC2;
                                            end
                                        end
                                    end
                                end
                            end
                            else begin
                                if (funct3 == 32'h6) begin
                                    state_out.rs1=8'h2;
                                    state_out.rs2=rs2;
                                    state_out.imm=((Rv32ic___bits(_this, 'h8, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'h9) <<< 'h2));
                                    state_out.mem_op=Mem_pkg::STORE;
                                    state_out.alu_op=Alu_pkg::ADD;
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    endtask

    task Rv32im___decode (
        input Rv32im _this
,       output State state_out
    );
    begin: Rv32im___decode
        state_out = 0;
        Rv32ic___decode(_this, state_out);
        if ((signed'(32'(_this._.r.opcode)) == 32'sh33) && (signed'(32'(_this._.r.funct7)) == 32'sh1)) begin
            state_out = 0;
            state_out.rd=_this._.r.rd;
            state_out.wb_op=Wb_pkg::ALU;
            case (signed'(32'(_this._.r.funct3)))
            'h0: begin
                state_out.alu_op=Alu_pkg::MUL;
            end
            'h1: begin
                state_out.alu_op=Alu_pkg::MULH;
            end
            'h2: begin
                state_out.alu_op=Alu_pkg::MULHSU;
            end
            'h3: begin
                state_out.alu_op=Alu_pkg::MULHU;
            end
            'h4: begin
                state_out.alu_op=Alu_pkg::DIV;
            end
            'h5: begin
                state_out.alu_op=Alu_pkg::DIVU;
            end
            'h6: begin
                state_out.alu_op=Alu_pkg::REM;
            end
            'h7: begin
                state_out.alu_op=Alu_pkg::REMU;
            end
            endcase
            state_out.funct3=_this._.r.funct3;
            state_out.rs1=_this._.r.rs1;
            state_out.rs2=_this._.r.rs2;
        end
    end
    endtask

    function logic[7:0] Rv32ia___funct5 (input Rv32ia _this);
        return 8'((_this._.raw >>> 32'sh1B));
    endfunction

    task Rv32ia___decode (
        input Rv32ia _this
,       output State state_out
    );
    begin: Rv32ia___decode
        state_out = 0;
        Rv32im___decode(_this, state_out);
        if (((((_this._.raw & 32'h3)) == 32'h3) && (signed'(32'(_this._.r.opcode)) == 32'sh2F)) && (signed'(32'(_this._.r.funct3)) == 32'sh2)) begin
            state_out = 0;
            state_out.rd=_this._.r.rd;
            state_out.rs1=_this._.r.rs1;
            state_out.rs2=_this._.r.rs2;
            state_out.funct3=_this._.r.funct3;
            state_out.imm=32'h0;
            state_out.alu_op=Alu_pkg::ADD;
            case (signed'(32'(Rv32ia___funct5(_this))))
            signed'(32'(unsigned'(8'h2))): begin
                if (signed'(32'(_this._.r.rs2)) == 32'sh0) begin
                    state_out.amo_op=Amo_pkg::LR_W;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
            end
            signed'(32'(unsigned'(8'h3))): begin
                state_out.amo_op=Amo_pkg::SC_W;
                state_out.mem_op=Mem_pkg::STORE;
                state_out.wb_op=Wb_pkg::ALU;
            end
            signed'(32'(unsigned'(8'h1))): begin
                state_out.amo_op=Amo_pkg::AMOSWAP_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h0))): begin
                state_out.amo_op=Amo_pkg::AMOADD_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h4))): begin
                state_out.amo_op=Amo_pkg::AMOXOR_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'hC))): begin
                state_out.amo_op=Amo_pkg::AMOAND_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h8))): begin
                state_out.amo_op=Amo_pkg::AMOOR_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h10))): begin
                state_out.amo_op=Amo_pkg::AMOMIN_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h14))): begin
                state_out.amo_op=Amo_pkg::AMOMAX_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h18))): begin
                state_out.amo_op=Amo_pkg::AMOMINU_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            signed'(32'(unsigned'(8'h1C))): begin
                state_out.amo_op=Amo_pkg::AMOMAXU_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            default: begin
                state_out = 0;
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            endcase
            if (signed'(32'(state_out.amo_op)) == signed'(32'(Amo_pkg::AMONONE))) begin
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
        end
    end
    endtask

    task Zicsr___decode (
        input Zicsr _this
,       output State state_out
    );
    begin: Zicsr___decode
        state_out = 0;
        Rv32ia___decode(_this, state_out);
        if (((((_this._.raw & 32'h3)) == 32'h3) && (signed'(32'(_this._.r.opcode)) == 32'sh73)) && (signed'(32'(_this._.i.funct3)) == 32'sh0)) begin
            if (_this._.raw == 32'h73) begin
                state_out = 0;
                state_out.sys_op=Sys_pkg::ECALL;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            else begin
                if (_this._.raw == 32'h100073) begin
                    state_out = 0;
                    state_out.sys_op=Sys_pkg::EBREAK;
                    state_out.trap_op=Trap_pkg::BREAKPOINT;
                    state_out.imm=_this._.raw;
                    state_out.br_op=Br_pkg::BNONE;
                end
                else begin
                    if (_this._.raw == 32'h30200073) begin
                        state_out = 0;
                        state_out.sys_op=Sys_pkg::MRET;
                        state_out.imm=32'h0;
                        state_out.br_op=Br_pkg::BNONE;
                    end
                    else begin
                        if (_this._.raw == 32'h10200073) begin
                            state_out = 0;
                            state_out.sys_op=Sys_pkg::SRET;
                            state_out.imm=32'h0;
                            state_out.br_op=Br_pkg::BNONE;
                        end
                        else begin
                            if (_this._.raw == 32'h10500073) begin
                                state_out = 0;
                                state_out.sys_op=Sys_pkg::WFI;
                                state_out.imm=_this._.raw;
                            end
                            else begin
                                if (((_this._.raw & 'hFE007FFF)) == 32'h12000073) begin
                                    state_out = 0;
                                    state_out.sys_op=Sys_pkg::SFENCE_VMA;
                                    state_out.rs1=_this._.r.rs1;
                                    state_out.rs2=_this._.r.rs2;
                                    state_out.imm=_this._.raw;
                                end
                                else begin
                                    state_out = 0;
                                    state_out.sys_op=Sys_pkg::TRAP;
                                    state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                    state_out.imm=_this._.raw;
                                    state_out.br_op=Br_pkg::BNONE;
                                end
                            end
                        end
                    end
                end
            end
        end
        if (((((_this._.raw & 32'h3)) == 32'h3) && (signed'(32'(_this._.r.opcode)) == 32'sh73)) && (signed'(32'(_this._.i.funct3)) != 32'sh0)) begin
            state_out = 0;
            state_out.rd=_this._.i.rd;
            state_out.funct3=_this._.i.funct3;
            state_out.csr_addr=_this._.i.imm11_0;
            state_out.csr_imm=_this._.i.rs1;
            state_out.imm=_this._.raw;
            case (signed'(32'(_this._.i.funct3)))
            'h1: begin
                state_out.csr_op=Csr_pkg::CSRRW;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h2: begin
                state_out.csr_op=Csr_pkg::CSRRS;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h3: begin
                state_out.csr_op=Csr_pkg::CSRRC;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h5: begin
                state_out.csr_op=Csr_pkg::CSRRWI;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h6: begin
                state_out.csr_op=Csr_pkg::CSRRSI;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h7: begin
                state_out.csr_op=Csr_pkg::CSRRCI;
                state_out.wb_op=(((_this._.i.rd) != '0)) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            default: begin
                state_out = 0;
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            endcase
        end
    end
    endtask

    function string Rv32i___mnemonic (input Rv32i _this);
        logic[31:0] op;
        logic[31:0] f3;
        logic[31:0] f7;
        op=_this._.r.opcode;
        f3=_this._.r.funct3;
        f7=_this._.r.funct7;
        case (op)
        32'h33: begin
            if ((f3 == 32'h0) && (f7 == 32'h0)) begin
                return "add   ";
            end
            if ((f3 == 32'h0) && (f7 == 32'h20)) begin
                return "sub   ";
            end
            if ((f3 == 32'h0) && (f7 == 32'h1)) begin
                return "mul   ";
            end
            if ((f3 == 32'h7) && (f7 == 32'h1)) begin
                return "remu  ";
            end
            if (f3 == 32'h7) begin
                return "and   ";
            end
            if (f3 == 32'h6) begin
                return "or    ";
            end
            if (f3 == 32'h4) begin
                return "xor   ";
            end
            if (f3 == 32'h1) begin
                return "sll   ";
            end
            if ((f3 == 32'h5) && (f7 == 32'h0)) begin
                return "srl   ";
            end
            if ((f3 == 32'h5) && (f7 == 32'h20)) begin
                return "sra   ";
            end
            if ((f3 == 32'h5) && (f7 == 32'h1)) begin
                return "divu  ";
            end
            if (f3 == 32'h2) begin
                return "slt   ";
            end
            if ((f3 == 32'h3) && (f7 == 32'h1)) begin
                return "mulhu ";
            end
            if (f3 == 32'h3) begin
                return "sltu  ";
            end
            return "r-type";
        end
        32'h13: begin
            if (f3 == 32'h0) begin
                return "addi  ";
            end
            if (f3 == 32'h7) begin
                return "andi  ";
            end
            if (f3 == 32'h6) begin
                return "ori   ";
            end
            if (f3 == 32'h4) begin
                return "xori  ";
            end
            if (f3 == 32'h1) begin
                return "slli  ";
            end
            if ((f3 == 32'h5) && (f7 == 32'h0)) begin
                return "srli  ";
            end
            if ((f3 == 32'h5) && (f7 == 32'h20)) begin
                return "srai  ";
            end
            if (f3 == 32'h2) begin
                return "slti  ";
            end
            if (f3 == 32'h3) begin
                return "sltiu ";
            end
            return "aluimm";
        end
        32'h3: begin
            return "load  ";
        end
        32'h23: begin
            return "store ";
        end
        32'h63: begin
            return "branch";
        end
        32'h6F: begin
            return "jal   ";
        end
        32'h67: begin
            return "jalr  ";
        end
        32'h37: begin
            return "lui   ";
        end
        32'h17: begin
            return "auipc ";
        end
        32'hF: begin
            return (f3 == 32'h1) ? ("fencei") : ("fence ");
        end
        default: begin
            return "unknwn";
        end
        endcase
        return "unknwn";
    endfunction

    function string Rv32ic___mnemonic (input Rv32ic _this);
        logic[7:0] op;
        logic[7:0] f3;
        logic[7:0] b12;
        logic[7:0] rs2;
        logic[7:0] bits6_5;
        logic[7:0] bits11_10;
        if (((_this._.raw & 32'h3)) == 32'h3) begin
            return Rv32i___mnemonic(_this);
        end
        op=Rv32ic___bits(_this, 'h1, 'h0);
        f3=Rv32ic___bits(_this, 'hF, 'hD);
        b12=Rv32ic___bit(_this, 'hC);
        bits6_5=Rv32ic___bits(_this, 'h6, 'h5);
        bits11_10=Rv32ic___bits(_this, 'hB, 'hA);
        rs2=Rv32ic___bits(_this, 'h6, 'h2);
        case (signed'(32'(op)))
        'h0: begin
            case (signed'(32'(f3)))
            'h0: begin
                return "addi4s";
            end
            'h2: begin
                return "lw    ";
            end
            'h6: begin
                return "sw    ";
            end
            'h3: begin
                return "ld    ";
            end
            'h7: begin
                return "sd    ";
            end
            default: begin
                return "rsrvd ";
            end
            endcase
            case (signed'(32'(f3)))
            'h0: begin
                return "addi  ";
            end
            'h1: begin
                return "jal   ";
            end
            'h2: begin
                return "li    ";
            end
            'h3: begin
                return "addisp";
            end
            'h4: begin
                if (signed'(32'(bits11_10)) == 32'sh0) begin
                    return "srli  ";
                end
                if (signed'(32'(bits11_10)) == 32'sh1) begin
                    return "srai  ";
                end
                if (signed'(32'(bits11_10)) == 32'sh2) begin
                    return "andi  ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh0)) begin
                    return "sub   ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh1)) begin
                    return "xor   ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh2)) begin
                    return "or    ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh3)) begin
                    return "and   ";
                end
                return "illgl ";
            end
            'h5: begin
                return "j     ";
            end
            'h6: begin
                return "beqz  ";
            end
            'h7: begin
                return "bnez  ";
            end
            endcase
            case (signed'(32'(f3)))
            'h0: begin
                return "slli  ";
            end
            'h1: begin
                return "fldsp ";
            end
            'h2: begin
                return "lwsp  ";
            end
            'h4: begin
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "mv    ";
                end
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "add   ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "jr    ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "jalr  ";
                end
                return "illgl ";
            end
            'h6: begin
                return "swsp  ";
            end
            'h3: begin
                return "ldsp  ";
            end
            'h7: begin
                return "sdsp  ";
            end
            default: begin
                return "rsrvd ";
            end
            endcase
        end
        'h1: begin
            case (signed'(32'(f3)))
            'h0: begin
                return "addi  ";
            end
            'h1: begin
                return "jal   ";
            end
            'h2: begin
                return "li    ";
            end
            'h3: begin
                return "addisp";
            end
            'h4: begin
                if (signed'(32'(bits11_10)) == 32'sh0) begin
                    return "srli  ";
                end
                if (signed'(32'(bits11_10)) == 32'sh1) begin
                    return "srai  ";
                end
                if (signed'(32'(bits11_10)) == 32'sh2) begin
                    return "andi  ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh0)) begin
                    return "sub   ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh1)) begin
                    return "xor   ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh2)) begin
                    return "or    ";
                end
                if (((signed'(32'(bits11_10)) == 32'sh3) && (signed'(32'(b12)) == 32'sh0)) && (signed'(32'(bits6_5)) == 32'sh3)) begin
                    return "and   ";
                end
                return "illgl ";
            end
            'h5: begin
                return "j     ";
            end
            'h6: begin
                return "beqz  ";
            end
            'h7: begin
                return "bnez  ";
            end
            endcase
            case (signed'(32'(f3)))
            'h0: begin
                return "slli  ";
            end
            'h1: begin
                return "fldsp ";
            end
            'h2: begin
                return "lwsp  ";
            end
            'h4: begin
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "mv    ";
                end
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "add   ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "jr    ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "jalr  ";
                end
                return "illgl ";
            end
            'h6: begin
                return "swsp  ";
            end
            'h3: begin
                return "ldsp  ";
            end
            'h7: begin
                return "sdsp  ";
            end
            default: begin
                return "rsrvd ";
            end
            endcase
        end
        'h2: begin
            case (signed'(32'(f3)))
            'h0: begin
                return "slli  ";
            end
            'h1: begin
                return "fldsp ";
            end
            'h2: begin
                return "lwsp  ";
            end
            'h4: begin
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "mv    ";
                end
                if ((signed'(32'(rs2)) != 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "add   ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh0)) begin
                    return "jr    ";
                end
                if ((signed'(32'(rs2)) == 32'sh0) && (signed'(32'(b12)) == 32'sh1)) begin
                    return "jalr  ";
                end
                return "illgl ";
            end
            'h6: begin
                return "swsp  ";
            end
            'h3: begin
                return "ldsp  ";
            end
            'h7: begin
                return "sdsp  ";
            end
            default: begin
                return "rsrvd ";
            end
            endcase
        end
        endcase
        return "unknwn";
    endfunction

    function string Rv32im___mnemonic (input Rv32im _this);
        if ((signed'(32'(_this._.r.opcode)) == 32'sh33) && (signed'(32'(_this._.r.funct7)) == 32'sh1)) begin
            if (signed'(32'(_this._.r.funct3)) == 32'sh0) begin
                return "mul   ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh1) begin
                return "mulh  ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh2) begin
                return "mulhsu";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh3) begin
                return "mulhu ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh4) begin
                return "div   ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh5) begin
                return "divu  ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh6) begin
                return "rem   ";
            end
            if (signed'(32'(_this._.r.funct3)) == 32'sh7) begin
                return "remu  ";
            end
        end
        return Rv32ic___mnemonic(_this);
    endfunction

    function string Rv32ia___mnemonic (input Rv32ia _this);
        if (((((_this._.raw & 32'h3)) == 32'h3) && (signed'(32'(_this._.r.opcode)) == 32'sh2F)) && (signed'(32'(_this._.r.funct3)) == 32'sh2)) begin
            if ((signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h2)))) && (signed'(32'(_this._.r.rs2)) == 32'sh0)) begin
                return "lr.w  ";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h3)))) begin
                return "sc.w  ";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h1)))) begin
                return "amoswp";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h0)))) begin
                return "amoadd";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h4)))) begin
                return "amoxor";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'hC)))) begin
                return "amoand";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h8)))) begin
                return "amoor ";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h10)))) begin
                return "amomin";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h14)))) begin
                return "amomax";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h18)))) begin
                return "amomiu";
            end
            if (signed'(32'(Rv32ia___funct5(_this))) == signed'(32'(unsigned'(8'h1C)))) begin
                return "amomau";
            end
        end
        return Rv32im___mnemonic(_this);
    endfunction

    function string Zicsr___mnemonic (input Zicsr _this);
        if ((((_this._.raw & 32'h3)) == 32'h3) && (signed'(32'(_this._.r.opcode)) == 32'sh73)) begin
            if (_this._.raw == 32'h73) begin
                return "ecall ";
            end
            if (_this._.raw == 32'h100073) begin
                return "ebreak";
            end
            if (_this._.raw == 32'h30200073) begin
                return "mret  ";
            end
            if (_this._.raw == 32'h10200073) begin
                return "sret  ";
            end
            if (_this._.raw == 32'h10500073) begin
                return "wfi   ";
            end
            if (((_this._.raw & 'hFE007FFF)) == 32'h12000073) begin
                return "sfence";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh1) begin
                return "csrrw ";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh2) begin
                return "csrrs ";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh3) begin
                return "csrrc ";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh5) begin
                return "csrrwi";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh6) begin
                return "csrrsi";
            end
            if (signed'(32'(_this._.i.funct3)) == 32'sh7) begin
                return "csrrci";
            end
        end
        return Rv32ia___mnemonic(_this);
    endfunction

    task debug ();
    begin: debug
        State tmp;
        Zicsr instr;
        instr = '{unsigned'(32'(fetch_instr_reg))};
        Zicsr___decode(instr, tmp);
        $write("(%d/%d)%x st[h%x b%x dc%x ic%x is%x ds%x ih%x]: [%s]%08x  rs%02d/%02d,imm:%08x,rd%02d => (%d)ops:%02d/%x/%x/%x sys%x rs%02d/%02d:%08x/%08x,imm:%08x,alu:%09x,rd%02d br(%d)%08x => mem(%d/%d@%08x)%08x/%01x (%d)wop(%x),r(%d)%08x@%02d", ((valid) != '0), ((stall_comb) != '0), pc, ((hazard_stall_comb) != '0), ((branch_stall_comb) != '0), ((dcache__busy_out) != '0), ((icache__busy_out) != '0), unsigned'(32'(icache__perf_out.state)), unsigned'(32'(dcache__perf_out.state)), ((icache__perf_out.hit) != '0), Zicsr___mnemonic(instr), (((instr._.raw & 32'h3)) == 32'h3) ? (instr._.raw) : ((instr._.raw | 'hFFFF0000)), signed'(32'(tmp.rs1)), signed'(32'(tmp.rs2)), tmp.imm, signed'(32'(tmp.rd)), ((state_reg[64'h0].valid) != '0), state_reg[64'h0].alu_op, state_reg[64'h0].mem_op, state_reg[64'h0].br_op, state_reg[64'h0].wb_op, state_reg[64'h0].sys_op, signed'(32'(state_reg[64'h0].rs1)), signed'(32'(state_reg[64'h0].rs2)), state_reg[64'h0].rs1_val, state_reg[64'h0].rs2_val, state_reg[64'h0].imm, exe__alu_result_out, signed'(32'(state_reg[64'h0].rd)), ((exe__branch_taken_out) != '0), exe__branch_target_out, ((exe_mem__mem_write_out) != '0), ((exe_mem__mem_read_out) != '0), exe_mem__mem_write_addr_out, exe_mem__mem_write_data_out, exe_mem__mem_write_mask_out, ((state_reg[64'h1].valid) != '0), state_reg[64'h1].wb_op, ((wb__regs_write_out) != '0), wb__regs_data_out, wb__regs_wr_id_out);
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (debugen_in && !reset) begin
            debug();
        end
        l2_ptw_owner_reg_tmp = l2_ptw_owner_reg;
        if (l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_NONE))) begin
            if (!(dcache__mem_out__read_out || dcache__mem_out__write_out)) begin
                if (dmmu__mem_read_out) begin
                    l2_ptw_owner_reg_tmp = L2_PTW_OWNER_DMMU;
                end
                else begin
                    if (immu__mem_read_out) begin
                        l2_ptw_owner_reg_tmp = L2_PTW_OWNER_IMMU;
                    end
                end
            end
        end
        else begin
            if ((((l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_DMMU))) && !dmmu__mem_read_out)) || (((l2_ptw_owner_reg == unsigned'(64'(L2_PTW_OWNER_IMMU))) && !immu__mem_read_out))) begin
                l2_ptw_owner_reg_tmp = L2_PTW_OWNER_NONE;
            end
        end
        fetch_buffer_valid_reg_tmp = fetch_buffer_valid_reg;
        fetch_instr_reg_tmp = fetch_instr_reg;
        fetch_pc_reg_tmp = fetch_pc_reg;
        decode_state_reg_tmp = decode_state_reg;
        decode_fallthrough_reg_tmp = decode_fallthrough_reg;
        icache_flush_reg_tmp = branch_mispredict_comb;
        load_result_pending_reg_tmp = load_result_pending_reg;
        load_retire_ready_reg_tmp = load_retire_ready_reg;
        atomic_write_accepted_reg_tmp = atomic_write_accepted_reg;
        if (!memory_wait_comb) begin
            load_result_pending_reg_tmp = 0;
            load_retire_ready_reg_tmp = 0;
            atomic_write_accepted_reg_tmp = 0;
        end
        else begin
            if (((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))) begin
                if (wb_mem__load_ready_out) begin
                    load_result_pending_reg_tmp = 1;
                end
                if (((((load_result_pending_reg) != '0) || wb_mem__load_ready_out)) && !memory_stage_transport_wait_comb) begin
                    load_retire_ready_reg_tmp = 1;
                end
            end
        end
        if ((((((state_reg[64'h1].valid) != '0) && (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM)))) && dcache__write_in) && !d_mem_out__wait_in) && memory_wait_comb) begin
            atomic_write_accepted_reg_tmp = 1;
        end
        csr_commit_state_reg_tmp = csr_commit_state_reg;
        csr_commit_fire_reg_tmp = 0;
        csr_commit_serialized_reg_tmp = csr_commit_serialized_reg;
        csr_commit_interrupt_valid_reg_tmp = csr_commit_interrupt_valid_reg;
        csr_commit_interrupt_cause_reg_tmp = csr_commit_interrupt_cause_reg;
        csr_commit_interrupt_to_supervisor_reg_tmp = csr_commit_interrupt_to_supervisor_reg;
        if (xret_redirect_comb || sync_trap_redirect_comb) begin
            csr_commit_state_reg_tmp = csr_state_comb;
            csr_commit_fire_reg_tmp = ((csr_state_comb.valid) != '0);
            csr_commit_serialized_reg_tmp = 0;
            csr_commit_interrupt_valid_reg_tmp = interrupt_accept_comb;
            csr_commit_interrupt_cause_reg_tmp = interrupt_cause_reg;
            csr_commit_interrupt_to_supervisor_reg_tmp = interrupt_to_supervisor_reg;
        end
        else begin
            if (csr_commit_wait_comb) begin
                csr_commit_serialized_reg_tmp = 1;
            end
            else begin
                if (csr_commit_serializing_comb && ((csr_commit_serialized_reg) != '0)) begin
                    csr_commit_state_reg_tmp.valid=0;
                    csr_commit_serialized_reg_tmp = 0;
                    csr_commit_interrupt_valid_reg_tmp = 0;
                end
                else begin
                    if (!memory_wait_comb || !((csr_commit_state_reg.valid) != '0)) begin
                        csr_commit_state_reg_tmp = csr_state_comb;
                        csr_commit_fire_reg_tmp = ((csr_state_comb.valid) != '0);
                        csr_commit_serialized_reg_tmp = 0;
                        csr_commit_interrupt_valid_reg_tmp = interrupt_accept_comb;
                        csr_commit_interrupt_cause_reg_tmp = interrupt_cause_reg;
                        csr_commit_interrupt_to_supervisor_reg_tmp = interrupt_to_supervisor_reg;
                    end
                end
            end
        end
        if (!((valid) != '0) || ((((fetch_buffer_valid_reg) != '0) && (unsigned'(32'(fetch_pc_reg)) != unsigned'(32'(pc)))))) begin
            fetch_buffer_valid_reg_tmp = 0;
        end
        if (icache_response_match_comb && ((!((fetch_buffer_valid_reg) != '0) || (unsigned'(32'(fetch_pc_reg)) != unsigned'(32'(pc)))))) begin
            fetch_buffer_valid_reg_tmp = 1;
            fetch_instr_reg_tmp = icache__read_data_out;
            fetch_pc_reg_tmp = pc;
        end
        sbi_ret_a1_valid_reg_tmp = 0;
        sbi_ret_a1_reg_tmp = sbi_ret_a1_reg;
        sbi_decode_reg_tmp = sbi_decode_reg;
        if (!sbi_legacy_ecall_comb) begin
            sbi_decode_reg_tmp.args_valid = 0;
            sbi_decode_reg_tmp.valid = 0;
        end
        else begin
            if ((!((sbi_decode_reg.args_valid) != '0) && !sbi_arg_hazard_comb) && !interrupt_retire_wait_comb) begin
                sbi_decode_reg_tmp.args_valid = 1;
                sbi_decode_reg_tmp.a0 = sbi_arg_value(8'hA);
                sbi_decode_reg_tmp.a1 = sbi_arg_value(8'hB);
                sbi_decode_reg_tmp.a6 = sbi_arg_value(8'h10);
                sbi_decode_reg_tmp.a7 = sbi_arg_value(8'h11);
            end
            else begin
                if (!((sbi_decode_reg.valid) != '0)) begin
                    sbi_decode_reg_tmp.valid = 1;
                    sbi_decode_reg_tmp.handled = sbi_registered_handled_comb;
                    sbi_decode_reg_tmp.set_timer = sbi_registered_set_timer_comb;
                    sbi_decode_reg_tmp.base = sbi_registered_base_comb;
                    sbi_decode_reg_tmp.noop = sbi_registered_noop_comb;
                    sbi_decode_reg_tmp.writes_a1 = sbi_registered_writes_a1_comb;
                    sbi_decode_reg_tmp.ret_a1 = sbi_registered_ret_value_comb;
                    sbi_decode_reg_tmp.send_ipi = 0;
                    sbi_decode_reg_tmp.remote_fence_i = 0;
                    sbi_decode_reg_tmp.remote_sfence_vma = 0;
                end
            end
        end
        if (!((valid) != '0)) begin
            immu_result_valid_reg_tmp = 0;
        end
        else begin
            if (!immu_result_match_comb && !immu__busy_out) begin
                immu_result_valid_reg_tmp = 1;
                immu_vaddr_reg_tmp = pc;
                immu_paddr_reg_tmp = immu__paddr_out;
                immu_fault_reg_tmp = immu__fault_out;
            end
        end
        if (!(((state_reg[64'h1].valid) != '0) && ((((exe_mem__mem_read_out || exe_mem__mem_write_out) || (signed'(32'(state_reg[64'h1].mem_op)) == signed'(32'(Mem_pkg::STORE)))) || (signed'(32'(state_reg[64'h1].wb_op)) == signed'(32'(Wb_pkg::MEM))))))) begin
            dmmu_result_valid_reg_tmp = 0;
        end
        else begin
            if ((!dmmu_result_match_comb && !dmmu__busy_out) && dmmu_access_ready_comb) begin
                dmmu_result_valid_reg_tmp = 1;
                dmmu_vaddr_reg_tmp = dmmu_vaddr_comb;
                dmmu_paddr_reg_tmp = dmmu__paddr_out;
                dmmu_fault_reg_tmp = dmmu__fault_out;
            end
        end
        if (sfence_vma_comb) begin
            immu_result_valid_reg_tmp = 0;
            dmmu_result_valid_reg_tmp = 0;
        end
        interrupt_accept_reg_tmp = interrupt_accept_reg;
        interrupt_cause_reg_tmp = interrupt_cause_reg;
        interrupt_to_supervisor_reg_tmp = interrupt_to_supervisor_reg;
        if (interrupt_accept_comb) begin
            interrupt_accept_reg_tmp = 0;
        end
        else begin
            if (interrupt_capture_comb) begin
                interrupt_accept_reg_tmp = 1;
                interrupt_cause_reg_tmp = irq__interrupt_cause_out;
                interrupt_to_supervisor_reg_tmp = irq__interrupt_to_supervisor_out;
            end
        end
        if (!((state_reg[64'h0].valid) != '0) || (signed'(32'(state_reg[64'h0].sys_op)) != signed'(32'(Sys_pkg::ECALL)))) begin
            sbi_arg_wait_reg_tmp = 0;
        end
        else begin
            if (sbi_arg_hazard_comb) begin
                sbi_arg_wait_reg_tmp = 1;
            end
            else begin
                if (!memory_wait_comb) begin
                    sbi_arg_wait_reg_tmp = 0;
                end
            end
        end
        if (((dmem_addr_out == 32'h11223344) && dmem_write_out) && !((output_write_active_reg) != '0)) begin
            logic signed[31:0] out; out = $fopen("out.txt", "a");
            if (debugen_in) begin
                $write("OUTPUT pc=%x data=%08x char=%02x\n", pc, dmem_write_data_out, dmem_write_data_out & 32'hFF);
            end
            $fwrite(out, "%c", dmem_write_data_out & 32'hFF);
            $fclose(out);
        end
        output_write_active_reg_tmp = (dmem_addr_out == 32'h11223344) && dmem_write_out;
        if (!reset && xret_redirect_comb) begin
            logic[31:0] epc; epc = (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::SRET))) ? (unsigned'(32'(csr__sepc_out))) : (unsigned'(32'(csr__mepc_out)));
            pc_tmp = epc;
            valid_tmp = 0;
            state_reg_tmp[64'h0] = 0;
            state_reg_tmp[64'h0].valid=0;
            state_reg_tmp[64'h1] = 0;
            state_reg_tmp[64'h1].valid=0;
            decode_state_reg_tmp.valid=0;
            predicted_next_reg_tmp = '0;
            fallthrough_reg_tmp = '0;
            predicted_taken_reg_tmp = '0;
            alu_result_reg_tmp = alu_result_reg;
            debug_branch_target_reg_tmp = epc;
            debug_branch_taken_reg_tmp = 1;
            interrupt_entry_guard_reg_tmp = 0;
        end
        else begin
            if (!reset && sync_trap_redirect_comb) begin
                pc_tmp = csr__trap_vector_out;
                valid_tmp = 0;
                state_reg_tmp[64'h0] = 0;
                state_reg_tmp[64'h0].valid=0;
                state_reg_tmp[64'h1] = 0;
                state_reg_tmp[64'h1].valid=0;
                decode_state_reg_tmp.valid=0;
                predicted_next_reg_tmp = '0;
                fallthrough_reg_tmp = '0;
                predicted_taken_reg_tmp = '0;
                alu_result_reg_tmp = alu_result_reg;
                debug_branch_target_reg_tmp = csr__trap_vector_out;
                debug_branch_taken_reg_tmp = 1;
                interrupt_entry_guard_reg_tmp = interrupt_accept_comb;
            end
            else begin
                if ((((!reset && immu_active_fault_comb) && ((((state_reg[64'h0].valid) != '0) || ((state_reg[64'h1].valid) != '0)))) && !dmmu_active_fault_comb) && !memory_wait_comb) begin
                    pc_tmp = pc;
                    valid_tmp = 1;
                    state_reg_tmp[64'h0] = 0;
                    state_reg_tmp[64'h0].valid=0;
                    state_reg_tmp[64'h1] = state_reg[64'h0];
                    decode_state_reg_tmp.valid=0;
                    predicted_next_reg_tmp = '0;
                    fallthrough_reg_tmp = '0;
                    predicted_taken_reg_tmp = '0;
                    alu_result_reg_tmp = unsigned'(32'((signed'(32'(state_reg[64'h0].csr_op)) != signed'(32'(Csr_pkg::CNONE))) ? (csr__read_data_out) : (((signed'(32'(state_reg[64'h0].amo_op)) == signed'(32'(Amo_pkg::SC_W))) ? (exe_mem__atomic_sc_result_out) : (exe__alu_result_out)))));
                    debug_branch_target_reg_tmp = exe__branch_target_out;
                    debug_branch_taken_reg_tmp = exe__branch_taken_out;
                    interrupt_entry_guard_reg_tmp = 0;
                end
                else begin
                    if (!reset && ((immu_active_fault_comb || dmmu_active_fault_comb))) begin
                        pc_tmp = unsigned'(32'((dmmu_active_fault_comb) ? (data_page_fault_vector_comb) : (inst_page_fault_vector_comb)));
                        valid_tmp = 0;
                        state_reg_tmp[64'h0] = 0;
                        state_reg_tmp[64'h0].valid=0;
                        state_reg_tmp[64'h1] = 0;
                        state_reg_tmp[64'h1].valid=0;
                        decode_state_reg_tmp.valid=0;
                        alu_result_reg_tmp = alu_result_reg;
                        predicted_next_reg_tmp = '0;
                        fallthrough_reg_tmp = '0;
                        predicted_taken_reg_tmp = '0;
                        debug_branch_target_reg_tmp = unsigned'(32'((dmmu_active_fault_comb) ? (data_page_fault_vector_comb) : (inst_page_fault_vector_comb)));
                        debug_branch_taken_reg_tmp = 1;
                        interrupt_entry_guard_reg_tmp = 0;
                    end
                    else begin
                        if (sbi_decode_handled_comb && !memory_wait_comb) begin
                            interrupt_entry_guard_reg_tmp = 0;
                            pc_tmp = pc;
                            valid_tmp = 0;
                            state_reg_tmp[64'h0] = 0;
                            state_reg_tmp[64'h0].valid=0;
                            state_reg_tmp[64'h1] = state_reg[64'h0];
                            decode_state_reg_tmp.valid=0;
                            state_reg_tmp[64'h1].sys_op=Sys_pkg::SNONE;
                            state_reg_tmp[64'h1].trap_op=Trap_pkg::TNONE;
                            state_reg_tmp[64'h1].csr_op=Csr_pkg::CNONE;
                            state_reg_tmp[64'h1].mem_op=Mem_pkg::MNONE;
                            state_reg_tmp[64'h1].br_op=Br_pkg::BNONE;
                            state_reg_tmp[64'h1].alu_op=Alu_pkg::ADD;
                            state_reg_tmp[64'h1].rs1_val=32'h0;
                            state_reg_tmp[64'h1].rs2_val=32'h0;
                            state_reg_tmp[64'h1].imm=32'h0;
                            state_reg_tmp[64'h1].rd=8'hA;
                            state_reg_tmp[64'h1].wb_op=Wb_pkg::ALU;
                            predicted_next_reg_tmp = '0;
                            fallthrough_reg_tmp = '0;
                            predicted_taken_reg_tmp = '0;
                            alu_result_reg_tmp = 32'h0;
                            sbi_ret_a1_valid_reg_tmp = sbi_decode_reg.writes_a1;
                            sbi_ret_a1_reg_tmp = sbi_decode_reg.ret_a1;
                            sbi_decode_reg_tmp.args_valid = 0;
                            sbi_decode_reg_tmp.valid = 0;
                            debug_branch_target_reg_tmp = pc;
                            debug_branch_taken_reg_tmp = 0;
                        end
                        else begin
                            if (memory_wait_comb) begin
                                pc_tmp = pc;
                                interrupt_entry_guard_reg_tmp = 0;
                                valid_tmp = valid;
                                state_reg_tmp = state_reg;
                                if (fence_i_registered_store_pending_comb && !interrupt_retire_wait_comb) begin
                                    state_reg_tmp[64'h1].valid=0;
                                end
                                predicted_next_reg_tmp = predicted_next_reg;
                                fallthrough_reg_tmp = fallthrough_reg;
                                predicted_taken_reg_tmp = predicted_taken_reg;
                                alu_result_reg_tmp = alu_result_reg;
                                if ((wb_mem__load_ready_out && (signed'(32'(state_reg[64'h1].rd)) != 32'sh0)) && ((state_reg[64'h0].valid) != '0)) begin
                                    if (signed'(32'(state_reg[64'h0].rs1)) == signed'(32'(state_reg[64'h1].rd))) begin
                                        state_reg_tmp[64'h0].rs1_val=wb_mem__load_result_out;
                                    end
                                    if (signed'(32'(state_reg[64'h0].rs2)) == signed'(32'(state_reg[64'h1].rd))) begin
                                        state_reg_tmp[64'h0].rs2_val=wb_mem__load_result_out;
                                    end
                                end
                                debug_branch_target_reg_tmp = debug_branch_target_reg;
                                debug_branch_taken_reg_tmp = debug_branch_taken_reg;
                            end
                            else begin
                                interrupt_entry_guard_reg_tmp = 0;
                                pc_tmp = pc;
                                if (fetch_valid_comb && !stall_comb) begin
                                    pc_tmp = decode_fallthrough_comb;
                                end
                                if (branch_mispredict_comb) begin
                                    pc_tmp = exe__branch_target_out;
                                end
                                valid_tmp = !(decode_indirect_branch_valid_comb || ((((decode_state_reg.valid) != '0) && (((signed'(32'(decode_state_reg.br_op)) == signed'(32'(Br_pkg::JALR))) || (signed'(32'(decode_state_reg.br_op)) == signed'(32'(Br_pkg::JR))))))));
                                if (branch_mispredict_comb) begin
                                    state_reg_tmp[64'h0] = 0;
                                    state_reg_tmp[64'h0].valid=0;
                                    decode_state_reg_tmp.valid=0;
                                    predicted_next_reg_tmp[64'h0] = pc;
                                    fallthrough_reg_tmp[64'h0] = pc;
                                    predicted_taken_reg_tmp[64'h0] = 0;
                                end
                                else begin
                                    if (hazard_stall_comb) begin
                                        state_reg_tmp[64'h0] = 0;
                                        state_reg_tmp[64'h0].valid=0;
                                        predicted_next_reg_tmp[64'h0] = pc;
                                        fallthrough_reg_tmp[64'h0] = pc;
                                        predicted_taken_reg_tmp[64'h0] = 0;
                                    end
                                    else begin
                                        if (((decode_state_reg.valid) != '0)) begin
                                            state_reg_tmp[64'h0] = decode_state_reg;
                                            if (signed'(32'(decode_state_reg.rs1)) != 32'sh0) begin
                                                state_reg_tmp[64'h0].rs1_val=regs__read_data0_out;
                                            end
                                            if (signed'(32'(decode_state_reg.rs2)) != 32'sh0) begin
                                                state_reg_tmp[64'h0].rs2_val=regs__read_data1_out;
                                            end
                                            state_reg_tmp[64'h0].valid=1;
                                            predicted_next_reg_tmp[64'h0] = decode_fallthrough_reg;
                                            fallthrough_reg_tmp[64'h0] = decode_fallthrough_reg;
                                            predicted_taken_reg_tmp[64'h0] = 0;
                                            forward();
                                        end
                                        else begin
                                            state_reg_tmp[64'h0] = 0;
                                            state_reg_tmp[64'h0].valid=0;
                                            predicted_next_reg_tmp[64'h0] = pc;
                                            fallthrough_reg_tmp[64'h0] = pc;
                                            predicted_taken_reg_tmp[64'h0] = 0;
                                        end
                                        if (fetch_valid_comb) begin
                                            decode_state_reg_tmp = dec__state_out;
                                            decode_state_reg_tmp.csr_illegal=csr__legality_out;
                                            decode_state_reg_tmp.valid=dec__instr_valid_in;
                                            decode_fallthrough_reg_tmp = decode_fallthrough_comb;
                                        end
                                        else begin
                                            decode_state_reg_tmp.valid=0;
                                        end
                                    end
                                end
                                state_reg_tmp[64'h1] = state_reg[64'h0];
                                predicted_next_reg_tmp[64'h1] = predicted_next_reg[64'h0];
                                fallthrough_reg_tmp[64'h1] = fallthrough_reg[64'h0];
                                predicted_taken_reg_tmp[64'h1] = predicted_taken_reg[64'h0];
                                alu_result_reg_tmp = unsigned'(32'((signed'(32'(state_reg[64'h0].csr_op)) != signed'(32'(Csr_pkg::CNONE))) ? (csr__read_data_out) : (((signed'(32'(state_reg[64'h0].amo_op)) == signed'(32'(Amo_pkg::SC_W))) ? (exe_mem__atomic_sc_result_out) : (exe__alu_result_out)))));
                                debug_branch_target_reg_tmp = exe__branch_target_out;
                                debug_branch_taken_reg_tmp = exe__branch_taken_out;
                            end
                        end
                    end
                end
            end
        end
        if (icache_invalidate_comb) begin
            if (((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::FENCEI)))) begin
                icache_invalidate_issued_reg_tmp = 1;
                pc_tmp = 32'(state_reg[64'h0].pc + 'h4);
            end
            valid_tmp = 0;
            fetch_buffer_valid_reg_tmp = 0;
            decode_state_reg_tmp.valid=0;
        end
        else begin
            if (!((state_reg[64'h0].valid) != '0) || (signed'(32'(state_reg[64'h0].sys_op)) != signed'(32'(Sys_pkg::FENCEI)))) begin
                icache_invalidate_issued_reg_tmp = 0;
            end
        end
        if (((((state_reg[64'h0].valid) != '0) && (signed'(32'(state_reg[64'h0].sys_op)) == signed'(32'(Sys_pkg::SFENCE_VMA)))) && !interrupt_retire_wait_comb) && !((sfence_vma_issued_reg) != '0)) begin
            sfence_vma_issued_reg_tmp = 1;
        end
        else begin
            if (!((state_reg[64'h0].valid) != '0) || (signed'(32'(state_reg[64'h0].sys_op)) != signed'(32'(Sys_pkg::SFENCE_VMA)))) begin
                sfence_vma_issued_reg_tmp = 0;
            end
        end
        if (reset) begin
            state_reg_tmp[64'h0].valid=8'h0;
            state_reg_tmp[64'h1].valid=8'h0;
            pc_tmp = reset_pc_in;
            valid_tmp = '0;
            fetch_buffer_valid_reg_tmp = '0;
            fetch_instr_reg_tmp = '0;
            fetch_pc_reg_tmp = '0;
            icache_flush_reg_tmp = '0;
            decode_state_reg_tmp = '0;
            decode_fallthrough_reg_tmp = '0;
            load_result_pending_reg_tmp = '0;
            load_retire_ready_reg_tmp = '0;
            atomic_write_accepted_reg_tmp = '0;
            csr_commit_state_reg_tmp = '0;
            csr_commit_fire_reg_tmp = '0;
            csr_commit_serialized_reg_tmp = '0;
            csr_commit_interrupt_valid_reg_tmp = '0;
            csr_commit_interrupt_cause_reg_tmp = '0;
            csr_commit_interrupt_to_supervisor_reg_tmp = '0;
            predicted_next_reg_tmp = '0;
            fallthrough_reg_tmp = '0;
            predicted_taken_reg_tmp = '0;
            output_write_active_reg_tmp = '0;
            interrupt_entry_guard_reg_tmp = '0;
            interrupt_accept_reg_tmp = '0;
            interrupt_cause_reg_tmp = '0;
            interrupt_to_supervisor_reg_tmp = '0;
            sbi_ret_a1_valid_reg_tmp = '0;
            sbi_ret_a1_reg_tmp = '0;
            icache_invalidate_issued_reg_tmp = '0;
            sfence_vma_issued_reg_tmp = '0;
            sbi_arg_wait_reg_tmp = '0;
            sbi_decode_reg_tmp = '0;
            immu_result_valid_reg_tmp = '0;
            immu_vaddr_reg_tmp = '0;
            immu_paddr_reg_tmp = '0;
            immu_fault_reg_tmp = '0;
            dmmu_result_valid_reg_tmp = '0;
            dmmu_vaddr_reg_tmp = '0;
            dmmu_paddr_reg_tmp = '0;
            dmmu_fault_reg_tmp = '0;
            l2_ptw_owner_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg (input logic reset);
    begin: _work_neg
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
        icache_invalidate_issued_reg_tmp = icache_invalidate_issued_reg;
        sfence_vma_issued_reg_tmp = sfence_vma_issued_reg;
        sbi_arg_wait_reg_tmp = sbi_arg_wait_reg;
        sbi_decode_reg_tmp = sbi_decode_reg;
        pc_tmp = pc;
        valid_tmp = valid;
        fetch_buffer_valid_reg_tmp = fetch_buffer_valid_reg;
        fetch_instr_reg_tmp = fetch_instr_reg;
        fetch_pc_reg_tmp = fetch_pc_reg;
        icache_flush_reg_tmp = icache_flush_reg;
        decode_state_reg_tmp = decode_state_reg;
        decode_fallthrough_reg_tmp = decode_fallthrough_reg;
        load_result_pending_reg_tmp = load_result_pending_reg;
        load_retire_ready_reg_tmp = load_retire_ready_reg;
        atomic_write_accepted_reg_tmp = atomic_write_accepted_reg;
        csr_commit_state_reg_tmp = csr_commit_state_reg;
        csr_commit_fire_reg_tmp = csr_commit_fire_reg;
        csr_commit_serialized_reg_tmp = csr_commit_serialized_reg;
        csr_commit_interrupt_valid_reg_tmp = csr_commit_interrupt_valid_reg;
        csr_commit_interrupt_cause_reg_tmp = csr_commit_interrupt_cause_reg;
        csr_commit_interrupt_to_supervisor_reg_tmp = csr_commit_interrupt_to_supervisor_reg;
        alu_result_reg_tmp = alu_result_reg;
        state_reg_tmp = state_reg;
        predicted_next_reg_tmp = predicted_next_reg;
        fallthrough_reg_tmp = fallthrough_reg;
        predicted_taken_reg_tmp = predicted_taken_reg;
        debug_alu_a_reg_tmp = debug_alu_a_reg;
        debug_alu_b_reg_tmp = debug_alu_b_reg;
        debug_branch_target_reg_tmp = debug_branch_target_reg;
        debug_branch_taken_reg_tmp = debug_branch_taken_reg;
        output_write_active_reg_tmp = output_write_active_reg;
        interrupt_entry_guard_reg_tmp = interrupt_entry_guard_reg;
        interrupt_accept_reg_tmp = interrupt_accept_reg;
        interrupt_cause_reg_tmp = interrupt_cause_reg;
        interrupt_to_supervisor_reg_tmp = interrupt_to_supervisor_reg;
        sbi_ret_a1_valid_reg_tmp = sbi_ret_a1_valid_reg;
        sbi_ret_a1_reg_tmp = sbi_ret_a1_reg;
        immu_result_valid_reg_tmp = immu_result_valid_reg;
        immu_vaddr_reg_tmp = immu_vaddr_reg;
        immu_paddr_reg_tmp = immu_paddr_reg;
        immu_fault_reg_tmp = immu_fault_reg;
        dmmu_result_valid_reg_tmp = dmmu_result_valid_reg;
        dmmu_vaddr_reg_tmp = dmmu_vaddr_reg;
        dmmu_paddr_reg_tmp = dmmu_paddr_reg;
        dmmu_fault_reg_tmp = dmmu_fault_reg;
        l2_ptw_owner_reg_tmp = l2_ptw_owner_reg;

            _work(reset);

        icache_invalidate_issued_reg <= icache_invalidate_issued_reg_tmp;
        sfence_vma_issued_reg <= sfence_vma_issued_reg_tmp;
        sbi_arg_wait_reg <= sbi_arg_wait_reg_tmp;
        sbi_decode_reg <= sbi_decode_reg_tmp;
        pc <= pc_tmp;
        valid <= valid_tmp;
        fetch_buffer_valid_reg <= fetch_buffer_valid_reg_tmp;
        fetch_instr_reg <= fetch_instr_reg_tmp;
        fetch_pc_reg <= fetch_pc_reg_tmp;
        icache_flush_reg <= icache_flush_reg_tmp;
        decode_state_reg <= decode_state_reg_tmp;
        decode_fallthrough_reg <= decode_fallthrough_reg_tmp;
        load_result_pending_reg <= load_result_pending_reg_tmp;
        load_retire_ready_reg <= load_retire_ready_reg_tmp;
        atomic_write_accepted_reg <= atomic_write_accepted_reg_tmp;
        csr_commit_state_reg <= csr_commit_state_reg_tmp;
        csr_commit_fire_reg <= csr_commit_fire_reg_tmp;
        csr_commit_serialized_reg <= csr_commit_serialized_reg_tmp;
        csr_commit_interrupt_valid_reg <= csr_commit_interrupt_valid_reg_tmp;
        csr_commit_interrupt_cause_reg <= csr_commit_interrupt_cause_reg_tmp;
        csr_commit_interrupt_to_supervisor_reg <= csr_commit_interrupt_to_supervisor_reg_tmp;
        alu_result_reg <= alu_result_reg_tmp;
        state_reg <= state_reg_tmp;
        predicted_next_reg <= predicted_next_reg_tmp;
        fallthrough_reg <= fallthrough_reg_tmp;
        predicted_taken_reg <= predicted_taken_reg_tmp;
        debug_alu_a_reg <= debug_alu_a_reg_tmp;
        debug_alu_b_reg <= debug_alu_b_reg_tmp;
        debug_branch_target_reg <= debug_branch_target_reg_tmp;
        debug_branch_taken_reg <= debug_branch_taken_reg_tmp;
        output_write_active_reg <= output_write_active_reg_tmp;
        interrupt_entry_guard_reg <= interrupt_entry_guard_reg_tmp;
        interrupt_accept_reg <= interrupt_accept_reg_tmp;
        interrupt_cause_reg <= interrupt_cause_reg_tmp;
        interrupt_to_supervisor_reg <= interrupt_to_supervisor_reg_tmp;
        sbi_ret_a1_valid_reg <= sbi_ret_a1_valid_reg_tmp;
        sbi_ret_a1_reg <= sbi_ret_a1_reg_tmp;
        immu_result_valid_reg <= immu_result_valid_reg_tmp;
        immu_vaddr_reg <= immu_vaddr_reg_tmp;
        immu_paddr_reg <= immu_paddr_reg_tmp;
        immu_fault_reg <= immu_fault_reg_tmp;
        dmmu_result_valid_reg <= dmmu_result_valid_reg_tmp;
        dmmu_vaddr_reg <= dmmu_vaddr_reg_tmp;
        dmmu_paddr_reg <= dmmu_paddr_reg_tmp;
        dmmu_fault_reg <= dmmu_fault_reg_tmp;
        l2_ptw_owner_reg <= l2_ptw_owner_reg_tmp;
    end

    always_ff @(negedge clk) begin

            _work_neg(reset);

    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign external_cache_invalidate_ready_out = (((!memory_wait_comb && !dcache__mem_out__read_out) && !dcache__mem_out__write_out) != '0);

    assign debug_core_out = debug_core_comb;

    assign debug_mmu_out = debug_mmu_comb;

    assign debug_cache_out = debug_cache_comb;

    assign debug_wb_out = debug_wb_comb;

    assign debug_csr_out = debug_csr_comb;

    assign debug_irq_out = debug_irq_comb;

    assign debug_regs_out = debug_regs_comb;

    assign debug_branch_out = debug_branch_comb;

    assign debug_decode_out = debug_decode_comb;

    assign sbi_set_timer_out = sbi_set_timer_output_comb;

    assign sbi_timer_lo_out = sbi_timer_lo_output_comb;

    assign sbi_timer_hi_out = sbi_timer_hi_output_comb;

    assign debug_sbi_out = debug_sbi_comb;

    assign perf_out = perf_comb;


endmodule
