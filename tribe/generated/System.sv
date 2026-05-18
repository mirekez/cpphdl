`default_nettype none

import Predef_pkg::*;
import Zicsr_pkg::*;
import Rv32ia_pkg::*;
import Rv32im_pkg::*;
import Rv32ic_pkg::*;
import Rv32i_pkg::*;
import Mem_pkg::*;
import Alu_pkg::*;
import Wb_pkg::*;
import Br_pkg::*;
import Sys_pkg::*;
import Trap_pkg::*;
import Amo_pkg::*;
import Csr_pkg::*;
import State_pkg::*;
import L1CachePerf_pkg::*;
import TribePerf_pkg::*;


module System (
    input wire clk
,   input wire reset
,   input wire debugen_in
,   input wire[31:0] reset_pc_in
,   input wire[31:0] boot_hartid_in
,   input wire[31:0] boot_dtb_addr_in
,   input wire[2-1:0] boot_priv_in
,   input wire[31:0] memory_base_in
,   input wire[31:0] memory_size_in
,   input wire[31:0] mem_region_size_in[4]
,   input wire uart_rx_valid_in
,   input wire[7:0] uart_rx_data_in
,   output wire uart_rx_ready_out
,   output wire uart_tx_valid_out
,   output wire[7:0] uart_tx_data_out
,   output TribePerf perf_out
,   output wire dmem_write_out
,   output wire[31:0] dmem_write_data_out
,   output wire[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output wire[31:0] dmem_addr_out
,   output wire[31:0] imem_read_addr_out
,   output wire debug_immu_ptw_read_out
,   output wire[31:0] debug_immu_ptw_addr_out
,   output wire debug_immu_busy_out
,   output wire debug_immu_fault_out
,   output wire[31:0] debug_immu_paddr_out
,   output wire[31:0] debug_immu_last_addr_out
,   output wire[31:0] debug_immu_last_pte_out
,   output wire debug_icache_read_valid_out
,   output wire[31:0] debug_icache_read_addr_out
,   output wire debug_fetch_valid_out
,   output wire debug_memory_wait_out
,   output wire debug_wb_load_ready_out
,   output wire debug_wb_mem_wait_out
,   output wire debug_icache_read_in_out
,   output wire debug_icache_stall_in_out
,   output wire debug_dmmu_ptw_read_out
,   output wire[31:0] debug_dmmu_ptw_addr_out
,   output wire debug_dmmu_busy_out
,   output wire debug_dmmu_fault_out
,   output wire[31:0] debug_mmu_ptw_word_out
,   output wire[31:0] debug_pc_out
,   output wire[31:0] debug_satp_out
,   output wire[31:0] debug_mstatus_out
,   output wire[31:0] debug_mtvec_out
,   output wire[31:0] debug_mepc_out
,   output wire[31:0] debug_mcause_out
,   output wire[31:0] debug_mtval_out
,   output wire[31:0] debug_sepc_out
,   output wire[31:0] debug_stvec_out
,   output wire[31:0] debug_scause_out
,   output wire[31:0] debug_stval_out
,   output wire[2-1:0] debug_priv_out
,   output wire[31:0] debug_ra_out
,   output wire debug_regs_write_out
,   output wire debug_regs_write_actual_out
,   output wire[7:0] debug_regs_wr_id_out
,   output wire[31:0] debug_regs_data_out
,   output wire debug_branch_taken_now_out
,   output wire[31:0] debug_branch_target_now_out
,   output wire[31:0] debug_decode_instr_out
,   output wire[31:0] debug_decode_pc_out
,   output wire[7:0] debug_decode_br_out
,   output wire[31:0] debug_decode_imm_out
,   output wire axi_out__awvalid_out[2]
,   input wire axi_out__awready_in[2]
,   output wire[19-1:0] axi_out__awaddr_out[2]
,   output wire[4-1:0] axi_out__awid_out[2]
,   output wire axi_out__wvalid_out[2]
,   input wire axi_out__wready_in[2]
,   output wire[128-1:0] axi_out__wdata_out[2]
,   output wire axi_out__wlast_out[2]
,   input wire axi_out__bvalid_in[2]
,   output wire axi_out__bready_out[2]
,   input wire[4-1:0] axi_out__bid_in[2]
,   output wire axi_out__arvalid_out[2]
,   input wire axi_out__arready_in[2]
,   output wire[19-1:0] axi_out__araddr_out[2]
,   output wire[4-1:0] axi_out__arid_out[2]
,   input wire axi_out__rvalid_in[2]
,   output wire axi_out__rready_out[2]
,   input wire[128-1:0] axi_out__rdata_in[2]
,   input wire axi_out__rlast_in[2]
,   input wire[4-1:0] axi_out__rid_in[2]
);


    // regs and combs

    // members
      wire tribe__dmem_write_out;
      wire[31:0] tribe__dmem_write_data_out;
      wire[7:0] tribe__dmem_write_mask_out;
      wire tribe__dmem_read_out;
      wire[31:0] tribe__dmem_addr_out;
      wire[31:0] tribe__imem_read_addr_out;
      wire tribe__debug_immu_ptw_read_out;
      wire[31:0] tribe__debug_immu_ptw_addr_out;
      wire tribe__debug_immu_busy_out;
      wire tribe__debug_immu_fault_out;
      wire[31:0] tribe__debug_immu_paddr_out;
      wire[31:0] tribe__debug_immu_last_addr_out;
      wire[31:0] tribe__debug_immu_last_pte_out;
      wire tribe__debug_icache_read_valid_out;
      wire[31:0] tribe__debug_icache_read_addr_out;
      wire tribe__debug_fetch_valid_out;
      wire tribe__debug_memory_wait_out;
      wire tribe__debug_wb_load_ready_out;
      wire tribe__debug_wb_mem_wait_out;
      wire tribe__debug_icache_read_in_out;
      wire tribe__debug_icache_stall_in_out;
      wire tribe__debug_dmmu_ptw_read_out;
      wire[31:0] tribe__debug_dmmu_ptw_addr_out;
      wire tribe__debug_dmmu_busy_out;
      wire tribe__debug_dmmu_fault_out;
      wire[31:0] tribe__debug_mmu_ptw_word_out;
      wire[31:0] tribe__debug_pc_out;
      wire[31:0] tribe__debug_satp_out;
      wire[31:0] tribe__debug_mstatus_out;
      wire[31:0] tribe__debug_mtvec_out;
      wire[31:0] tribe__debug_mepc_out;
      wire[31:0] tribe__debug_mcause_out;
      wire[31:0] tribe__debug_mtval_out;
      wire[31:0] tribe__debug_sepc_out;
      wire[31:0] tribe__debug_stvec_out;
      wire[31:0] tribe__debug_scause_out;
      wire[31:0] tribe__debug_stval_out;
      wire tribe__debug_irq_valid_out;
      wire[31:0] tribe__debug_irq_cause_out;
      wire tribe__debug_irq_to_supervisor_out;
      wire[31:0] tribe__debug_irq_mip_out;
      wire[31:0] tribe__debug_irq_mie_out;
      wire[31:0] tribe__debug_irq_mideleg_out;
      wire[2-1:0] tribe__debug_priv_out;
      wire[31:0] tribe__debug_ra_out;
      wire tribe__debug_regs_write_out;
      wire tribe__debug_regs_write_actual_out;
      wire[7:0] tribe__debug_regs_wr_id_out;
      wire[31:0] tribe__debug_regs_data_out;
      wire tribe__debug_branch_taken_now_out;
      wire[31:0] tribe__debug_branch_target_now_out;
      wire[31:0] tribe__debug_decode_instr_out;
      wire[31:0] tribe__debug_decode_pc_out;
      wire[7:0] tribe__debug_decode_br_out;
      wire[31:0] tribe__debug_decode_imm_out;
      wire tribe__sbi_set_timer_out;
      wire[31:0] tribe__sbi_timer_lo_out;
      wire[31:0] tribe__sbi_timer_hi_out;
      wire[31:0] tribe__reset_pc_in;
      wire[31:0] tribe__boot_hartid_in;
      wire[31:0] tribe__boot_dtb_addr_in;
      wire[2-1:0] tribe__boot_priv_in;
      wire[31:0] tribe__memory_base_in;
      wire[31:0] tribe__memory_size_in;
      wire[31:0] tribe__mem_region_size_in[4];
      wire tribe__clint_msip_in;
      wire tribe__clint_mtip_in;
      wire tribe__external_irq_in;
      wire tribe__axi_in__awvalid_in[4];
      wire tribe__axi_in__awready_out[4];
      wire[19-1:0] tribe__axi_in__awaddr_in[4];
      wire[4-1:0] tribe__axi_in__awid_in[4];
      wire tribe__axi_in__wvalid_in[4];
      wire tribe__axi_in__wready_out[4];
      wire[128-1:0] tribe__axi_in__wdata_in[4];
      wire tribe__axi_in__wlast_in[4];
      wire tribe__axi_in__bvalid_out[4];
      wire tribe__axi_in__bready_in[4];
      wire[4-1:0] tribe__axi_in__bid_out[4];
      wire tribe__axi_in__arvalid_in[4];
      wire tribe__axi_in__arready_out[4];
      wire[19-1:0] tribe__axi_in__araddr_in[4];
      wire[4-1:0] tribe__axi_in__arid_in[4];
      wire tribe__axi_in__rvalid_out[4];
      wire tribe__axi_in__rready_in[4];
      wire[128-1:0] tribe__axi_in__rdata_out[4];
      wire tribe__axi_in__rlast_out[4];
      wire[4-1:0] tribe__axi_in__rid_out[4];
      wire tribe__axi_out__awvalid_out[4];
      wire tribe__axi_out__awready_in[4];
      wire[19-1:0] tribe__axi_out__awaddr_out[4];
      wire[4-1:0] tribe__axi_out__awid_out[4];
      wire tribe__axi_out__wvalid_out[4];
      wire tribe__axi_out__wready_in[4];
      wire[128-1:0] tribe__axi_out__wdata_out[4];
      wire tribe__axi_out__wlast_out[4];
      wire tribe__axi_out__bvalid_in[4];
      wire tribe__axi_out__bready_out[4];
      wire[4-1:0] tribe__axi_out__bid_in[4];
      wire tribe__axi_out__arvalid_out[4];
      wire tribe__axi_out__arready_in[4];
      wire[19-1:0] tribe__axi_out__araddr_out[4];
      wire[4-1:0] tribe__axi_out__arid_out[4];
      wire tribe__axi_out__rvalid_in[4];
      wire tribe__axi_out__rready_out[4];
      wire[128-1:0] tribe__axi_out__rdata_in[4];
      wire tribe__axi_out__rlast_in[4];
      wire[4-1:0] tribe__axi_out__rid_in[4];
      TribePerf tribe__perf_out;
      wire tribe__debugen_in;
    Tribe      tribe (
        .clk(clk)
,       .reset(reset)
,       .dmem_write_out(tribe__dmem_write_out)
,       .dmem_write_data_out(tribe__dmem_write_data_out)
,       .dmem_write_mask_out(tribe__dmem_write_mask_out)
,       .dmem_read_out(tribe__dmem_read_out)
,       .dmem_addr_out(tribe__dmem_addr_out)
,       .imem_read_addr_out(tribe__imem_read_addr_out)
,       .debug_immu_ptw_read_out(tribe__debug_immu_ptw_read_out)
,       .debug_immu_ptw_addr_out(tribe__debug_immu_ptw_addr_out)
,       .debug_immu_busy_out(tribe__debug_immu_busy_out)
,       .debug_immu_fault_out(tribe__debug_immu_fault_out)
,       .debug_immu_paddr_out(tribe__debug_immu_paddr_out)
,       .debug_immu_last_addr_out(tribe__debug_immu_last_addr_out)
,       .debug_immu_last_pte_out(tribe__debug_immu_last_pte_out)
,       .debug_icache_read_valid_out(tribe__debug_icache_read_valid_out)
,       .debug_icache_read_addr_out(tribe__debug_icache_read_addr_out)
,       .debug_fetch_valid_out(tribe__debug_fetch_valid_out)
,       .debug_memory_wait_out(tribe__debug_memory_wait_out)
,       .debug_wb_load_ready_out(tribe__debug_wb_load_ready_out)
,       .debug_wb_mem_wait_out(tribe__debug_wb_mem_wait_out)
,       .debug_icache_read_in_out(tribe__debug_icache_read_in_out)
,       .debug_icache_stall_in_out(tribe__debug_icache_stall_in_out)
,       .debug_dmmu_ptw_read_out(tribe__debug_dmmu_ptw_read_out)
,       .debug_dmmu_ptw_addr_out(tribe__debug_dmmu_ptw_addr_out)
,       .debug_dmmu_busy_out(tribe__debug_dmmu_busy_out)
,       .debug_dmmu_fault_out(tribe__debug_dmmu_fault_out)
,       .debug_mmu_ptw_word_out(tribe__debug_mmu_ptw_word_out)
,       .debug_pc_out(tribe__debug_pc_out)
,       .debug_satp_out(tribe__debug_satp_out)
,       .debug_mstatus_out(tribe__debug_mstatus_out)
,       .debug_mtvec_out(tribe__debug_mtvec_out)
,       .debug_mepc_out(tribe__debug_mepc_out)
,       .debug_mcause_out(tribe__debug_mcause_out)
,       .debug_mtval_out(tribe__debug_mtval_out)
,       .debug_sepc_out(tribe__debug_sepc_out)
,       .debug_stvec_out(tribe__debug_stvec_out)
,       .debug_scause_out(tribe__debug_scause_out)
,       .debug_stval_out(tribe__debug_stval_out)
,       .debug_irq_valid_out(tribe__debug_irq_valid_out)
,       .debug_irq_cause_out(tribe__debug_irq_cause_out)
,       .debug_irq_to_supervisor_out(tribe__debug_irq_to_supervisor_out)
,       .debug_irq_mip_out(tribe__debug_irq_mip_out)
,       .debug_irq_mie_out(tribe__debug_irq_mie_out)
,       .debug_irq_mideleg_out(tribe__debug_irq_mideleg_out)
,       .debug_priv_out(tribe__debug_priv_out)
,       .debug_ra_out(tribe__debug_ra_out)
,       .debug_regs_write_out(tribe__debug_regs_write_out)
,       .debug_regs_write_actual_out(tribe__debug_regs_write_actual_out)
,       .debug_regs_wr_id_out(tribe__debug_regs_wr_id_out)
,       .debug_regs_data_out(tribe__debug_regs_data_out)
,       .debug_branch_taken_now_out(tribe__debug_branch_taken_now_out)
,       .debug_branch_target_now_out(tribe__debug_branch_target_now_out)
,       .debug_decode_instr_out(tribe__debug_decode_instr_out)
,       .debug_decode_pc_out(tribe__debug_decode_pc_out)
,       .debug_decode_br_out(tribe__debug_decode_br_out)
,       .debug_decode_imm_out(tribe__debug_decode_imm_out)
,       .sbi_set_timer_out(tribe__sbi_set_timer_out)
,       .sbi_timer_lo_out(tribe__sbi_timer_lo_out)
,       .sbi_timer_hi_out(tribe__sbi_timer_hi_out)
,       .reset_pc_in(tribe__reset_pc_in)
,       .boot_hartid_in(tribe__boot_hartid_in)
,       .boot_dtb_addr_in(tribe__boot_dtb_addr_in)
,       .boot_priv_in(tribe__boot_priv_in)
,       .memory_base_in(tribe__memory_base_in)
,       .memory_size_in(tribe__memory_size_in)
,       .mem_region_size_in(tribe__mem_region_size_in)
,       .clint_msip_in(tribe__clint_msip_in)
,       .clint_mtip_in(tribe__clint_mtip_in)
,       .external_irq_in(tribe__external_irq_in)
,       .axi_in__awvalid_in(tribe__axi_in__awvalid_in)
,       .axi_in__awready_out(tribe__axi_in__awready_out)
,       .axi_in__awaddr_in(tribe__axi_in__awaddr_in)
,       .axi_in__awid_in(tribe__axi_in__awid_in)
,       .axi_in__wvalid_in(tribe__axi_in__wvalid_in)
,       .axi_in__wready_out(tribe__axi_in__wready_out)
,       .axi_in__wdata_in(tribe__axi_in__wdata_in)
,       .axi_in__wlast_in(tribe__axi_in__wlast_in)
,       .axi_in__bvalid_out(tribe__axi_in__bvalid_out)
,       .axi_in__bready_in(tribe__axi_in__bready_in)
,       .axi_in__bid_out(tribe__axi_in__bid_out)
,       .axi_in__arvalid_in(tribe__axi_in__arvalid_in)
,       .axi_in__arready_out(tribe__axi_in__arready_out)
,       .axi_in__araddr_in(tribe__axi_in__araddr_in)
,       .axi_in__arid_in(tribe__axi_in__arid_in)
,       .axi_in__rvalid_out(tribe__axi_in__rvalid_out)
,       .axi_in__rready_in(tribe__axi_in__rready_in)
,       .axi_in__rdata_out(tribe__axi_in__rdata_out)
,       .axi_in__rlast_out(tribe__axi_in__rlast_out)
,       .axi_in__rid_out(tribe__axi_in__rid_out)
,       .axi_out__awvalid_out(tribe__axi_out__awvalid_out)
,       .axi_out__awready_in(tribe__axi_out__awready_in)
,       .axi_out__awaddr_out(tribe__axi_out__awaddr_out)
,       .axi_out__awid_out(tribe__axi_out__awid_out)
,       .axi_out__wvalid_out(tribe__axi_out__wvalid_out)
,       .axi_out__wready_in(tribe__axi_out__wready_in)
,       .axi_out__wdata_out(tribe__axi_out__wdata_out)
,       .axi_out__wlast_out(tribe__axi_out__wlast_out)
,       .axi_out__bvalid_in(tribe__axi_out__bvalid_in)
,       .axi_out__bready_out(tribe__axi_out__bready_out)
,       .axi_out__bid_in(tribe__axi_out__bid_in)
,       .axi_out__arvalid_out(tribe__axi_out__arvalid_out)
,       .axi_out__arready_in(tribe__axi_out__arready_in)
,       .axi_out__araddr_out(tribe__axi_out__araddr_out)
,       .axi_out__arid_out(tribe__axi_out__arid_out)
,       .axi_out__rvalid_in(tribe__axi_out__rvalid_in)
,       .axi_out__rready_out(tribe__axi_out__rready_out)
,       .axi_out__rdata_in(tribe__axi_out__rdata_in)
,       .axi_out__rlast_in(tribe__axi_out__rlast_in)
,       .axi_out__rid_in(tribe__axi_out__rid_in)
,       .perf_out(tribe__perf_out)
,       .debugen_in(tribe__debugen_in)
    );
      wire mem2__axi_in__awvalid_in;
      wire mem2__axi_in__awready_out;
      wire[(19)-1:0] mem2__axi_in__awaddr_in;
      wire[(4)-1:0] mem2__axi_in__awid_in;
      wire mem2__axi_in__wvalid_in;
      wire mem2__axi_in__wready_out;
      wire[(128)-1:0] mem2__axi_in__wdata_in;
      wire mem2__axi_in__wlast_in;
      wire mem2__axi_in__bvalid_out;
      wire mem2__axi_in__bready_in;
      wire[(4)-1:0] mem2__axi_in__bid_out;
      wire mem2__axi_in__arvalid_in;
      wire mem2__axi_in__arready_out;
      wire[(19)-1:0] mem2__axi_in__araddr_in;
      wire[(4)-1:0] mem2__axi_in__arid_in;
      wire mem2__axi_in__rvalid_out;
      wire mem2__axi_in__rready_in;
      wire[(128)-1:0] mem2__axi_in__rdata_out;
      wire mem2__axi_in__rlast_out;
      wire[(4)-1:0] mem2__axi_in__rid_out;
      wire mem2__debugen_in;
    Axi4Ram #(
        19
,       4
,       128
,       7168
    ) mem2 (
        .clk(clk)
,       .reset(reset)
,       .axi_in__awvalid_in(mem2__axi_in__awvalid_in)
,       .axi_in__awready_out(mem2__axi_in__awready_out)
,       .axi_in__awaddr_in(mem2__axi_in__awaddr_in)
,       .axi_in__awid_in(mem2__axi_in__awid_in)
,       .axi_in__wvalid_in(mem2__axi_in__wvalid_in)
,       .axi_in__wready_out(mem2__axi_in__wready_out)
,       .axi_in__wdata_in(mem2__axi_in__wdata_in)
,       .axi_in__wlast_in(mem2__axi_in__wlast_in)
,       .axi_in__bvalid_out(mem2__axi_in__bvalid_out)
,       .axi_in__bready_in(mem2__axi_in__bready_in)
,       .axi_in__bid_out(mem2__axi_in__bid_out)
,       .axi_in__arvalid_in(mem2__axi_in__arvalid_in)
,       .axi_in__arready_out(mem2__axi_in__arready_out)
,       .axi_in__araddr_in(mem2__axi_in__araddr_in)
,       .axi_in__arid_in(mem2__axi_in__arid_in)
,       .axi_in__rvalid_out(mem2__axi_in__rvalid_out)
,       .axi_in__rready_in(mem2__axi_in__rready_in)
,       .axi_in__rdata_out(mem2__axi_in__rdata_out)
,       .axi_in__rlast_out(mem2__axi_in__rlast_out)
,       .axi_in__rid_out(mem2__axi_in__rid_out)
,       .debugen_in(mem2__debugen_in)
    );
      wire iospace__slave_in__awvalid_in;
      wire iospace__slave_in__awready_out;
      wire[(19)-1:0] iospace__slave_in__awaddr_in;
      wire[(4)-1:0] iospace__slave_in__awid_in;
      wire iospace__slave_in__wvalid_in;
      wire iospace__slave_in__wready_out;
      wire[(128)-1:0] iospace__slave_in__wdata_in;
      wire iospace__slave_in__wlast_in;
      wire iospace__slave_in__bvalid_out;
      wire iospace__slave_in__bready_in;
      wire[(4)-1:0] iospace__slave_in__bid_out;
      wire iospace__slave_in__arvalid_in;
      wire iospace__slave_in__arready_out;
      wire[(19)-1:0] iospace__slave_in__araddr_in;
      wire[(4)-1:0] iospace__slave_in__arid_in;
      wire iospace__slave_in__rvalid_out;
      wire iospace__slave_in__rready_in;
      wire[(128)-1:0] iospace__slave_in__rdata_out;
      wire iospace__slave_in__rlast_out;
      wire[(4)-1:0] iospace__slave_in__rid_out;
      wire iospace__masters_out__awvalid_out[(4)];
      wire iospace__masters_out__awready_in[(4)];
      wire[(19)-1:0] iospace__masters_out__awaddr_out[(4)];
      wire[(4)-1:0] iospace__masters_out__awid_out[(4)];
      wire iospace__masters_out__wvalid_out[(4)];
      wire iospace__masters_out__wready_in[(4)];
      wire[(128)-1:0] iospace__masters_out__wdata_out[(4)];
      wire iospace__masters_out__wlast_out[(4)];
      wire iospace__masters_out__bvalid_in[(4)];
      wire iospace__masters_out__bready_out[(4)];
      wire[(4)-1:0] iospace__masters_out__bid_in[(4)];
      wire iospace__masters_out__arvalid_out[(4)];
      wire iospace__masters_out__arready_in[(4)];
      wire[(19)-1:0] iospace__masters_out__araddr_out[(4)];
      wire[(4)-1:0] iospace__masters_out__arid_out[(4)];
      wire iospace__masters_out__rvalid_in[(4)];
      wire iospace__masters_out__rready_out[(4)];
      wire[(128)-1:0] iospace__masters_out__rdata_in[(4)];
      wire iospace__masters_out__rlast_in[(4)];
      wire[(4)-1:0] iospace__masters_out__rid_in[(4)];
      wire[31:0] iospace__region_base_in[(4)];
      wire[31:0] iospace__region_size_in[(4)];
    Axi4RegionMux #(
        4
,       19
,       4
,       128
    ) iospace (
        .clk(clk)
,       .reset(reset)
,       .slave_in__awvalid_in(iospace__slave_in__awvalid_in)
,       .slave_in__awready_out(iospace__slave_in__awready_out)
,       .slave_in__awaddr_in(iospace__slave_in__awaddr_in)
,       .slave_in__awid_in(iospace__slave_in__awid_in)
,       .slave_in__wvalid_in(iospace__slave_in__wvalid_in)
,       .slave_in__wready_out(iospace__slave_in__wready_out)
,       .slave_in__wdata_in(iospace__slave_in__wdata_in)
,       .slave_in__wlast_in(iospace__slave_in__wlast_in)
,       .slave_in__bvalid_out(iospace__slave_in__bvalid_out)
,       .slave_in__bready_in(iospace__slave_in__bready_in)
,       .slave_in__bid_out(iospace__slave_in__bid_out)
,       .slave_in__arvalid_in(iospace__slave_in__arvalid_in)
,       .slave_in__arready_out(iospace__slave_in__arready_out)
,       .slave_in__araddr_in(iospace__slave_in__araddr_in)
,       .slave_in__arid_in(iospace__slave_in__arid_in)
,       .slave_in__rvalid_out(iospace__slave_in__rvalid_out)
,       .slave_in__rready_in(iospace__slave_in__rready_in)
,       .slave_in__rdata_out(iospace__slave_in__rdata_out)
,       .slave_in__rlast_out(iospace__slave_in__rlast_out)
,       .slave_in__rid_out(iospace__slave_in__rid_out)
,       .masters_out__awvalid_out(iospace__masters_out__awvalid_out)
,       .masters_out__awready_in(iospace__masters_out__awready_in)
,       .masters_out__awaddr_out(iospace__masters_out__awaddr_out)
,       .masters_out__awid_out(iospace__masters_out__awid_out)
,       .masters_out__wvalid_out(iospace__masters_out__wvalid_out)
,       .masters_out__wready_in(iospace__masters_out__wready_in)
,       .masters_out__wdata_out(iospace__masters_out__wdata_out)
,       .masters_out__wlast_out(iospace__masters_out__wlast_out)
,       .masters_out__bvalid_in(iospace__masters_out__bvalid_in)
,       .masters_out__bready_out(iospace__masters_out__bready_out)
,       .masters_out__bid_in(iospace__masters_out__bid_in)
,       .masters_out__arvalid_out(iospace__masters_out__arvalid_out)
,       .masters_out__arready_in(iospace__masters_out__arready_in)
,       .masters_out__araddr_out(iospace__masters_out__araddr_out)
,       .masters_out__arid_out(iospace__masters_out__arid_out)
,       .masters_out__rvalid_in(iospace__masters_out__rvalid_in)
,       .masters_out__rready_out(iospace__masters_out__rready_out)
,       .masters_out__rdata_in(iospace__masters_out__rdata_in)
,       .masters_out__rlast_in(iospace__masters_out__rlast_in)
,       .masters_out__rid_in(iospace__masters_out__rid_in)
,       .region_base_in(iospace__region_base_in)
,       .region_size_in(iospace__region_size_in)
    );
      wire uart__axi_in__awvalid_in;
      wire uart__axi_in__awready_out;
      wire[(19)-1:0] uart__axi_in__awaddr_in;
      wire[(4)-1:0] uart__axi_in__awid_in;
      wire uart__axi_in__wvalid_in;
      wire uart__axi_in__wready_out;
      wire[(128)-1:0] uart__axi_in__wdata_in;
      wire uart__axi_in__wlast_in;
      wire uart__axi_in__bvalid_out;
      wire uart__axi_in__bready_in;
      wire[(4)-1:0] uart__axi_in__bid_out;
      wire uart__axi_in__arvalid_in;
      wire uart__axi_in__arready_out;
      wire[(19)-1:0] uart__axi_in__araddr_in;
      wire[(4)-1:0] uart__axi_in__arid_in;
      wire uart__axi_in__rvalid_out;
      wire uart__axi_in__rready_in;
      wire[(128)-1:0] uart__axi_in__rdata_out;
      wire uart__axi_in__rlast_out;
      wire[(4)-1:0] uart__axi_in__rid_out;
      wire uart__uart_valid_out;
      wire[7:0] uart__uart_data_out;
      wire uart__uart_rx_valid_in;
      wire[7:0] uart__uart_rx_data_in;
      wire uart__uart_rx_ready_out;
      wire uart__irq_out;
    NS16550A #(
        19
,       4
,       128
    ) uart (
        .clk(clk)
,       .reset(reset)
,       .axi_in__awvalid_in(uart__axi_in__awvalid_in)
,       .axi_in__awready_out(uart__axi_in__awready_out)
,       .axi_in__awaddr_in(uart__axi_in__awaddr_in)
,       .axi_in__awid_in(uart__axi_in__awid_in)
,       .axi_in__wvalid_in(uart__axi_in__wvalid_in)
,       .axi_in__wready_out(uart__axi_in__wready_out)
,       .axi_in__wdata_in(uart__axi_in__wdata_in)
,       .axi_in__wlast_in(uart__axi_in__wlast_in)
,       .axi_in__bvalid_out(uart__axi_in__bvalid_out)
,       .axi_in__bready_in(uart__axi_in__bready_in)
,       .axi_in__bid_out(uart__axi_in__bid_out)
,       .axi_in__arvalid_in(uart__axi_in__arvalid_in)
,       .axi_in__arready_out(uart__axi_in__arready_out)
,       .axi_in__araddr_in(uart__axi_in__araddr_in)
,       .axi_in__arid_in(uart__axi_in__arid_in)
,       .axi_in__rvalid_out(uart__axi_in__rvalid_out)
,       .axi_in__rready_in(uart__axi_in__rready_in)
,       .axi_in__rdata_out(uart__axi_in__rdata_out)
,       .axi_in__rlast_out(uart__axi_in__rlast_out)
,       .axi_in__rid_out(uart__axi_in__rid_out)
,       .uart_valid_out(uart__uart_valid_out)
,       .uart_data_out(uart__uart_data_out)
,       .uart_rx_valid_in(uart__uart_rx_valid_in)
,       .uart_rx_data_in(uart__uart_rx_data_in)
,       .uart_rx_ready_out(uart__uart_rx_ready_out)
,       .irq_out(uart__irq_out)
    );
      wire clint__axi_in__awvalid_in;
      wire clint__axi_in__awready_out;
      wire[(19)-1:0] clint__axi_in__awaddr_in;
      wire[(4)-1:0] clint__axi_in__awid_in;
      wire clint__axi_in__wvalid_in;
      wire clint__axi_in__wready_out;
      wire[(128)-1:0] clint__axi_in__wdata_in;
      wire clint__axi_in__wlast_in;
      wire clint__axi_in__bvalid_out;
      wire clint__axi_in__bready_in;
      wire[(4)-1:0] clint__axi_in__bid_out;
      wire clint__axi_in__arvalid_in;
      wire clint__axi_in__arready_out;
      wire[(19)-1:0] clint__axi_in__araddr_in;
      wire[(4)-1:0] clint__axi_in__arid_in;
      wire clint__axi_in__rvalid_out;
      wire clint__axi_in__rready_in;
      wire[(128)-1:0] clint__axi_in__rdata_out;
      wire clint__axi_in__rlast_out;
      wire[(4)-1:0] clint__axi_in__rid_out;
      wire clint__set_mtimecmp_in;
      wire[31:0] clint__set_mtimecmp_lo_in;
      wire[31:0] clint__set_mtimecmp_hi_in;
      wire clint__msip_out;
      wire clint__mtip_out;
    CLINT #(
        19
,       4
,       128
    ) clint (
        .clk(clk)
,       .reset(reset)
,       .axi_in__awvalid_in(clint__axi_in__awvalid_in)
,       .axi_in__awready_out(clint__axi_in__awready_out)
,       .axi_in__awaddr_in(clint__axi_in__awaddr_in)
,       .axi_in__awid_in(clint__axi_in__awid_in)
,       .axi_in__wvalid_in(clint__axi_in__wvalid_in)
,       .axi_in__wready_out(clint__axi_in__wready_out)
,       .axi_in__wdata_in(clint__axi_in__wdata_in)
,       .axi_in__wlast_in(clint__axi_in__wlast_in)
,       .axi_in__bvalid_out(clint__axi_in__bvalid_out)
,       .axi_in__bready_in(clint__axi_in__bready_in)
,       .axi_in__bid_out(clint__axi_in__bid_out)
,       .axi_in__arvalid_in(clint__axi_in__arvalid_in)
,       .axi_in__arready_out(clint__axi_in__arready_out)
,       .axi_in__araddr_in(clint__axi_in__araddr_in)
,       .axi_in__arid_in(clint__axi_in__arid_in)
,       .axi_in__rvalid_out(clint__axi_in__rvalid_out)
,       .axi_in__rready_in(clint__axi_in__rready_in)
,       .axi_in__rdata_out(clint__axi_in__rdata_out)
,       .axi_in__rlast_out(clint__axi_in__rlast_out)
,       .axi_in__rid_out(clint__axi_in__rid_out)
,       .set_mtimecmp_in(clint__set_mtimecmp_in)
,       .set_mtimecmp_lo_in(clint__set_mtimecmp_lo_in)
,       .set_mtimecmp_hi_in(clint__set_mtimecmp_hi_in)
,       .msip_out(clint__msip_out)
,       .mtip_out(clint__mtip_out)
    );
      wire plic__axi_in__awvalid_in;
      wire plic__axi_in__awready_out;
      wire[(19)-1:0] plic__axi_in__awaddr_in;
      wire[(4)-1:0] plic__axi_in__awid_in;
      wire plic__axi_in__wvalid_in;
      wire plic__axi_in__wready_out;
      wire[(128)-1:0] plic__axi_in__wdata_in;
      wire plic__axi_in__wlast_in;
      wire plic__axi_in__bvalid_out;
      wire plic__axi_in__bready_in;
      wire[(4)-1:0] plic__axi_in__bid_out;
      wire plic__axi_in__arvalid_in;
      wire plic__axi_in__arready_out;
      wire[(19)-1:0] plic__axi_in__araddr_in;
      wire[(4)-1:0] plic__axi_in__arid_in;
      wire plic__axi_in__rvalid_out;
      wire plic__axi_in__rready_in;
      wire[(128)-1:0] plic__axi_in__rdata_out;
      wire plic__axi_in__rlast_out;
      wire[(4)-1:0] plic__axi_in__rid_out;
      wire plic__source_irq_in[(32)];
      wire plic__external_irq_out;
    PLIC #(
        19
,       4
,       128
,       32
    ) plic (
        .clk(clk)
,       .reset(reset)
,       .axi_in__awvalid_in(plic__axi_in__awvalid_in)
,       .axi_in__awready_out(plic__axi_in__awready_out)
,       .axi_in__awaddr_in(plic__axi_in__awaddr_in)
,       .axi_in__awid_in(plic__axi_in__awid_in)
,       .axi_in__wvalid_in(plic__axi_in__wvalid_in)
,       .axi_in__wready_out(plic__axi_in__wready_out)
,       .axi_in__wdata_in(plic__axi_in__wdata_in)
,       .axi_in__wlast_in(plic__axi_in__wlast_in)
,       .axi_in__bvalid_out(plic__axi_in__bvalid_out)
,       .axi_in__bready_in(plic__axi_in__bready_in)
,       .axi_in__bid_out(plic__axi_in__bid_out)
,       .axi_in__arvalid_in(plic__axi_in__arvalid_in)
,       .axi_in__arready_out(plic__axi_in__arready_out)
,       .axi_in__araddr_in(plic__axi_in__araddr_in)
,       .axi_in__arid_in(plic__axi_in__arid_in)
,       .axi_in__rvalid_out(plic__axi_in__rvalid_out)
,       .axi_in__rready_in(plic__axi_in__rready_in)
,       .axi_in__rdata_out(plic__axi_in__rdata_out)
,       .axi_in__rlast_out(plic__axi_in__rlast_out)
,       .axi_in__rid_out(plic__axi_in__rid_out)
,       .source_irq_in(plic__source_irq_in)
,       .external_irq_out(plic__external_irq_out)
    );
      wire accelerator__axi_in__awvalid_in;
      wire accelerator__axi_in__awready_out;
      wire[(19)-1:0] accelerator__axi_in__awaddr_in;
      wire[(4)-1:0] accelerator__axi_in__awid_in;
      wire accelerator__axi_in__wvalid_in;
      wire accelerator__axi_in__wready_out;
      wire[(128)-1:0] accelerator__axi_in__wdata_in;
      wire accelerator__axi_in__wlast_in;
      wire accelerator__axi_in__bvalid_out;
      wire accelerator__axi_in__bready_in;
      wire[(4)-1:0] accelerator__axi_in__bid_out;
      wire accelerator__axi_in__arvalid_in;
      wire accelerator__axi_in__arready_out;
      wire[(19)-1:0] accelerator__axi_in__araddr_in;
      wire[(4)-1:0] accelerator__axi_in__arid_in;
      wire accelerator__axi_in__rvalid_out;
      wire accelerator__axi_in__rready_in;
      wire[(128)-1:0] accelerator__axi_in__rdata_out;
      wire accelerator__axi_in__rlast_out;
      wire[(4)-1:0] accelerator__axi_in__rid_out;
      wire accelerator__dma_out__awvalid_out;
      wire accelerator__dma_out__awready_in;
      wire[(19)-1:0] accelerator__dma_out__awaddr_out;
      wire[(4)-1:0] accelerator__dma_out__awid_out;
      wire accelerator__dma_out__wvalid_out;
      wire accelerator__dma_out__wready_in;
      wire[(128)-1:0] accelerator__dma_out__wdata_out;
      wire accelerator__dma_out__wlast_out;
      wire accelerator__dma_out__bvalid_in;
      wire accelerator__dma_out__bready_out;
      wire[(4)-1:0] accelerator__dma_out__bid_in;
      wire accelerator__dma_out__arvalid_out;
      wire accelerator__dma_out__arready_in;
      wire[(19)-1:0] accelerator__dma_out__araddr_out;
      wire[(4)-1:0] accelerator__dma_out__arid_out;
      wire accelerator__dma_out__rvalid_in;
      wire accelerator__dma_out__rready_out;
      wire[(128)-1:0] accelerator__dma_out__rdata_in;
      wire accelerator__dma_out__rlast_in;
      wire[(4)-1:0] accelerator__dma_out__rid_in;
    Accelerator #(
        19
,       4
,       128
,       256
    ) accelerator (
        .clk(clk)
,       .reset(reset)
,       .axi_in__awvalid_in(accelerator__axi_in__awvalid_in)
,       .axi_in__awready_out(accelerator__axi_in__awready_out)
,       .axi_in__awaddr_in(accelerator__axi_in__awaddr_in)
,       .axi_in__awid_in(accelerator__axi_in__awid_in)
,       .axi_in__wvalid_in(accelerator__axi_in__wvalid_in)
,       .axi_in__wready_out(accelerator__axi_in__wready_out)
,       .axi_in__wdata_in(accelerator__axi_in__wdata_in)
,       .axi_in__wlast_in(accelerator__axi_in__wlast_in)
,       .axi_in__bvalid_out(accelerator__axi_in__bvalid_out)
,       .axi_in__bready_in(accelerator__axi_in__bready_in)
,       .axi_in__bid_out(accelerator__axi_in__bid_out)
,       .axi_in__arvalid_in(accelerator__axi_in__arvalid_in)
,       .axi_in__arready_out(accelerator__axi_in__arready_out)
,       .axi_in__araddr_in(accelerator__axi_in__araddr_in)
,       .axi_in__arid_in(accelerator__axi_in__arid_in)
,       .axi_in__rvalid_out(accelerator__axi_in__rvalid_out)
,       .axi_in__rready_in(accelerator__axi_in__rready_in)
,       .axi_in__rdata_out(accelerator__axi_in__rdata_out)
,       .axi_in__rlast_out(accelerator__axi_in__rlast_out)
,       .axi_in__rid_out(accelerator__axi_in__rid_out)
,       .dma_out__awvalid_out(accelerator__dma_out__awvalid_out)
,       .dma_out__awready_in(accelerator__dma_out__awready_in)
,       .dma_out__awaddr_out(accelerator__dma_out__awaddr_out)
,       .dma_out__awid_out(accelerator__dma_out__awid_out)
,       .dma_out__wvalid_out(accelerator__dma_out__wvalid_out)
,       .dma_out__wready_in(accelerator__dma_out__wready_in)
,       .dma_out__wdata_out(accelerator__dma_out__wdata_out)
,       .dma_out__wlast_out(accelerator__dma_out__wlast_out)
,       .dma_out__bvalid_in(accelerator__dma_out__bvalid_in)
,       .dma_out__bready_out(accelerator__dma_out__bready_out)
,       .dma_out__bid_in(accelerator__dma_out__bid_in)
,       .dma_out__arvalid_out(accelerator__dma_out__arvalid_out)
,       .dma_out__arready_in(accelerator__dma_out__arready_in)
,       .dma_out__araddr_out(accelerator__dma_out__araddr_out)
,       .dma_out__arid_out(accelerator__dma_out__arid_out)
,       .dma_out__rvalid_in(accelerator__dma_out__rvalid_in)
,       .dma_out__rready_out(accelerator__dma_out__rready_out)
,       .dma_out__rdata_in(accelerator__dma_out__rdata_in)
,       .dma_out__rlast_in(accelerator__dma_out__rlast_in)
,       .dma_out__rid_in(accelerator__dma_out__rid_in)
    );

    // tmp variables


    generate  // _assign
        genvar gi;
        assign tribe__debugen_in=debugen_in;
        assign tribe__reset_pc_in = reset_pc_in;
        assign tribe__boot_hartid_in = boot_hartid_in;
        assign tribe__boot_dtb_addr_in = boot_dtb_addr_in;
        assign tribe__boot_priv_in = boot_priv_in;
        assign tribe__memory_base_in = memory_base_in;
        assign tribe__memory_size_in = memory_size_in;
        for (gi='h0;gi < 'h4;gi=gi+1) begin
            assign tribe__mem_region_size_in[gi] = mem_region_size_in[gi];
            assign tribe__axi_in__awvalid_in[gi] = 0;
            assign tribe__axi_in__awaddr_in[gi] = unsigned'(19'(unsigned'(19'('h0))));
            assign tribe__axi_in__awid_in[gi] = unsigned'(4'(unsigned'(4'('h0))));
            assign tribe__axi_in__wvalid_in[gi] = 0;
            assign tribe__axi_in__wdata_in[gi] = 'h0;
            assign tribe__axi_in__wlast_in[gi] = 0;
            assign tribe__axi_in__bready_in[gi] = 0;
            assign tribe__axi_in__arvalid_in[gi] = 0;
            assign tribe__axi_in__araddr_in[gi] = unsigned'(19'(unsigned'(19'('h0))));
            assign tribe__axi_in__arid_in[gi] = unsigned'(4'(unsigned'(4'('h0))));
            assign tribe__axi_in__rready_in[gi] = 0;
        end
        assign tribe__axi_in__awvalid_in['h0] = accelerator__dma_out__awvalid_out;
        assign tribe__axi_in__awaddr_in['h0] = unsigned'(19'(unsigned'(19'(unsigned'(32'(accelerator__dma_out__awaddr_out))))));
        assign tribe__axi_in__awid_in['h0] = unsigned'(4'(unsigned'(4'(unsigned'(32'(accelerator__dma_out__awid_out))))));
        assign tribe__axi_in__wvalid_in['h0] = accelerator__dma_out__wvalid_out;
        assign tribe__axi_in__wdata_in['h0] = accelerator__dma_out__wdata_out;
        assign tribe__axi_in__wlast_in['h0] = accelerator__dma_out__wlast_out;
        assign tribe__axi_in__bready_in['h0] = accelerator__dma_out__bready_out;
        assign tribe__axi_in__arvalid_in['h0] = accelerator__dma_out__arvalid_out;
        assign tribe__axi_in__araddr_in['h0] = unsigned'(19'(unsigned'(19'(unsigned'(32'(accelerator__dma_out__araddr_out))))));
        assign tribe__axi_in__arid_in['h0] = unsigned'(4'(unsigned'(4'(unsigned'(32'(accelerator__dma_out__arid_out))))));
        assign tribe__axi_in__rready_in['h0] = accelerator__dma_out__rready_out;
        assign tribe__clint_msip_in = clint__msip_out;
        assign tribe__clint_mtip_in = clint__mtip_out;
        assign tribe__external_irq_in = plic__external_irq_out;
        assign axi_out__awvalid_out['h0] = tribe__axi_out__awvalid_out['h0];
        assign axi_out__awaddr_out['h0] = tribe__axi_out__awaddr_out['h0];
        assign axi_out__awid_out['h0] = tribe__axi_out__awid_out['h0];
        assign axi_out__wvalid_out['h0] = tribe__axi_out__wvalid_out['h0];
        assign axi_out__wdata_out['h0] = tribe__axi_out__wdata_out['h0];
        assign axi_out__wlast_out['h0] = tribe__axi_out__wlast_out['h0];
        assign axi_out__bready_out['h0] = tribe__axi_out__bready_out['h0];
        assign axi_out__arvalid_out['h0] = tribe__axi_out__arvalid_out['h0];
        assign axi_out__araddr_out['h0] = tribe__axi_out__araddr_out['h0];
        assign axi_out__arid_out['h0] = tribe__axi_out__arid_out['h0];
        assign axi_out__rready_out['h0] = tribe__axi_out__rready_out['h0];
        assign axi_out__awvalid_out['h1] = tribe__axi_out__awvalid_out['h1];
        assign axi_out__awaddr_out['h1] = tribe__axi_out__awaddr_out['h1];
        assign axi_out__awid_out['h1] = tribe__axi_out__awid_out['h1];
        assign axi_out__wvalid_out['h1] = tribe__axi_out__wvalid_out['h1];
        assign axi_out__wdata_out['h1] = tribe__axi_out__wdata_out['h1];
        assign axi_out__wlast_out['h1] = tribe__axi_out__wlast_out['h1];
        assign axi_out__bready_out['h1] = tribe__axi_out__bready_out['h1];
        assign axi_out__arvalid_out['h1] = tribe__axi_out__arvalid_out['h1];
        assign axi_out__araddr_out['h1] = tribe__axi_out__araddr_out['h1];
        assign axi_out__arid_out['h1] = tribe__axi_out__arid_out['h1];
        assign axi_out__rready_out['h1] = tribe__axi_out__rready_out['h1];
        assign mem2__axi_in__awvalid_in = tribe__axi_out__awvalid_out['h2];
        assign mem2__axi_in__awaddr_in = tribe__axi_out__awaddr_out['h2];
        assign mem2__axi_in__awid_in = tribe__axi_out__awid_out['h2];
        assign mem2__axi_in__wvalid_in = tribe__axi_out__wvalid_out['h2];
        assign mem2__axi_in__wdata_in = tribe__axi_out__wdata_out['h2];
        assign mem2__axi_in__wlast_in = tribe__axi_out__wlast_out['h2];
        assign mem2__axi_in__bready_in = tribe__axi_out__bready_out['h2];
        assign mem2__axi_in__arvalid_in = tribe__axi_out__arvalid_out['h2];
        assign mem2__axi_in__araddr_in = tribe__axi_out__araddr_out['h2];
        assign mem2__axi_in__arid_in = tribe__axi_out__arid_out['h2];
        assign mem2__axi_in__rready_in = tribe__axi_out__rready_out['h2];
        assign iospace__slave_in__awvalid_in = tribe__axi_out__awvalid_out['h3];
        assign iospace__slave_in__awaddr_in = tribe__axi_out__awaddr_out['h3];
        assign iospace__slave_in__awid_in = tribe__axi_out__awid_out['h3];
        assign iospace__slave_in__wvalid_in = tribe__axi_out__wvalid_out['h3];
        assign iospace__slave_in__wdata_in = tribe__axi_out__wdata_out['h3];
        assign iospace__slave_in__wlast_in = tribe__axi_out__wlast_out['h3];
        assign iospace__slave_in__bready_in = tribe__axi_out__bready_out['h3];
        assign iospace__slave_in__arvalid_in = tribe__axi_out__arvalid_out['h3];
        assign iospace__slave_in__araddr_in = tribe__axi_out__araddr_out['h3];
        assign iospace__slave_in__arid_in = tribe__axi_out__arid_out['h3];
        assign iospace__slave_in__rready_in = tribe__axi_out__rready_out['h3];
        assign accelerator__dma_out__awready_in = tribe__axi_in__awready_out['h0];
        assign accelerator__dma_out__wready_in = tribe__axi_in__wready_out['h0];
        assign accelerator__dma_out__bvalid_in = tribe__axi_in__bvalid_out['h0];
        assign accelerator__dma_out__bid_in = tribe__axi_in__bid_out['h0];
        assign accelerator__dma_out__arready_in = tribe__axi_in__arready_out['h0];
        assign accelerator__dma_out__rvalid_in = tribe__axi_in__rvalid_out['h0];
        assign accelerator__dma_out__rdata_in = tribe__axi_in__rdata_out['h0];
        assign accelerator__dma_out__rlast_in = tribe__axi_in__rlast_out['h0];
        assign accelerator__dma_out__rid_in = tribe__axi_in__rid_out['h0];
        assign mem2__debugen_in=debugen_in;
        assign tribe__axi_out__awready_in['h2] = mem2__axi_in__awready_out;
        assign tribe__axi_out__wready_in['h2] = mem2__axi_in__wready_out;
        assign tribe__axi_out__bvalid_in['h2] = mem2__axi_in__bvalid_out;
        assign tribe__axi_out__bid_in['h2] = mem2__axi_in__bid_out;
        assign tribe__axi_out__arready_in['h2] = mem2__axi_in__arready_out;
        assign tribe__axi_out__rvalid_in['h2] = mem2__axi_in__rvalid_out;
        assign tribe__axi_out__rdata_in['h2] = mem2__axi_in__rdata_out;
        assign tribe__axi_out__rlast_in['h2] = mem2__axi_in__rlast_out;
        assign tribe__axi_out__rid_in['h2] = mem2__axi_in__rid_out;
        assign iospace__region_base_in['h0] = unsigned'(32'('h0));
        assign iospace__region_size_in['h0] = unsigned'(32'('h100));
        assign iospace__region_base_in['h1] = unsigned'(32'('h100));
        assign iospace__region_size_in['h1] = unsigned'(32'('hC000));
        assign iospace__region_base_in['h2] = unsigned'(32'('hC100));
        assign iospace__region_size_in['h2] = unsigned'(32'('h1000));
        assign iospace__region_base_in['h3] = unsigned'(32'('h10000));
        assign iospace__region_size_in['h3] = unsigned'(32'('h210000));
        assign uart__axi_in__awvalid_in = iospace__masters_out__awvalid_out['h0];
        assign uart__axi_in__awaddr_in = iospace__masters_out__awaddr_out['h0];
        assign uart__axi_in__awid_in = iospace__masters_out__awid_out['h0];
        assign uart__axi_in__wvalid_in = iospace__masters_out__wvalid_out['h0];
        assign uart__axi_in__wdata_in = iospace__masters_out__wdata_out['h0];
        assign uart__axi_in__wlast_in = iospace__masters_out__wlast_out['h0];
        assign uart__axi_in__bready_in = iospace__masters_out__bready_out['h0];
        assign uart__axi_in__arvalid_in = iospace__masters_out__arvalid_out['h0];
        assign uart__axi_in__araddr_in = iospace__masters_out__araddr_out['h0];
        assign uart__axi_in__arid_in = iospace__masters_out__arid_out['h0];
        assign uart__axi_in__rready_in = iospace__masters_out__rready_out['h0];
        assign clint__axi_in__awvalid_in = iospace__masters_out__awvalid_out['h1];
        assign clint__axi_in__awaddr_in = iospace__masters_out__awaddr_out['h1];
        assign clint__axi_in__awid_in = iospace__masters_out__awid_out['h1];
        assign clint__axi_in__wvalid_in = iospace__masters_out__wvalid_out['h1];
        assign clint__axi_in__wdata_in = iospace__masters_out__wdata_out['h1];
        assign clint__axi_in__wlast_in = iospace__masters_out__wlast_out['h1];
        assign clint__axi_in__bready_in = iospace__masters_out__bready_out['h1];
        assign clint__axi_in__arvalid_in = iospace__masters_out__arvalid_out['h1];
        assign clint__axi_in__araddr_in = iospace__masters_out__araddr_out['h1];
        assign clint__axi_in__arid_in = iospace__masters_out__arid_out['h1];
        assign clint__axi_in__rready_in = iospace__masters_out__rready_out['h1];
        assign accelerator__axi_in__awvalid_in = iospace__masters_out__awvalid_out['h2];
        assign accelerator__axi_in__awaddr_in = iospace__masters_out__awaddr_out['h2];
        assign accelerator__axi_in__awid_in = iospace__masters_out__awid_out['h2];
        assign accelerator__axi_in__wvalid_in = iospace__masters_out__wvalid_out['h2];
        assign accelerator__axi_in__wdata_in = iospace__masters_out__wdata_out['h2];
        assign accelerator__axi_in__wlast_in = iospace__masters_out__wlast_out['h2];
        assign accelerator__axi_in__bready_in = iospace__masters_out__bready_out['h2];
        assign accelerator__axi_in__arvalid_in = iospace__masters_out__arvalid_out['h2];
        assign accelerator__axi_in__araddr_in = iospace__masters_out__araddr_out['h2];
        assign accelerator__axi_in__arid_in = iospace__masters_out__arid_out['h2];
        assign accelerator__axi_in__rready_in = iospace__masters_out__rready_out['h2];
        assign plic__axi_in__awvalid_in = iospace__masters_out__awvalid_out['h3];
        assign plic__axi_in__awaddr_in = iospace__masters_out__awaddr_out['h3];
        assign plic__axi_in__awid_in = iospace__masters_out__awid_out['h3];
        assign plic__axi_in__wvalid_in = iospace__masters_out__wvalid_out['h3];
        assign plic__axi_in__wdata_in = iospace__masters_out__wdata_out['h3];
        assign plic__axi_in__wlast_in = iospace__masters_out__wlast_out['h3];
        assign plic__axi_in__bready_in = iospace__masters_out__bready_out['h3];
        assign plic__axi_in__arvalid_in = iospace__masters_out__arvalid_out['h3];
        assign plic__axi_in__araddr_in = iospace__masters_out__araddr_out['h3];
        assign plic__axi_in__arid_in = iospace__masters_out__arid_out['h3];
        assign plic__axi_in__rready_in = iospace__masters_out__rready_out['h3];
        assign uart__uart_rx_valid_in = uart_rx_valid_in;
        assign uart__uart_rx_data_in = uart_rx_data_in;
        assign clint__set_mtimecmp_in = tribe__sbi_set_timer_out;
        assign clint__set_mtimecmp_lo_in = tribe__sbi_timer_lo_out;
        assign clint__set_mtimecmp_hi_in = tribe__sbi_timer_hi_out;
        for (gi='h0;gi < 'h20;gi=gi+1) begin
            assign plic__source_irq_in[gi] = 0;
        end
        assign plic__source_irq_in['h1] = uart__irq_out;
        assign iospace__masters_out__awready_in['h0] = uart__axi_in__awready_out;
        assign iospace__masters_out__wready_in['h0] = uart__axi_in__wready_out;
        assign iospace__masters_out__bvalid_in['h0] = uart__axi_in__bvalid_out;
        assign iospace__masters_out__bid_in['h0] = uart__axi_in__bid_out;
        assign iospace__masters_out__arready_in['h0] = uart__axi_in__arready_out;
        assign iospace__masters_out__rvalid_in['h0] = uart__axi_in__rvalid_out;
        assign iospace__masters_out__rdata_in['h0] = uart__axi_in__rdata_out;
        assign iospace__masters_out__rlast_in['h0] = uart__axi_in__rlast_out;
        assign iospace__masters_out__rid_in['h0] = uart__axi_in__rid_out;
        assign iospace__masters_out__awready_in['h1] = clint__axi_in__awready_out;
        assign iospace__masters_out__wready_in['h1] = clint__axi_in__wready_out;
        assign iospace__masters_out__bvalid_in['h1] = clint__axi_in__bvalid_out;
        assign iospace__masters_out__bid_in['h1] = clint__axi_in__bid_out;
        assign iospace__masters_out__arready_in['h1] = clint__axi_in__arready_out;
        assign iospace__masters_out__rvalid_in['h1] = clint__axi_in__rvalid_out;
        assign iospace__masters_out__rdata_in['h1] = clint__axi_in__rdata_out;
        assign iospace__masters_out__rlast_in['h1] = clint__axi_in__rlast_out;
        assign iospace__masters_out__rid_in['h1] = clint__axi_in__rid_out;
        assign iospace__masters_out__awready_in['h2] = accelerator__axi_in__awready_out;
        assign iospace__masters_out__wready_in['h2] = accelerator__axi_in__wready_out;
        assign iospace__masters_out__bvalid_in['h2] = accelerator__axi_in__bvalid_out;
        assign iospace__masters_out__bid_in['h2] = accelerator__axi_in__bid_out;
        assign iospace__masters_out__arready_in['h2] = accelerator__axi_in__arready_out;
        assign iospace__masters_out__rvalid_in['h2] = accelerator__axi_in__rvalid_out;
        assign iospace__masters_out__rdata_in['h2] = accelerator__axi_in__rdata_out;
        assign iospace__masters_out__rlast_in['h2] = accelerator__axi_in__rlast_out;
        assign iospace__masters_out__rid_in['h2] = accelerator__axi_in__rid_out;
        assign iospace__masters_out__awready_in['h3] = plic__axi_in__awready_out;
        assign iospace__masters_out__wready_in['h3] = plic__axi_in__wready_out;
        assign iospace__masters_out__bvalid_in['h3] = plic__axi_in__bvalid_out;
        assign iospace__masters_out__bid_in['h3] = plic__axi_in__bid_out;
        assign iospace__masters_out__arready_in['h3] = plic__axi_in__arready_out;
        assign iospace__masters_out__rvalid_in['h3] = plic__axi_in__rvalid_out;
        assign iospace__masters_out__rdata_in['h3] = plic__axi_in__rdata_out;
        assign iospace__masters_out__rlast_in['h3] = plic__axi_in__rlast_out;
        assign iospace__masters_out__rid_in['h3] = plic__axi_in__rid_out;
        assign tribe__axi_out__awready_in['h0] = axi_out__awready_in['h0];
        assign tribe__axi_out__wready_in['h0] = axi_out__wready_in['h0];
        assign tribe__axi_out__bvalid_in['h0] = axi_out__bvalid_in['h0];
        assign tribe__axi_out__bid_in['h0] = unsigned'(4'(axi_out__bid_in['h0]));
        assign tribe__axi_out__arready_in['h0] = axi_out__arready_in['h0];
        assign tribe__axi_out__rvalid_in['h0] = axi_out__rvalid_in['h0];
        assign tribe__axi_out__rdata_in['h0] = axi_out__rdata_in['h0];
        assign tribe__axi_out__rlast_in['h0] = axi_out__rlast_in['h0];
        assign tribe__axi_out__rid_in['h0] = unsigned'(4'(axi_out__rid_in['h0]));
        assign tribe__axi_out__awready_in['h1] = axi_out__awready_in['h1];
        assign tribe__axi_out__wready_in['h1] = axi_out__wready_in['h1];
        assign tribe__axi_out__bvalid_in['h1] = axi_out__bvalid_in['h1];
        assign tribe__axi_out__bid_in['h1] = unsigned'(4'(axi_out__bid_in['h1]));
        assign tribe__axi_out__arready_in['h1] = axi_out__arready_in['h1];
        assign tribe__axi_out__rvalid_in['h1] = axi_out__rvalid_in['h1];
        assign tribe__axi_out__rdata_in['h1] = axi_out__rdata_in['h1];
        assign tribe__axi_out__rlast_in['h1] = axi_out__rlast_in['h1];
        assign tribe__axi_out__rid_in['h1] = unsigned'(4'(axi_out__rid_in['h1]));
        assign tribe__axi_out__awready_in['h3] = iospace__slave_in__awready_out;
        assign tribe__axi_out__wready_in['h3] = iospace__slave_in__wready_out;
        assign tribe__axi_out__bvalid_in['h3] = iospace__slave_in__bvalid_out;
        assign tribe__axi_out__bid_in['h3] = iospace__slave_in__bid_out;
        assign tribe__axi_out__arready_in['h3] = iospace__slave_in__arready_out;
        assign tribe__axi_out__rvalid_in['h3] = iospace__slave_in__rvalid_out;
        assign tribe__axi_out__rdata_in['h3] = iospace__slave_in__rdata_out;
        assign tribe__axi_out__rlast_in['h3] = iospace__slave_in__rlast_out;
        assign tribe__axi_out__rid_in['h3] = iospace__slave_in__rid_out;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    task _work_neg (input logic reset);
    begin: _work_neg
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    always @(negedge clk) begin
        _work_neg(reset);
    end

    assign uart_rx_ready_out = uart__uart_rx_ready_out;

    assign uart_tx_valid_out = uart__uart_valid_out;

    assign uart_tx_data_out = uart__uart_data_out;

    assign perf_out = tribe__perf_out;

    assign dmem_write_out = tribe__dmem_write_out;

    assign dmem_write_data_out = tribe__dmem_write_data_out;

    assign dmem_write_mask_out = tribe__dmem_write_mask_out;

    assign dmem_read_out = tribe__dmem_read_out;

    assign dmem_addr_out = tribe__dmem_addr_out;

    assign imem_read_addr_out = tribe__imem_read_addr_out;

    assign debug_immu_ptw_read_out = tribe__debug_immu_ptw_read_out;

    assign debug_immu_ptw_addr_out = tribe__debug_immu_ptw_addr_out;

    assign debug_immu_busy_out = tribe__debug_immu_busy_out;

    assign debug_immu_fault_out = tribe__debug_immu_fault_out;

    assign debug_immu_paddr_out = tribe__debug_immu_paddr_out;

    assign debug_immu_last_addr_out = tribe__debug_immu_last_addr_out;

    assign debug_immu_last_pte_out = tribe__debug_immu_last_pte_out;

    assign debug_icache_read_valid_out = tribe__debug_icache_read_valid_out;

    assign debug_icache_read_addr_out = tribe__debug_icache_read_addr_out;

    assign debug_fetch_valid_out = tribe__debug_fetch_valid_out;

    assign debug_memory_wait_out = tribe__debug_memory_wait_out;

    assign debug_wb_load_ready_out = tribe__debug_wb_load_ready_out;

    assign debug_wb_mem_wait_out = tribe__debug_wb_mem_wait_out;

    assign debug_icache_read_in_out = tribe__debug_icache_read_in_out;

    assign debug_icache_stall_in_out = tribe__debug_icache_stall_in_out;

    assign debug_dmmu_ptw_read_out = tribe__debug_dmmu_ptw_read_out;

    assign debug_dmmu_ptw_addr_out = tribe__debug_dmmu_ptw_addr_out;

    assign debug_dmmu_busy_out = tribe__debug_dmmu_busy_out;

    assign debug_dmmu_fault_out = tribe__debug_dmmu_fault_out;

    assign debug_mmu_ptw_word_out = tribe__debug_mmu_ptw_word_out;

    assign debug_pc_out = tribe__debug_pc_out;

    assign debug_satp_out = tribe__debug_satp_out;

    assign debug_mstatus_out = tribe__debug_mstatus_out;

    assign debug_mtvec_out = tribe__debug_mtvec_out;

    assign debug_mepc_out = tribe__debug_mepc_out;

    assign debug_mcause_out = tribe__debug_mcause_out;

    assign debug_mtval_out = tribe__debug_mtval_out;

    assign debug_sepc_out = tribe__debug_sepc_out;

    assign debug_stvec_out = tribe__debug_stvec_out;

    assign debug_scause_out = tribe__debug_scause_out;

    assign debug_stval_out = tribe__debug_stval_out;

    assign debug_priv_out = tribe__debug_priv_out;

    assign debug_ra_out = tribe__debug_ra_out;

    assign debug_regs_write_out = tribe__debug_regs_write_out;

    assign debug_regs_write_actual_out = tribe__debug_regs_write_actual_out;

    assign debug_regs_wr_id_out = tribe__debug_regs_wr_id_out;

    assign debug_regs_data_out = tribe__debug_regs_data_out;

    assign debug_branch_taken_now_out = tribe__debug_branch_taken_now_out;

    assign debug_branch_target_now_out = tribe__debug_branch_target_now_out;

    assign debug_decode_instr_out = tribe__debug_decode_instr_out;

    assign debug_decode_pc_out = tribe__debug_decode_pc_out;

    assign debug_decode_br_out = tribe__debug_decode_br_out;

    assign debug_decode_imm_out = tribe__debug_decode_imm_out;


endmodule
