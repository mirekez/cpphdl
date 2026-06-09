#pragma once

#include "File.h"

#include "Config.h"

static constexpr size_t STAGES_NUM = 3;  // Decode, Execute, Writeback  (+ IFetch not counted here)
static constexpr size_t CACHE_LINE_SIZE = 32;
static constexpr size_t ADDR_BITS = 32;

#ifdef L2_AXI_WIDTH
static constexpr size_t TRIBE_L2_AXI_WIDTH = L2_AXI_WIDTH;  // 64, 128, 256 has special targets in Makefile
#if L2_AXI_WIDTH == 64
#define TRIBE_L2_AXI_WIDTH_IS_64
#elif L2_AXI_WIDTH == 128
#define TRIBE_L2_AXI_WIDTH_IS_128
#else
#define TRIBE_L2_AXI_WIDTH_IS_256
#endif
#undef L2_AXI_WIDTH
#else
static constexpr size_t TRIBE_L2_AXI_WIDTH = 256;  // default
#define TRIBE_L2_AXI_WIDTH_IS_256
#endif

#include "Decode.h"
#include "Execute.h"
#include "ExecuteMem.h"
#include "Writeback.h"
#include "WritebackMem.h"
#include "CSR.h"
#include "MMU_TLB.h"
#include "InterruptController.h"
#include "BranchPredictor.h"
#include "Axi4.h"
#include "Axi4RegionMux.h"
#include "devices/IOUART.h"
#include "devices/NS16550A.h"
#include "devices/CLINT.h"
#include "devices/PLIC.h"
#include "devices/Accelerator.h"
#include "devices/sd/SDController.h"
#ifndef SYNTHESIS
#include "verif/SDCardVerif.h"
#endif
#include "cache/L1Cache.h"
#include "cache/L2Cache.h"

#include <cstdlib>
#include <csignal>
#include <vector>
#include <deque>
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

// system configuration for cpp
static constexpr size_t DEFAULT_RAM_SIZE = 32768;
#ifndef TRIBE_RAM_BYTES_CONFIG
#define TRIBE_RAM_BYTES_CONFIG (448 * 1024)
#endif
#ifndef TRIBE_IO_REGION_SIZE_CONFIG
#define TRIBE_IO_REGION_SIZE_CONFIG (4 * 1024 * 1024)
#endif
static constexpr size_t TRIBE_RAM_BYTES = TRIBE_RAM_BYTES_CONFIG;
static constexpr size_t TRIBE_IO_ADDRESS_SPACE_SIZE = 0x220000;
static constexpr size_t TRIBE_IO_REGION_SIZE =
    TRIBE_IO_REGION_SIZE_CONFIG < TRIBE_IO_ADDRESS_SPACE_SIZE ? TRIBE_IO_ADDRESS_SPACE_SIZE : TRIBE_IO_REGION_SIZE_CONFIG;
static constexpr size_t MAX_RAM_SIZE = TRIBE_RAM_BYTES + TRIBE_IO_REGION_SIZE;
static constexpr size_t TRIBE_MEM_REGION0_SIZE = TRIBE_RAM_BYTES / 2;
static constexpr size_t TRIBE_MEM_REGION1_SIZE = TRIBE_RAM_BYTES / 4;
static constexpr size_t TRIBE_MEM_REGION2_SIZE = TRIBE_RAM_BYTES - TRIBE_MEM_REGION0_SIZE - TRIBE_MEM_REGION1_SIZE;
#define L2_MEM_PORTS 4

#define L2_CACHE_ADDR_BITS cpphdl::clog2(MAX_RAM_SIZE)

long _system_clock = -1;

struct TribePerf
{
    unsigned hazard_stall:1;
    unsigned branch_stall:1;
    unsigned dcache_wait:1;
    unsigned icache_wait:1;
    L1CachePerf icache;
    L1CachePerf dcache;
} __PACKED;

class Tribe: public Module
{
    Decode          dec;
    Execute         exe;
    ExecuteMem      exe_mem;
    Writeback       wb;
    WritebackMem    wb_mem;
#ifdef ENABLE_ZICSR
    CSR             csr;
#endif
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
    InterruptController irq;
#endif
#ifdef ENABLE_MMU_TLB
    MMU_TLB<8>      immu;
    MMU_TLB<8>      dmmu;
#endif
    File<32,32>     regs;
    L1Cache<L1_CACHE_SIZE,CACHE_LINE_SIZE,L1_CACHE_ASSOCIATIONS,0,ADDR_BITS,TRIBE_L2_AXI_WIDTH> icache;
    L1Cache<L1_CACHE_SIZE,CACHE_LINE_SIZE,L1_CACHE_ASSOCIATIONS,1,ADDR_BITS,TRIBE_L2_AXI_WIDTH> dcache;
    L2Cache<L2_CACHE_SIZE,TRIBE_L2_AXI_WIDTH,CACHE_LINE_SIZE,L2_CACHE_ASSOCIATIONS,ADDR_BITS,clog2(MAX_RAM_SIZE),L2_MEM_PORTS> l2cache;
    BranchPredictor<BRANCH_PREDICTOR_ENTRIES, BRANCH_PREDICTOR_COUNTER_BITS> bp;
    reg<u1> icache_invalidate_issued_reg;

public:

    _PORT(bool)      dmem_write_out;
    _PORT(uint32_t)  dmem_write_data_out;
    _PORT(uint8_t)   dmem_write_mask_out;
    _PORT(bool)      dmem_read_out;
    _PORT(uint32_t)  dmem_addr_out;
    _PORT(uint32_t)  imem_read_addr_out;
#ifdef ENABLE_MMU_TLB
    _PORT(bool)      debug_immu_ptw_read_out;
    _PORT(uint32_t)  debug_immu_ptw_addr_out;
    _PORT(bool)      debug_immu_busy_out;
    _PORT(bool)      debug_immu_fault_out;
    _PORT(uint32_t)  debug_immu_paddr_out;
    _PORT(uint32_t)  debug_immu_last_addr_out;
    _PORT(uint32_t)  debug_immu_last_pte_out;
    _PORT(bool)      debug_icache_read_valid_out;
    _PORT(uint32_t)  debug_icache_read_addr_out;
    _PORT(bool)      debug_dcache_read_valid_out;
    _PORT(uint32_t)  debug_dcache_read_addr_out;
    _PORT(uint32_t)  debug_dcache_read_data_out;
    _PORT(bool)      debug_dcache_cpu_read_out;
    _PORT(bool)      debug_dcache_cpu_write_out;
    _PORT(uint32_t)  debug_dcache_cpu_addr_out;
    _PORT(uint32_t)  debug_dcache_cpu_wdata_out;
    _PORT(uint8_t)   debug_dcache_cpu_wmask_out;
    _PORT(bool)      debug_fetch_valid_out;
    _PORT(bool)      debug_memory_wait_out;
    _PORT(bool)      debug_wb_load_ready_out;
    _PORT(bool)      debug_wb_mem_wait_out;
    _PORT(bool)      debug_wb_load_data_valid_out;
    _PORT(uint32_t)  debug_wb_load_addr_out;
    _PORT(bool)      debug_wb_split_low_valid_out;
    _PORT(bool)      debug_wb_split_high_valid_out;
    _PORT(bool)      debug_wb_held_load_valid_out;
    _PORT(bool)      debug_wb_split_load_in_out;
    _PORT(uint32_t)  debug_wb_alu_addr_out;
    _PORT(uint32_t)  debug_wb_state_pc_out;
    _PORT(uint8_t)   debug_wb_state_wb_op_out;
    _PORT(uint8_t)   debug_wb_state_mem_op_out;
    _PORT(uint8_t)   debug_wb_state_rd_out;
    _PORT(uint8_t)   debug_wb_state_funct3_out;
    _PORT(bool)      debug_icache_read_in_out;
    _PORT(bool)      debug_icache_stall_in_out;
    _PORT(bool)      debug_dmmu_ptw_read_out;
    _PORT(uint32_t)  debug_dmmu_ptw_addr_out;
    _PORT(bool)      debug_dmmu_busy_out;
    _PORT(bool)      debug_dmmu_fault_out;
    _PORT(uint32_t)  debug_mmu_ptw_word_out;
    _PORT(uint32_t)  debug_pc_out;
    _PORT(uint32_t)  debug_satp_out;
    _PORT(uint32_t)  debug_mstatus_out;
    _PORT(uint32_t)  debug_mtvec_out;
    _PORT(uint32_t)  debug_mepc_out;
    _PORT(uint32_t)  debug_mcause_out;
    _PORT(uint32_t)  debug_mtval_out;
    _PORT(uint32_t)  debug_sepc_out;
    _PORT(uint32_t)  debug_stvec_out;
    _PORT(uint32_t)  debug_scause_out;
    _PORT(uint32_t)  debug_stval_out;
    _PORT(bool)      debug_irq_valid_out;
    _PORT(uint32_t)  debug_irq_cause_out;
    _PORT(bool)      debug_irq_to_supervisor_out;
    _PORT(uint32_t)  debug_irq_mip_out;
    _PORT(uint32_t)  debug_irq_mie_out;
    _PORT(uint32_t)  debug_irq_mideleg_out;
    _PORT(u<2>)      debug_priv_out;
    _PORT(uint32_t)  debug_ra_out;
    _PORT(bool)      debug_regs_write_out;
    _PORT(bool)      debug_regs_write_actual_out;
    _PORT(uint8_t)   debug_regs_wr_id_out;
    _PORT(uint32_t)  debug_regs_data_out;
    _PORT(bool)      debug_branch_taken_now_out;
    _PORT(uint32_t)  debug_branch_target_now_out;
    _PORT(uint32_t)  debug_decode_instr_out;
    _PORT(uint32_t)  debug_decode_pc_out;
    _PORT(uint8_t)   debug_decode_br_out;
    _PORT(uint32_t)  debug_decode_imm_out;
#endif
    _PORT(bool)      sbi_set_timer_out = _ASSIGN_COMB(sbi_set_timer_comb_func());
    _PORT(uint32_t)  sbi_timer_lo_out = _ASSIGN_COMB(sbi_timer_lo_comb_func());
    _PORT(uint32_t)  sbi_timer_hi_out = _ASSIGN_COMB(sbi_timer_hi_comb_func());
    _PORT(bool)      debug_sbi_ecall_out = _ASSIGN_COMB(sbi_ecall_debug_comb_func());
    _PORT(uint32_t)  debug_sbi_a7_out = _ASSIGN_COMB(sbi_a7_debug_comb_func());
    _PORT(uint32_t)  debug_sbi_a6_out = _ASSIGN_COMB(sbi_a6_debug_comb_func());
    _PORT(uint32_t)  debug_sbi_a0_out = _ASSIGN_COMB(sbi_a0_debug_comb_func());
    _PORT(bool)      debug_sbi_base_out = _ASSIGN_COMB(sbi_base_comb_func());
    _PORT(bool)      debug_sbi_noop_out = _ASSIGN_COMB(sbi_noop_comb_func());
    _PORT(bool)      debug_sbi_handled_out = _ASSIGN_COMB(sbi_handled_comb_func());
    _PORT(uint32_t)  reset_pc_in;
    _PORT(uint32_t)  boot_hartid_in;
    _PORT(uint32_t)  boot_dtb_addr_in;
    _PORT(u<2>)      boot_priv_in;
    _PORT(bool)      external_cache_invalidate_in;
    _PORT(uint32_t)  memory_base_in;
    _PORT(uint32_t)  memory_size_in;
    _PORT(uint32_t)  mem_region_size_in[L2_MEM_PORTS];
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
    _PORT(bool)      clint_msip_in;
    _PORT(bool)      clint_mtip_in;
    _PORT(uint32_t)  time_lo_in = _ASSIGN((uint32_t)0);
    _PORT(uint32_t)  time_hi_in = _ASSIGN((uint32_t)0);
    _PORT(bool)      external_irq_in = _ASSIGN(false);
#endif

    Axi4If<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> axi_in[L2_MEM_PORTS];
    Axi4If<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> axi_out[L2_MEM_PORTS];

    _PORT(TribePerf) perf_out = _ASSIGN_COMB(perf_comb_func());
    bool              debugen_in;

    void _assign()
    {
        size_t i;
//        dec.state_in       = _ASSIGN_REG( state_reg[0] );  // execute stage input is same
        dec.pc_in          = _ASSIGN_REG( pc );
        dec.instr_valid_in = _ASSIGN(fetch_valid_comb_func());
        dec.instr_in       = icache.read_data_out;
        dec.regs_data0_in  = _ASSIGN( dec.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        dec.regs_data1_in  = _ASSIGN( dec.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        dec._assign();  // outputs are ready

        exe.state_in       = _ASSIGN_COMB( exe_state_comb_func() );
        exe._assign();  // outputs are ready

        exe_mem.state_in = _ASSIGN_COMB(exe_state_comb_func());
        exe_mem.alu_result_in = exe.alu_result_out;
#ifdef ENABLE_RV32IA
        exe_mem.dcache_read_valid_in = dcache.read_valid_out;
        exe_mem.dcache_read_addr_in = dcache.read_addr_out;
#ifdef ENABLE_MMU_TLB
        // AMO read responses are tagged by the physical D-cache address; match
        // them against the translated MMU address, not the architectural VA.
        exe_mem.dcache_read_expected_addr_in = dmmu.paddr_out;
#else
        exe_mem.dcache_read_expected_addr_in = exe_mem.mem_read_addr_out;
#endif
        exe_mem.dcache_read_data_in = dcache.read_data_out;
#endif
        exe_mem.mem_stall_in = dcache.busy_out;
        exe_mem.hold_in = _ASSIGN( memory_wait_comb_func() );
        exe_mem.__inst_name = __inst_name + "/exe_mem";
        exe_mem._assign();

        wb_mem.state_in = _ASSIGN_REG(state_reg[1]);
        wb_mem.alu_result_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN(dcache.read_valid_out() ? (uint32_t)dcache.read_addr_out() :
                ((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM) ? (uint32_t)dmmu.paddr_out() : (uint32_t)alu_result_reg));
#else
            _ASSIGN_REG(alu_result_reg);
#endif
        wb_mem.split_load_in = exe_mem.split_load_out;
        wb_mem.split_load_low_addr_in = exe_mem.split_load_low_out;
        wb_mem.split_load_high_addr_in = exe_mem.split_load_high_out;
        wb_mem.dcache_read_valid_in = dcache.read_valid_out;
        wb_mem.dcache_read_addr_in = dcache.read_addr_out;
        wb_mem.dcache_read_data_in = dcache.read_data_out;
        wb_mem.dcache_write_valid_in = dcache.mem_write_out;
        wb_mem.dcache_write_addr_in = dcache.mem_addr_out;
        wb_mem.dcache_write_data_in = dcache.mem_write_data_out;
        wb_mem.dcache_write_mask_in = dcache.mem_write_mask_out;
        wb_mem.store_forward_enable_in = _ASSIGN(
            (uint32_t)wb_mem.alu_result_in() < memory_base_in() + mem_region_size_in[0]() + mem_region_size_in[1]() + mem_region_size_in[2]());
        wb_mem.hold_in = _ASSIGN(memory_wait_comb_func());
        wb_mem.__inst_name = __inst_name + "/wb_mem";
        wb_mem._assign();

#ifdef ENABLE_ZICSR
#ifdef ENABLE_ISR
        irq.mstatus_in = csr.mstatus_out;
        irq.mie_in = csr.mie_out;
        irq.mideleg_in = csr.mideleg_out;
        irq.mip_sw_in = csr.mip_sw_out;
        irq.priv_in = csr.priv_out;
        irq.clint_msip_in = clint_msip_in;
        irq.clint_mtip_in = clint_mtip_in;
        irq.external_irq_in = external_irq_in;
        irq.__inst_name = __inst_name + "/irq";
        irq._assign();
#endif
        csr.state_in       = _ASSIGN_COMB( csr_state_comb_func() );
        csr.trap_check_state_in = _ASSIGN_REG(state_reg[0]);
        csr.reset_priv_in = boot_priv_in;
        csr.time_lo_in = time_lo_in;
        csr.time_hi_in = time_hi_in;
#ifdef ENABLE_ISR
        csr.interrupt_valid_in = _ASSIGN_COMB(interrupt_accept_comb_func());
        csr.interrupt_cause_in = irq.interrupt_cause_out;
        csr.interrupt_to_supervisor_in = irq.interrupt_to_supervisor_out;
        csr.irq_pending_bits_in = irq.mip_out;
#else
        csr.interrupt_valid_in = _ASSIGN(false);
        csr.interrupt_cause_in = _ASSIGN((uint32_t)0);
        csr.interrupt_to_supervisor_in = _ASSIGN(false);
        csr.irq_pending_bits_in = _ASSIGN((uint32_t)0);
#endif
        csr.__inst_name = __inst_name + "/csr";
        csr._assign();
#endif

#ifdef ENABLE_MMU_TLB
        immu.vaddr_in = _ASSIGN(fetch_addr_comb_func());
        immu.read_in = _ASSIGN(false);
        immu.write_in = _ASSIGN(false);
        // Only a live front-end fetch may request instruction translation.
        // During redirects and bubbles fetch_addr_comb can be a placeholder; translating
        // it would create a spurious instruction page fault.
        immu.execute_in = _ASSIGN((bool)valid);
#ifdef ENABLE_ZICSR
        immu.satp_in = csr.satp_out;
        immu.priv_in = csr.priv_out;
        immu.sum_in = _ASSIGN(false);
        immu.mxr_in = _ASSIGN(false);
#else
        immu.satp_in = _ASSIGN((uint32_t)0);
        immu.priv_in = _ASSIGN((u<2>)3);
        immu.sum_in = _ASSIGN(false);
        immu.mxr_in = _ASSIGN(false);
#endif
        // Instruction fetches are fully translated; only data/MMIO uses the direct window.
        immu.direct_base_in = _ASSIGN((uint32_t)0);
        immu.direct_size_in = _ASSIGN((uint32_t)0);
        immu.fill_in = _ASSIGN(false);
        immu.fill_index_in = _ASSIGN((u<3>)0);
        immu.fill_vpn_in = _ASSIGN((uint32_t)0);
        immu.fill_ppn_in = _ASSIGN((uint32_t)0);
        immu.fill_flags_in = _ASSIGN((uint8_t)0);
        immu.sfence_in = _ASSIGN(sfence_vma_comb_func());
        immu.mem_read_data_in = _ASSIGN_COMB(mmu_l2_read_word_comb_func());
        immu.mem_wait_in = _ASSIGN(!immu_ptw_selected_comb_func() || l2cache.d_wait_out());
        immu.__inst_name = __inst_name + "/immu";
        immu._assign();

        dmmu.vaddr_in = _ASSIGN(exe_mem.mem_read_out() ? (uint32_t)exe_mem.mem_read_addr_out() : (uint32_t)exe_mem.mem_write_addr_out());
        // ExecuteMem registers can still hold a previous request for one cycle
        // after a trap or SRET flush; only a valid memory-stage instruction may
        // drive translation.
        dmmu.read_in = _ASSIGN(state_reg[1].valid && exe_mem.mem_read_out());
        dmmu.write_in = _ASSIGN(state_reg[1].valid && exe_mem.mem_write_out());
        dmmu.execute_in = _ASSIGN(false);
#ifdef ENABLE_ZICSR
        dmmu.satp_in = csr.satp_out;
        dmmu.priv_in = csr.priv_out;
        dmmu.sum_in = _ASSIGN(((uint32_t)csr.mstatus_out() & (1u << 18)) != 0);
        dmmu.mxr_in = _ASSIGN(((uint32_t)csr.mstatus_out() & (1u << 19)) != 0);
#else
        dmmu.satp_in = _ASSIGN((uint32_t)0);
        dmmu.priv_in = _ASSIGN((u<2>)3);
        dmmu.sum_in = _ASSIGN(false);
        dmmu.mxr_in = _ASSIGN(false);
#endif
        // Data MMU bypasses translation for the IO region so MMIO stays physical under Linux.
        dmmu.direct_base_in = _ASSIGN(memory_base_in() + mem_region_size_in[0]() + mem_region_size_in[1]() + mem_region_size_in[2]());
        dmmu.direct_size_in = mem_region_size_in[3];
        dmmu.fill_in = _ASSIGN(false);
        dmmu.fill_index_in = _ASSIGN((u<3>)0);
        dmmu.fill_vpn_in = _ASSIGN((uint32_t)0);
        dmmu.fill_ppn_in = _ASSIGN((uint32_t)0);
        dmmu.fill_flags_in = _ASSIGN((uint8_t)0);
        dmmu.sfence_in = _ASSIGN(sfence_vma_comb_func());
        dmmu.mem_read_data_in = _ASSIGN_COMB(mmu_l2_read_word_comb_func());
        dmmu.mem_wait_in = _ASSIGN(!dmmu_ptw_selected_comb_func() || l2cache.d_wait_out());
        dmmu.__inst_name = __inst_name + "/dmmu";
        dmmu._assign();
#endif

        wb.state_in       = _ASSIGN_REG( state_reg[1] );
        wb.mem_data_in    = wb_mem.load_raw_out;
        wb.mem_data_hi_in = _ASSIGN((uint32_t)0);
        wb.mem_addr_in    =
#ifdef ENABLE_MMU_TLB
            _ASSIGN((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM) ? (uint32_t)dmmu.paddr_out() : (uint32_t)alu_result_reg);
#else
            _ASSIGN_REG(alu_result_reg);
#endif
        wb.mem_split_in   = _ASSIGN(false);
        wb.alu_result_in  = _ASSIGN_REG( alu_result_reg );
        wb._assign();  // outputs are ready

        regs.read_addr0_in = _ASSIGN( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = _ASSIGN( (uint8_t)dec.rs2_out() );
        regs.write_in = _ASSIGN(wb.regs_write_out() &&
            !memory_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || wb_mem.load_ready_out()));
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.write2_in = _ASSIGN((bool)sbi_ret_a1_valid_reg);
        regs.write2_addr_in = _ASSIGN((uint8_t)11);
        regs.write2_data_in = _ASSIGN((uint32_t)sbi_ret_a1_reg);
        regs.reset_x10_in = boot_hartid_in;
        regs.reset_x11_in = boot_dtb_addr_in;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = _ASSIGN( state_reg[1].valid && exe_mem.mem_read_out() && !dcache.busy_out()
#ifdef ENABLE_MMU_TLB
            && !dmmu.busy_out() && !dmmu.fault_out() && dmmu_access_ready_comb_func()
#endif
            );
        dcache.write_in = _ASSIGN( state_reg[1].valid && exe_mem.mem_write_out() && !dcache.busy_out()
#ifdef ENABLE_MMU_TLB
            && !dmmu.busy_out() && !dmmu.fault_out() && dmmu_access_ready_comb_func()
#endif
            );
        dcache.addr_in =
#ifdef ENABLE_MMU_TLB
            dmmu.paddr_out;
#else
            _ASSIGN( exe_mem.mem_read_out() ? (uint32_t)exe_mem.mem_read_addr_out() : (uint32_t)exe_mem.mem_write_addr_out() );
#endif
        dcache.write_data_in = exe_mem.mem_write_data_out;
        dcache.write_mask_in = exe_mem.mem_write_mask_out;
        dcache.mem_read_data_in = l2cache.d_read_data_out;
        dcache.mem_wait_in = l2cache.d_wait_out;
        dcache.stall_in = _ASSIGN(branch_stall_comb_func());
        dcache.flush_in = _ASSIGN(false);
        dcache.invalidate_in = external_cache_invalidate_in;
        // MMIO region bypasses L1 caching; RAM stays cacheable and coherent through L2.
        dcache.cache_disable_in = _ASSIGN(
            (uint32_t)dcache.addr_in() >= memory_base_in() + mem_region_size_in[0]() + mem_region_size_in[1]() + mem_region_size_in[2]() &&
            (uint32_t)dcache.addr_in() < memory_base_in() + memory_size_in());
        dcache.debugen_in = debugen_in;
        dcache.__inst_name = __inst_name + "/dcache";
        dcache._assign();

        bp.lookup_valid_in = _ASSIGN(decode_branch_valid_comb_func());
        bp.lookup_pc_in = _ASSIGN((uint32_t)dec.state_out().pc);
        bp.lookup_target_in = _ASSIGN(decode_branch_target_comb_func());
        bp.lookup_fallthrough_in = _ASSIGN(decode_fallthrough_comb_func());
        bp.lookup_br_op_in = _ASSIGN((u<4>)dec.state_out().br_op);
        bp.update_valid_in = _ASSIGN(state_reg[0].valid && state_reg[0].br_op != Br::BNONE && !memory_wait_comb_func());
        bp.update_pc_in = _ASSIGN((uint32_t)state_reg[0].pc);
        bp.update_taken_in = _ASSIGN(exe.branch_taken_out());
        bp.update_target_in = _ASSIGN(exe.branch_target_out());
        bp.__inst_name = __inst_name + "/bp";
        bp._assign();

        icache.read_in = _ASSIGN( (bool)valid
#ifdef ENABLE_MMU_TLB
            && !immu.busy_out() && !immu.fault_out()
#endif
            );
        icache.addr_in =
#ifdef ENABLE_MMU_TLB
            immu.paddr_out;
#else
            _ASSIGN( fetch_addr_comb_func() );
#endif
        icache.write_in = _ASSIGN( false );
        icache.write_data_in = _ASSIGN( (uint32_t)0 );
        icache.write_mask_in = _ASSIGN( (uint8_t)0 );
        icache.mem_read_data_in = l2cache.i_read_data_out;
        icache.mem_wait_in = l2cache.i_wait_out;
        icache.stall_in = _ASSIGN(memory_wait_comb_func() || stall_comb_func());
        icache.flush_in = _ASSIGN(branch_mispredict_comb_func() && !memory_wait_comb_func());
        icache.invalidate_in = _ASSIGN_COMB(icache_invalidate_comb_func());
        icache.cache_disable_in = _ASSIGN(false);
        icache.debugen_in = debugen_in;
        icache.__inst_name = __inst_name + "/icache";
        icache._assign();

        l2cache.i_read_in = icache.mem_read_out;
        l2cache.i_write_in = _ASSIGN(false);
        l2cache.i_addr_in = icache.mem_addr_out;
        l2cache.i_write_data_in = _ASSIGN((uint32_t)0);
        l2cache.i_write_mask_in = _ASSIGN((uint8_t)0);
        // CPU data misses and MMU page-table walks share the L2 data-side request path.
        l2cache.d_read_in = _ASSIGN(dcache.mem_read_out()
#ifdef ENABLE_MMU_TLB
            || dmmu_ptw_selected_comb_func() || immu_ptw_selected_comb_func()
#endif
            );
        l2cache.d_write_in = dcache.mem_write_out;
        l2cache.d_addr_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN(dcache.mem_read_out() || dcache.mem_write_out() ? (uint32_t)dcache.mem_addr_out() :
                (dmmu_ptw_selected_comb_func() ? (uint32_t)dmmu.mem_addr_out() : (uint32_t)immu.mem_addr_out()));
#else
            dcache.mem_addr_out;
#endif
        l2cache.d_write_data_in = dcache.mem_write_data_out;
        l2cache.d_write_mask_in = dcache.mem_write_mask_out;
        l2cache.memory_base_in = memory_base_in;
        l2cache.memory_size_in = memory_size_in;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            l2cache.mem_region_size_in[i] = mem_region_size_in[i];
        }
        // The last L2 region is IO/MMIO and is statically uncached.
        l2cache.mem_region_uncached_in[0] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[1] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[2] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[3] = _ASSIGN(true);
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            AXI4_DRIVER_FROM(l2cache.axi_in[i], axi_in[i]);
            AXI4_RESPONDER_FROM_I(l2cache.axi_out[i], axi_out[i]);
        }
        l2cache.debugen_in = debugen_in;
        l2cache.__inst_name = __inst_name + "/l2cache";
        l2cache._assign();
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM(axi_in[i], l2cache.axi_in[i]);
        }

        dmem_write_out      = dcache.mem_write_out;
        dmem_write_data_out = dcache.mem_write_data_out;
        dmem_write_mask_out = dcache.mem_write_mask_out;
        dmem_read_out       = dcache.mem_read_out;
        dmem_addr_out       = dcache.mem_addr_out;
        imem_read_addr_out  = icache.mem_addr_out;
#ifdef ENABLE_MMU_TLB
        debug_immu_ptw_read_out = immu.mem_read_out;
        debug_immu_ptw_addr_out = immu.mem_addr_out;
        debug_immu_busy_out = immu.busy_out;
        debug_immu_fault_out = immu.fault_out;
        debug_immu_paddr_out = immu.paddr_out;
        debug_immu_last_addr_out = immu.debug_last_addr_out;
        debug_immu_last_pte_out = immu.debug_last_pte_out;
        debug_icache_read_valid_out = icache.read_valid_out;
        debug_icache_read_addr_out = icache.read_addr_out;
        debug_dcache_read_valid_out = dcache.read_valid_out;
        debug_dcache_read_addr_out = dcache.read_addr_out;
        debug_dcache_read_data_out = dcache.read_data_out;
        debug_dcache_cpu_read_out = dcache.read_in;
        debug_dcache_cpu_write_out = dcache.write_in;
        debug_dcache_cpu_addr_out = dcache.addr_in;
        debug_dcache_cpu_wdata_out = dcache.write_data_in;
        debug_dcache_cpu_wmask_out = dcache.write_mask_in;
        debug_fetch_valid_out = _ASSIGN_COMB(fetch_valid_comb_func());
        debug_memory_wait_out = _ASSIGN_COMB(memory_wait_comb_func());
        debug_wb_load_ready_out = wb_mem.load_ready_out;
        debug_wb_mem_wait_out = _ASSIGN(state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && !wb_mem.load_ready_out());
        debug_wb_load_data_valid_out = wb_mem.debug_load_data_valid_out;
        debug_wb_load_addr_out = wb_mem.debug_load_addr_out;
        debug_wb_split_low_valid_out = wb_mem.debug_split_low_valid_out;
        debug_wb_split_high_valid_out = wb_mem.debug_split_high_valid_out;
        debug_wb_held_load_valid_out = wb_mem.debug_held_load_valid_out;
        debug_wb_split_load_in_out = exe_mem.split_load_out;
        debug_wb_alu_addr_out = wb_mem.alu_result_in;
        debug_wb_state_pc_out = _ASSIGN((uint32_t)state_reg[1].pc);
        debug_wb_state_wb_op_out = _ASSIGN((uint8_t)state_reg[1].wb_op);
        debug_wb_state_mem_op_out = _ASSIGN((uint8_t)state_reg[1].mem_op);
        debug_wb_state_rd_out = _ASSIGN((uint8_t)state_reg[1].rd);
        debug_wb_state_funct3_out = _ASSIGN((uint8_t)state_reg[1].funct3);
        debug_icache_read_in_out = _ASSIGN_COMB(icache.read_in());
        debug_icache_stall_in_out = _ASSIGN_COMB(icache.stall_in());
        debug_dmmu_ptw_read_out = dmmu.mem_read_out;
        debug_dmmu_ptw_addr_out = dmmu.mem_addr_out;
        debug_dmmu_busy_out = dmmu.busy_out;
        debug_dmmu_fault_out = dmmu.fault_out;
        debug_mmu_ptw_word_out = _ASSIGN_COMB(mmu_l2_read_word_comb_func());
        debug_pc_out = _ASSIGN_REG(pc);
        debug_satp_out = csr.satp_out;
        debug_mstatus_out = csr.mstatus_out;
        debug_mtvec_out = csr.mtvec_out;
        debug_mepc_out = csr.mepc_out;
        debug_mcause_out = csr.mcause_out;
        debug_mtval_out = csr.mtval_out;
        debug_sepc_out = csr.sepc_out;
        debug_stvec_out = csr.stvec_out;
        debug_scause_out = csr.scause_out;
        debug_stval_out = csr.stval_out;
        debug_irq_valid_out = irq.interrupt_valid_out;
        debug_irq_cause_out = irq.interrupt_cause_out;
        debug_irq_to_supervisor_out = irq.interrupt_to_supervisor_out;
        debug_irq_mip_out = irq.mip_out;
        debug_irq_mie_out = csr.mie_out;
        debug_irq_mideleg_out = csr.mideleg_out;
        debug_priv_out = csr.priv_out;
        debug_ra_out = regs.x1_out;
        debug_regs_write_out = wb.regs_write_out;
        debug_regs_write_actual_out = _ASSIGN(wb.regs_write_out() &&
            !memory_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || wb_mem.load_ready_out()));
        debug_regs_wr_id_out = wb.regs_wr_id_out;
        debug_regs_data_out = wb.regs_data_out;
        debug_branch_taken_now_out = exe.branch_taken_out;
        debug_branch_target_now_out = exe.branch_target_out;
        debug_decode_instr_out = icache.read_data_out;
        debug_decode_pc_out = _ASSIGN((uint32_t)dec.state_out().pc);
        debug_decode_br_out = _ASSIGN((uint8_t)dec.state_out().br_op);
        debug_decode_imm_out = _ASSIGN((uint32_t)dec.state_out().imm);
#endif
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            AXI4_DRIVER_FROM_I(axi_out[i], l2cache.axi_out[i]);
        }
    }

private:

    reg<u32>        pc;
    reg<u1>         valid;

    reg<u32>        alu_result_reg;

    reg<array<State,STAGES_NUM-1>> state_reg;
    reg<array<u32,STAGES_NUM-1>> predicted_next_reg;
    reg<array<u32,STAGES_NUM-1>> fallthrough_reg;
    reg<array<u1,STAGES_NUM-1>> predicted_taken_reg;

    // debug
    reg<u32>        debug_alu_a_reg;
    reg<u32>        debug_alu_b_reg;
    reg<u32>        debug_branch_target_reg;
    reg<u1>         debug_branch_taken_reg;
    reg<u1>         output_write_active_reg;
    reg<u1>         interrupt_entry_guard_reg;
    reg<u1>         sbi_ret_a1_valid_reg;
    reg<u32>        sbi_ret_a1_reg;

    // Hold decode/execute when a pending load, split access, or atomic op would be observed too early.
    _LAZY_COMB(hazard_stall_comb, bool)
        hazard_stall_comb = false;
        if (fetch_valid_comb_func() && state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex hazard
            const auto& dec_state_tmp = dec.state_out();

            if (state_reg[0].rd == dec_state_tmp.rs1) {
                hazard_stall_comb = true;
            }
            if (state_reg[0].rd == dec_state_tmp.rs2) {
                hazard_stall_comb = true;
            }
        }
        if (exe_mem.mem_split_out() || exe_mem.mem_split_busy_out()) {
            hazard_stall_comb = true;
        }
#ifdef ENABLE_RV32IA
        if (exe_mem.atomic_busy_out()) {
            hazard_stall_comb = true;
        }
#endif
        return hazard_stall_comb;
    }

    // A mispredict freezes the front end for one correction cycle.
    _LAZY_COMB(branch_stall_comb, bool)
        branch_stall_comb = branch_mispredict_comb_func();
        return branch_stall_comb;
    }

    // Flush decode/fetch state when execute resolves a different next PC.
    _LAZY_COMB(branch_flush_comb, bool)
        branch_flush_comb = branch_mispredict_comb_func();
        return branch_flush_comb;
    }

    // Combined front-end stall used by fetch and decode.
    _LAZY_COMB(stall_comb, bool)
        stall_comb = hazard_stall_comb_func() || branch_stall_comb_func();
        return stall_comb;
    }

    // Pack per-cycle stall and cache wait indicators for the test harness.
    _LAZY_COMB(perf_comb, TribePerf)
        perf_comb.hazard_stall = hazard_stall_comb_func();
        perf_comb.branch_stall = branch_stall_comb_func();
        perf_comb.dcache_wait = dcache.busy_out();
        perf_comb.icache_wait = icache.busy_out();
        perf_comb.icache = icache.perf_out();
        perf_comb.dcache = dcache.perf_out();
        return perf_comb;
    }

#ifdef ENABLE_MMU_TLB
    // DMMU page-table walks share the L2 data port after normal CPU data requests.
    _LAZY_COMB(dmmu_ptw_selected_comb, bool)
        dmmu_ptw_selected_comb = dmmu.mem_read_out() && !dcache.mem_read_out() && !dcache.mem_write_out();
        return dmmu_ptw_selected_comb;
    }

    // IMMU page-table walks use the shared L2 data port only when DMMU and D-cache are idle.
    _LAZY_COMB(immu_ptw_selected_comb, bool)
        immu_ptw_selected_comb = immu.mem_read_out() && !dmmu.mem_read_out() &&
            !dcache.mem_read_out() && !dcache.mem_write_out();
        return immu_ptw_selected_comb;
    }

    // Extract the 32-bit PTE lane from the current L2 AXI-width read beat.
    _LAZY_COMB(mmu_l2_read_word_comb, uint32_t)
        uint32_t addr;
        uint32_t lane;
        addr = dmmu_ptw_selected_comb_func() ? (uint32_t)dmmu.mem_addr_out() : (uint32_t)immu.mem_addr_out();
#ifdef TRIBE_L2_AXI_WIDTH_IS_64
        lane = (addr % 8u) / 4u;
#elif defined(TRIBE_L2_AXI_WIDTH_IS_128)
        lane = (addr % 16u) / 4u;
#else
        lane = (addr % 32u) / 4u;
#endif
        mmu_l2_read_word_comb = (uint32_t)l2cache.d_read_data_out().bits(lane * 32 + 31, lane * 32);
        return mmu_l2_read_word_comb;
    }
    // Do not let D-cache consume dmmu.paddr_out until the current translated access has a TLB hit.
    _LAZY_COMB(dmmu_access_ready_comb, bool)
        bool access;
        access = state_reg[1].valid && (exe_mem.mem_read_out() || exe_mem.mem_write_out());
        dmmu_access_ready_comb = !access || !dmmu.translated_out() || dmmu.hit_out() || dmmu.fault_out();
        return dmmu_access_ready_comb;
    }
#endif

    // Global pipeline memory wait, including split accesses, atomics, cache waits, and page-table walks.
    _LAZY_COMB(memory_wait_comb, bool)
        bool data_mem_access;
        bool next_data_mem_access;
        bool dmmu_faulted_access;
        data_mem_access = state_reg[1].valid && (
            exe_mem.mem_read_out() ||
            exe_mem.mem_write_out() ||
            state_reg[1].mem_op == Mem::STORE ||
            state_reg[1].wb_op == Wb::MEM);
        next_data_mem_access = state_reg[0].valid &&
            (state_reg[0].mem_op == Mem::LOAD || state_reg[0].mem_op == Mem::STORE);
        dmmu_faulted_access = false;
#ifdef ENABLE_MMU_TLB
        dmmu_faulted_access = state_reg[1].valid && dmmu.fault_out() &&
            (exe_mem.mem_read_out() || exe_mem.mem_write_out());
#endif
        memory_wait_comb =
#ifdef ENABLE_RV32IA
            (data_mem_access && !dmmu_faulted_access && exe_mem.atomic_busy_out()) ||
#endif
#ifdef ENABLE_MMU_TLB
            ((bool)valid && immu.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && dmmu.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && !dmmu_access_ready_comb_func()) ||
#endif
            (next_data_mem_access && dcache.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && dcache.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && exe_mem.mem_split_busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && dcache.mem_read_out() && l2cache.d_wait_out()) ||
            (data_mem_access && !dmmu_faulted_access &&
                (exe_mem.mem_write_out() || state_reg[1].mem_op == Mem::STORE) && l2cache.d_wait_out()) ||
            (state_reg[0].valid && state_reg[0].sys_op == Sys::FENCE &&
                (dcache.busy_out() || l2cache.d_wait_out() || l2cache.i_wait_out())) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !dmmu_faulted_access &&
            !wb_mem.load_ready_out());
        return memory_wait_comb;
    }

    // Fetched instruction is valid only when it matches the current PC or translated physical PC.
    _LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && icache.read_valid_out() &&
#ifdef ENABLE_MMU_TLB
            // I-cache stores physical refill addresses; the architectural PC can be virtual.
            icache.read_addr_out() == (uint32_t)immu.paddr_out();
#else
            icache.read_addr_out() == (uint32_t)pc;
#endif
    }

    // Sequential next PC respects 16-bit compressed and 32-bit instructions.
    _LAZY_COMB(decode_fallthrough_comb, uint32_t)
        return decode_fallthrough_comb = pc + ((dec.instr_in()&3)==3?4:2);
    }

    // Predictor sees only direct branches in decode. Register-indirect JALR/JR
    // targets can depend on a just-loaded register, so they are redirected from
    // execute where forwarding has already been applied.
    _LAZY_COMB(decode_branch_valid_comb, bool)
        decode_branch_valid_comb = fetch_valid_comb_func() && dec.state_out().valid &&
            dec.state_out().br_op != Br::BNONE &&
            dec.state_out().br_op != Br::JALR &&
            dec.state_out().br_op != Br::JR &&
            !stall_comb_func();
        return decode_branch_valid_comb;
    }

    // Register-indirect branches wait one cycle for execute-stage forwarding,
    // so the frontend must not speculatively fetch their fallthrough address.
    _LAZY_COMB(decode_indirect_branch_valid_comb, bool)
        decode_indirect_branch_valid_comb = fetch_valid_comb_func() && dec.state_out().valid &&
            (dec.state_out().br_op == Br::JALR || dec.state_out().br_op == Br::JR) &&
            !stall_comb_func();
        return decode_indirect_branch_valid_comb;
    }

    // Predicted branch target from decode operands and immediate.
    _LAZY_COMB(decode_branch_target_comb, uint32_t)
        const auto& dec_state_tmp = dec.state_out();
        decode_branch_target_comb = 0;
        if (dec_state_tmp.br_op == Br::JAL) {
            decode_branch_target_comb = dec_state_tmp.pc + dec_state_tmp.imm;
        }
        else if (dec_state_tmp.br_op == Br::JALR || dec_state_tmp.br_op == Br::JR) {
            decode_branch_target_comb = (dec_state_tmp.rs1_val + dec_state_tmp.imm) & ~1U;
        }
        else {
            decode_branch_target_comb = dec_state_tmp.pc + dec_state_tmp.imm;
        }
        return decode_branch_target_comb;
    }

    // Execute-stage resolved next PC for mispredict comparison.
    _LAZY_COMB(branch_actual_next_comb, uint32_t)
        return branch_actual_next_comb = exe.branch_taken_out() ? exe.branch_target_out() : (uint32_t)fallthrough_reg[0];
    }

    // Detect when the decoded prediction does not match execute resolution.
    _LAZY_COMB(branch_mispredict_comb, bool)
        branch_mispredict_comb = state_reg[0].valid && exe_state_comb_func().br_op != Br::BNONE &&
            branch_actual_next_comb_func() != (uint32_t)predicted_next_reg[0];
        return branch_mispredict_comb;
    }

    // Fetch address, with execute redirect taking priority over sequential PC.
    _LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        if (branch_mispredict_comb_func()) {
            fetch_addr_comb = branch_actual_next_comb_func();
        }
        return fetch_addr_comb;
    }

    static constexpr uint32_t SBI_EXT_BASE = 0x10;
    static constexpr uint32_t SBI_EXT_TIME = 0x54494d45;
    static constexpr uint32_t SBI_EXT_RFENCE = 0x52464e43;
    static constexpr uint32_t SBI_SUCCESS = 0;

    // SBI ECALLs are handled locally because this model has no M-mode firmware.
    bool sbi_legacy_ecall_comb;
    bool& sbi_legacy_ecall_comb_func()
    {
        return sbi_legacy_ecall_comb = state_reg[0].valid &&
            state_reg[0].sys_op == Sys::ECALL &&
            csr.priv_out() == (u<2>)1;
    }

    uint32_t sbi_arg_value(uint8_t reg_id)
    {
        // SBI arguments are not explicit ECALL source registers in Decode.
        // Forward the writeback stage so sequences such as "li a7,6; ecall"
        // observe the freshly produced extension ID before it is committed.
        if (wb.regs_write_out() && wb.regs_wr_id_out() == reg_id) {
            return wb.regs_data_out();
        }
        if (reg_id == 10) {
            return regs.x10_out();
        }
        if (reg_id == 11) {
            return regs.x11_out();
        }
        if (reg_id == 16) {
            return regs.x16_out();
        }
        if (reg_id == 17) {
            return regs.x17_out();
        }
        return 0;
    }

    // Single-hart Linux still emits remote fence SBI calls during VM changes; they are no-ops here.
    bool sbi_noop_comb;
    bool& sbi_noop_comb_func()
    {
        uint32_t ext;
        ext = sbi_arg_value(17);
        return sbi_noop_comb = sbi_legacy_ecall_comb_func() &&
            (ext == 5 || ext == 6 || ext == 7 || ext == SBI_EXT_RFENCE);
    }

    bool sbi_base_comb;
    bool& sbi_base_comb_func()
    {
        return sbi_base_comb = sbi_legacy_ecall_comb_func() && sbi_arg_value(17) == SBI_EXT_BASE;
    }

    bool sbi_set_timer_comb;
    bool& sbi_set_timer_comb_func()
    {
        uint32_t ext;
        ext = sbi_arg_value(17);
        return sbi_set_timer_comb = sbi_legacy_ecall_comb_func() &&
            (ext == 0 || (ext == SBI_EXT_TIME && sbi_arg_value(16) == 0));
    }

    uint32_t sbi_ret_value_comb;
    uint32_t& sbi_ret_value_comb_func()
    {
        uint32_t fid;
        uint32_t ext;
        uint32_t probe_ext;
        fid = sbi_arg_value(16);
        ext = sbi_arg_value(17);
        probe_ext = sbi_arg_value(10);
        sbi_ret_value_comb = 0;
        if (ext == SBI_EXT_BASE) {
            if (fid == 0) {
                // SBI v0.2 is enough for Linux to use the TIME extension.
                sbi_ret_value_comb = 2;
            }
            else if (fid == 1) {
                sbi_ret_value_comb = 0;
            }
            else if (fid == 2) {
                sbi_ret_value_comb = 1;
            }
            else if (fid == 3) {
                sbi_ret_value_comb = (probe_ext == SBI_EXT_BASE ||
                    probe_ext == SBI_EXT_TIME ||
                    probe_ext == SBI_EXT_RFENCE) ? 1 : 0;
            }
            else {
                sbi_ret_value_comb = 0;
            }
        }
        return sbi_ret_value_comb;
    }

    bool sbi_writes_a1_comb;
    bool& sbi_writes_a1_comb_func()
    {
        return sbi_writes_a1_comb = sbi_base_comb_func() || sbi_set_timer_comb_func();
    }

    // All locally handled SBI calls retire as successful calls with a0=0.
    bool sbi_handled_comb;
    bool& sbi_handled_comb_func()
    {
        return sbi_handled_comb = sbi_set_timer_comb_func() || sbi_noop_comb_func() || sbi_base_comb_func();
    }

    // Low word of the SBI timer value is passed in a0 on RV32.
    uint32_t sbi_timer_lo_comb;
    uint32_t& sbi_timer_lo_comb_func()
    {
        return sbi_timer_lo_comb = sbi_arg_value(10);
    }

    // High word of the SBI timer value is passed in a1 on RV32.
    uint32_t sbi_timer_hi_comb;
    uint32_t& sbi_timer_hi_comb_func()
    {
        return sbi_timer_hi_comb = sbi_arg_value(11);
    }

    bool sbi_ecall_debug_comb;
    bool& sbi_ecall_debug_comb_func()
    {
        return sbi_ecall_debug_comb = state_reg[0].valid && state_reg[0].sys_op == Sys::ECALL;
    }

    uint32_t sbi_a7_debug_comb;
    uint32_t& sbi_a7_debug_comb_func()
    {
        return sbi_a7_debug_comb = sbi_arg_value(17);
    }

    uint32_t sbi_a6_debug_comb;
    uint32_t& sbi_a6_debug_comb_func()
    {
        return sbi_a6_debug_comb = sbi_arg_value(16);
    }

    uint32_t sbi_a0_debug_comb;
    uint32_t& sbi_a0_debug_comb_func()
    {
        return sbi_a0_debug_comb = sbi_arg_value(10);
    }

    // Keep raw pending interrupt state visible in mip/sip, but accept an
    // interrupt only at a normal execute boundary. Waiting here is important:
    // the PC redirect and CSR trap update must happen in the same cycle.
    _LAZY_COMB(interrupt_accept_comb, bool)
#ifdef ENABLE_ISR
        bool trap_redirect;
        trap_redirect = state_reg[0].valid &&
            (state_reg[0].sys_op == Sys::MRET ||
             state_reg[0].sys_op == Sys::SRET ||
             state_reg[0].sys_op == Sys::ECALL ||
             state_reg[0].sys_op == Sys::EBREAK ||
             state_reg[0].sys_op == Sys::TRAP ||
             state_reg[0].trap_op != Trap::TNONE ||
             csr.illegal_trap_out());
        return interrupt_accept_comb = state_reg[0].valid && irq.interrupt_valid_out() &&
            !interrupt_entry_guard_reg && !trap_redirect && !memory_wait_comb_func() &&
            !hazard_stall_comb_func();
#else
        return interrupt_accept_comb = false;
#endif
    }

    // Execute input state after trap/xret/fence redirection and late load forwarding.
    _LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
#ifdef ENABLE_ZICSR
        if (sbi_handled_comb_func()) {
            exe_state_comb.sys_op = Sys::SNONE;
            exe_state_comb.trap_op = Trap::TNONE;
            exe_state_comb.csr_op = Csr::CNONE;
            exe_state_comb.mem_op = Mem::MNONE;
            exe_state_comb.br_op = Br::BNONE;
            exe_state_comb.alu_op = Alu::ADD;
            exe_state_comb.rs1_val = 0;
            exe_state_comb.rs2_val = 0;
            exe_state_comb.imm = 0;
            exe_state_comb.rd = 10;
            exe_state_comb.wb_op = Wb::ALU;
        }
        if (state_reg[0].valid &&
            !sbi_handled_comb_func() &&
            (
#ifdef ENABLE_ISR
             interrupt_accept_comb_func() ||
#endif
             state_reg[0].sys_op == Sys::ECALL ||
             state_reg[0].sys_op == Sys::EBREAK ||
             state_reg[0].sys_op == Sys::TRAP ||
             state_reg[0].trap_op != Trap::TNONE ||
             csr.illegal_trap_out())) {
            exe_state_comb.rs1_val = csr.trap_vector_out();
            exe_state_comb.imm = 0;
            exe_state_comb.br_op = Br::JR;
            exe_state_comb.mem_op = Mem::MNONE;
            exe_state_comb.wb_op = Wb::WNONE;
#ifdef ENABLE_RV32IA
            exe_state_comb.amo_op = Amo::AMONONE;
#endif
        }
        else if (state_reg[0].valid && (state_reg[0].sys_op == Sys::MRET || state_reg[0].sys_op == Sys::SRET)) {
            exe_state_comb.rs1_val = csr.epc_out();
            exe_state_comb.imm = 0;
        }
        if (state_reg[0].valid && state_reg[0].sys_op == Sys::FENCEI) {
            exe_state_comb.rs1_val = state_reg[0].pc + 4;
            exe_state_comb.imm = 0;
        }
#endif
        if (wb_mem.load_ready_out() && state_reg[1].rd != 0) {
            if (state_reg[0].rs1 == state_reg[1].rd) {
                exe_state_comb.rs1_val = wb_mem.load_result_out();
            }
            if (state_reg[0].rs2 == state_reg[1].rd) {
                exe_state_comb.rs2_val = wb_mem.load_result_out();
            }
        }
        return exe_state_comb;
    }

#ifdef ENABLE_ZICSR
#ifdef ENABLE_MMU_TLB
    // DMMU faults matter only while a valid memory-stage instruction is still
    // actively reading or writing. After a trap flush the DMMU fault output can
    // remain asserted for one cycle, but it must not overwrite sepc/stval.
    _LAZY_COMB(dmmu_active_fault_comb, bool)
        return dmmu_active_fault_comb = state_reg[1].valid && dmmu.fault_out() &&
            (exe_mem.mem_read_out() || exe_mem.mem_write_out());
    }
#endif

    // CSR/trap stage input, including synthesized page faults and locally emulated SBI timer calls.
    _LAZY_COMB(csr_state_comb, State)
        csr_state_comb = exe_state_comb_func();
        if (sbi_handled_comb_func()) {
            csr_state_comb.sys_op = Sys::SNONE;
            csr_state_comb.trap_op = Trap::TNONE;
            csr_state_comb.csr_op = Csr::CNONE;
        }
#ifdef ENABLE_MMU_TLB
        if (immu.fault_out() && !state_reg[0].valid) {
            csr_state_comb = State{};
            csr_state_comb.valid = true;
            csr_state_comb.pc = fetch_addr_comb_func();
            csr_state_comb.imm = fetch_addr_comb_func();
            csr_state_comb.sys_op = Sys::TRAP;
            csr_state_comb.trap_op = Trap::INST_PAGE_FAULT;
            csr_state_comb.csr_op = Csr::CNONE;
            csr_state_comb.mem_op = Mem::MNONE;
            csr_state_comb.wb_op = Wb::WNONE;
            csr_state_comb.br_op = Br::JR;
        }
        if (dmmu_active_fault_comb_func()) {
            csr_state_comb = State{};
            csr_state_comb.valid = true;
            csr_state_comb.pc = state_reg[1].pc;
            csr_state_comb.imm = exe_mem.mem_read_out() ?
                (uint32_t)exe_mem.mem_read_addr_out() : (uint32_t)exe_mem.mem_write_addr_out();
            csr_state_comb.sys_op = Sys::TRAP;
            csr_state_comb.trap_op = exe_mem.mem_write_out() ? Trap::STORE_PAGE_FAULT : Trap::LOAD_PAGE_FAULT;
            csr_state_comb.csr_op = Csr::CNONE;
            csr_state_comb.mem_op = Mem::MNONE;
            csr_state_comb.wb_op = Wb::WNONE;
            csr_state_comb.br_op = Br::JR;
        }
#endif
        if (
#ifdef ENABLE_ISR
            interrupt_accept_comb_func() ||
#endif
            csr.illegal_trap_out()) {
            csr_state_comb = state_reg[0];
#ifdef ENABLE_ISR
            if (interrupt_accept_comb_func()) {
                csr_state_comb.imm = 0;
            }
#endif
            csr_state_comb.sys_op = Sys::TRAP;
            csr_state_comb.trap_op =
#ifdef ENABLE_ISR
                interrupt_accept_comb_func() ? Trap::TNONE :
#endif
                Trap::ILLEGAL_INST;
            csr_state_comb.csr_op = Csr::CNONE;
            csr_state_comb.mem_op = Mem::MNONE;
            csr_state_comb.wb_op = Wb::WNONE;
            csr_state_comb.br_op = Br::JR;
        }
        if (memory_wait_comb_func()
#ifdef ENABLE_MMU_TLB
            && !immu.fault_out() && !dmmu_active_fault_comb_func()
#endif
            ) {
            csr_state_comb.valid = false;
        }
        return csr_state_comb;
    }
#endif

    // FENCE.I and SFENCE.VMA both discard I-cache contents before fetching translated code again.
    _LAZY_COMB(icache_invalidate_comb, bool)
        return icache_invalidate_comb = state_reg[0].valid &&
            (state_reg[0].sys_op == Sys::FENCEI || state_reg[0].sys_op == Sys::SFENCE_VMA) &&
            !memory_wait_comb_func() && !icache_invalidate_issued_reg;
    }

    // SFENCE.VMA invalidates cached translations once the instruction can retire.
    _LAZY_COMB(sfence_vma_comb, bool)
        return sfence_vma_comb = state_reg[0].valid && state_reg[0].sys_op == Sys::SFENCE_VMA && !memory_wait_comb_func();
    }

    void forward()
    {
        const auto& dec_state_tmp = dec.state_out();

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem/Wb alu
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)alu_result_reg);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)alu_result_reg);
                }
            }
        }

        if (wb_mem.load_ready_out() && state_reg[1].rd != 0) {  // Mem/Wb mem
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = wb_mem.load_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS1\n", (uint32_t)wb_mem.load_result_out());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = wb_mem.load_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS2\n", (uint32_t)wb_mem.load_result_out());
                }
            }
        }

        if (state_reg[1].valid && (state_reg[1].wb_op == Wb::PC2 || state_reg[1].wb_op == Wb::PC4) && state_reg[1].rd != 0) {  // Mem/Wb link
            uint32_t link_value = state_reg[1].pc + (state_reg[1].wb_op == Wb::PC2 ? 2 : 4);
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS1\n", link_value);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS2\n", link_value);
                }
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex/Mem alu/csr
            if (dec_state_tmp.rs1 == state_reg[0].rd) {
                state_reg._next[0].rs1_val =
#ifdef ENABLE_RV32IA
                        state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() : exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", state_reg[0].csr_op != Csr::CNONE ? (uint32_t)csr.read_data_out() : (uint32_t)exe.alu_result_out());
                }
#else
                        exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)exe.alu_result_out());
                }
#endif
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg._next[0].rs2_val =
#ifdef ENABLE_RV32IA
                        state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() : exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", state_reg[0].csr_op != Csr::CNONE ? (uint32_t)csr.read_data_out() : (uint32_t)exe.alu_result_out());
                }
#else
                        exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)exe.alu_result_out());
                }
#endif
            }
        }

        if (state_reg[0].valid && (state_reg[0].wb_op == Wb::PC2 || state_reg[0].wb_op == Wb::PC4) && state_reg[0].rd != 0) {  // Ex/Mem link
            uint32_t link_value = state_reg[0].pc + (state_reg[0].wb_op == Wb::PC2 ? 2 : 4);
            if (dec_state_tmp.rs1 == state_reg[0].rd) {
                state_reg._next[0].rs1_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS1\n", link_value);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg._next[0].rs2_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS2\n", link_value);
                }
            }
        }
    }

public:

    void _work(bool reset)
    {
#ifndef SYNTHESIS
        const char* trace_pc_write_from_env = std::getenv("TRIBE_TRACE_PC_WRITE_FROM");
        const char* trace_pc_write_target_env = std::getenv("TRIBE_TRACE_PC_WRITE_TARGET");
        const char* trace_pc_write_reason_env = std::getenv("TRIBE_TRACE_PC_WRITE_REASON");
        const char* trace_pc_write_file_env = std::getenv("TRIBE_TRACE_PC_WRITE_FILE");
        const bool trace_pc_write_all = std::getenv("TRIBE_TRACE_PC_WRITE_ALL") != nullptr;
        const bool trace_pc_write_zero_only = std::getenv("TRIBE_TRACE_PC_WRITE_ZERO_ONLY") != nullptr;
        const bool trace_pc_write_user_kernel = std::getenv("TRIBE_TRACE_PC_WRITE_USER_KERNEL") != nullptr;
        const uint32_t trace_pc_write_target = trace_pc_write_target_env != nullptr ?
            (uint32_t)std::strtoul(trace_pc_write_target_env, nullptr, 0) : 0;
        auto trace_pc_write = [&](const char* reason, uint32_t next_pc) {
            if (trace_pc_write_from_env == nullptr) {
                return;
            }
            const long long from_cycle = std::strtoll(trace_pc_write_from_env, nullptr, 0);
            if (_system_clock < from_cycle) {
                return;
            }
            if (trace_pc_write_reason_env != nullptr && std::strstr(reason, trace_pc_write_reason_env) == nullptr) {
                return;
            }
            const uint32_t old_pc = (uint32_t)pc;
            if (trace_pc_write_user_kernel &&
                ((uint32_t)csr.priv_out() != 0 || (next_pc < 0x80000000u && old_pc < 0x80000000u))) {
                return;
            }
            if (trace_pc_write_zero_only && old_pc != 0 && next_pc != 0) {
                return;
            }
            if (trace_pc_write_target != 0 && old_pc != trace_pc_write_target && next_pc != trace_pc_write_target) {
                return;
            }
            if (trace_pc_write_target == 0 && !trace_pc_write_all && old_pc >= 0x10000u && next_pc >= 0x10000u) {
                return;
            }
            static FILE* trace_pc_write_file = nullptr;
            static bool trace_pc_write_file_initialized = false;
            if (!trace_pc_write_file_initialized) {
                trace_pc_write_file_initialized = true;
                if (trace_pc_write_file_env != nullptr && trace_pc_write_file_env[0] != 0) {
                    trace_pc_write_file = fopen(trace_pc_write_file_env, "wb");
                    if (trace_pc_write_file != nullptr) {
                        setvbuf(trace_pc_write_file, nullptr, _IOLBF, 0);
                    }
                }
            }
            FILE* trace_pc_write_out = trace_pc_write_file != nullptr ? trace_pc_write_file : stdout;
            std::print(trace_pc_write_out, "trace-pc-write cycle={} reason={} pc={:08x} next={:08x} valid={} fetch_valid={} memwait={} stall={} hazard={} branch_mispredict={} branch_target={:08x} predicted={:08x} state0_valid={} state0_pc={:08x} state0_wb={} state0_rd={} state0_sys={} state0_trap={} state0_br={} state1_valid={} state1_pc={:08x} state1_wb={} state1_rd={}",
                _system_clock,
                reason,
                old_pc,
                next_pc,
                (bool)valid,
                (bool)fetch_valid_comb_func(),
                (bool)memory_wait_comb_func(),
                (bool)stall_comb_func(),
                (bool)hazard_stall_comb_func(),
                (bool)branch_mispredict_comb_func(),
                (uint32_t)branch_actual_next_comb_func(),
                (uint32_t)predicted_next_reg[0],
                (bool)state_reg[0].valid,
                (uint32_t)state_reg[0].pc,
                (uint32_t)state_reg[0].wb_op,
                (uint32_t)state_reg[0].rd,
                (uint32_t)state_reg[0].sys_op,
                (uint32_t)state_reg[0].trap_op,
                (uint32_t)state_reg[0].br_op,
                (bool)state_reg[1].valid,
                (uint32_t)state_reg[1].pc,
                (uint32_t)state_reg[1].wb_op,
                (uint32_t)state_reg[1].rd);
#ifdef ENABLE_MMU_TLB
            std::print(trace_pc_write_out, " immu_fault={} immu_busy={} immu_paddr={:08x} dmmu_fault={} dmmu_active={}",
                (bool)immu.fault_out(),
                (bool)immu.busy_out(),
                (uint32_t)immu.paddr_out(),
                (bool)dmmu.fault_out(),
                (bool)dmmu_active_fault_comb_func());
#endif
#ifdef ENABLE_ISR
            std::print(trace_pc_write_out, " irq_valid={} irq_cause={} irq_to_s={} irq_mip={:08x} irq_mie={:08x} irq_mideleg={:08x} clint_mtip={} external_irq={}",
                (bool)irq.interrupt_valid_out(),
                (uint32_t)irq.interrupt_cause_out(),
                (bool)irq.interrupt_to_supervisor_out(),
                (uint32_t)irq.mip_out(),
                (uint32_t)csr.mie_out(),
                (uint32_t)csr.mideleg_out(),
                (bool)clint_mtip_in(),
                (bool)external_irq_in());
#endif
#ifdef ENABLE_ZICSR
            std::print(trace_pc_write_out, " csr_illegal={}", (bool)csr.illegal_trap_out());
#endif
#ifdef ENABLE_ZICSR
            std::print(trace_pc_write_out, " priv={} stvec={:08x} sepc={:08x} scause={:08x} stval={:08x} mepc={:08x} mtvec={:08x}",
                (uint32_t)csr.priv_out(),
                (uint32_t)csr.stvec_out(),
                (uint32_t)csr.sepc_out(),
                (uint32_t)csr.scause_out(),
                (uint32_t)csr.stval_out(),
                (uint32_t)csr.mepc_out(),
                (uint32_t)csr.mtvec_out());
#endif
            std::print(trace_pc_write_out, "\n");
        };
#endif
        if (debugen_in && !reset) {
            debug();
        }
        sbi_ret_a1_valid_reg._next = false;
        sbi_ret_a1_reg._next = sbi_ret_a1_reg;

        if (dmem_addr_out() == 0x11223344 && dmem_write_out() && !output_write_active_reg) {
            FILE* out = fopen("out.txt", "a");
            if (debugen_in) {
                std::print("OUTPUT pc={} data={:08x} char={:02x}\n", pc, dmem_write_data_out(), dmem_write_data_out() & 0xFF);
            }
            fprintf(out, "%c", dmem_write_data_out()&0xFF);
            fclose(out);
        }
        output_write_active_reg._next = dmem_addr_out() == 0x11223344 && dmem_write_out();

#if defined(ENABLE_ZICSR) && defined(ENABLE_MMU_TLB)
        if (!reset && state_reg[0].valid && (state_reg[0].sys_op == Sys::MRET || state_reg[0].sys_op == Sys::SRET)) {
            uint32_t epc = state_reg[0].sys_op == Sys::SRET ? (uint32_t)csr.sepc_out() : (uint32_t)csr.mepc_out();
            pc._next = epc;
#ifndef SYNTHESIS
            trace_pc_write("xret-mmu", epc);
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = State{};
            state_reg._next[1].valid = false;
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            alu_result_reg._next = alu_result_reg;
            debug_branch_target_reg._next = epc;
            debug_branch_taken_reg._next = true;
            interrupt_entry_guard_reg._next = false;
        }
        else
        if (!reset && state_reg[0].valid &&
            !sbi_handled_comb_func() &&
            (
#ifdef ENABLE_ISR
             interrupt_accept_comb_func() ||
#endif
             state_reg[0].sys_op == Sys::ECALL ||
             state_reg[0].sys_op == Sys::EBREAK ||
             state_reg[0].sys_op == Sys::TRAP ||
             state_reg[0].trap_op != Trap::TNONE ||
             csr.illegal_trap_out())) {
            pc._next = csr.trap_vector_out();
#ifndef SYNTHESIS
            trace_pc_write("trap-exec-mmu", (uint32_t)csr.trap_vector_out());
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = State{};
            state_reg._next[1].valid = false;
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            alu_result_reg._next = alu_result_reg;
            debug_branch_target_reg._next = csr.trap_vector_out();
            debug_branch_taken_reg._next = true;
            interrupt_entry_guard_reg._next =
#ifdef ENABLE_ISR
                (u1)interrupt_accept_comb_func();
#else
                (u1)false;
#endif
            ;
        }
        else
        if (!reset && immu.fault_out() && state_reg[0].valid && !dmmu_active_fault_comb_func()) {
            // Fetch faults are younger than the execute-stage instruction.
            // Drain that instruction first so JAL/JALR links and xRET privilege
            // changes retire before any trap caused by the next fetch.
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("fetch-fault-drain", (uint32_t)pc);
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = state_reg[0];
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            alu_result_reg._next =
#ifdef ENABLE_ZICSR
                state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() :
#endif
#ifdef ENABLE_RV32IA
                (state_reg[0].amo_op == Amo::SC_W ? exe_mem.atomic_sc_result_out() : exe.alu_result_out());
#else
                 exe.alu_result_out();
#endif
            debug_branch_target_reg._next = exe.branch_target_out();
            debug_branch_taken_reg._next = exe.branch_taken_out();
            interrupt_entry_guard_reg._next = false;
        }
        else
        if (!reset && (immu.fault_out() || dmmu_active_fault_comb_func())) {
            pc._next = csr.trap_vector_out();
#ifndef SYNTHESIS
            trace_pc_write("mmu-fault-trap", (uint32_t)csr.trap_vector_out());
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = State{};
            state_reg._next[1].valid = false;
            alu_result_reg._next = alu_result_reg;
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            debug_branch_target_reg._next = csr.trap_vector_out();
            debug_branch_taken_reg._next = true;
            interrupt_entry_guard_reg._next = false;
        }
        else
#endif
#ifdef ENABLE_ZICSR
        if (!reset && state_reg[0].valid && (state_reg[0].sys_op == Sys::MRET || state_reg[0].sys_op == Sys::SRET)) {
            uint32_t epc = state_reg[0].sys_op == Sys::SRET ? (uint32_t)csr.sepc_out() : (uint32_t)csr.mepc_out();
            pc._next = epc;
#ifndef SYNTHESIS
            trace_pc_write("xret", epc);
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = State{};
            state_reg._next[1].valid = false;
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            alu_result_reg._next = alu_result_reg;
            debug_branch_target_reg._next = epc;
            debug_branch_taken_reg._next = true;
            interrupt_entry_guard_reg._next = false;
        }
        else
#endif
        if (sbi_handled_comb_func()) {
            interrupt_entry_guard_reg._next = false;
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("sbi-retire", (uint32_t)pc);
#endif
            valid._next = false;
            state_reg._next[0] = State{};
            state_reg._next[0].valid = false;
            state_reg._next[1] = exe_state_comb_func();
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            alu_result_reg._next = 0;
            sbi_ret_a1_valid_reg._next = sbi_writes_a1_comb_func();
            sbi_ret_a1_reg._next = sbi_ret_value_comb_func();
            debug_branch_target_reg._next = pc;
            debug_branch_taken_reg._next = false;
        }
        else
        if (memory_wait_comb_func()) {
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("memory-wait", (uint32_t)pc);
#endif
            interrupt_entry_guard_reg._next = false;
            valid._next = valid;
            state_reg._next = state_reg;
            predicted_next_reg._next = predicted_next_reg;
            fallthrough_reg._next = fallthrough_reg;
            predicted_taken_reg._next = predicted_taken_reg;
            alu_result_reg._next = alu_result_reg;
            if (wb_mem.load_ready_out() && state_reg[1].rd != 0 && state_reg[0].valid) {
                if (state_reg[0].rs1 == state_reg[1].rd) {
                    state_reg._next[0].rs1_val = wb_mem.load_result_out();
                }
                if (state_reg[0].rs2 == state_reg[1].rd) {
                    state_reg._next[0].rs2_val = wb_mem.load_result_out();
                }
            }
            debug_branch_target_reg._next = debug_branch_target_reg;
            debug_branch_taken_reg._next = debug_branch_taken_reg;
        }
        else {
            interrupt_entry_guard_reg._next = false;
            // Hold PC by default while the front end is waiting for a valid
            // translated/cacheable fetch. Redirects below override this.
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("normal-hold", (uint32_t)pc);
#endif
            if (fetch_valid_comb_func() && !stall_comb_func()) {
                pc._next = decode_fallthrough_comb_func();
#ifndef SYNTHESIS
                trace_pc_write("fetch-fallthrough", decode_fallthrough_comb_func());
#endif
            }
            if (decode_branch_valid_comb_func()) {
                pc._next = bp.predict_next_out();
#ifndef SYNTHESIS
                trace_pc_write("decode-predict", (uint32_t)bp.predict_next_out());
#endif
            }
            if (branch_mispredict_comb_func()) {
#ifndef SYNTHESIS
                if (std::getenv("TRIBE_TRACE_BAD_BRANCH") != nullptr) {
                    uint32_t target = branch_actual_next_comb_func();
                    if (target < 0x10000u || (target >= 0x80000000u && target < 0x80001000u)) {
                        std::print("trace-pc-select cycle={} state_pc={:08x} br_op={} rs1={:08x} imm={:08x} fallthrough={:08x} target={:08x} predicted={:08x} valid={}\n",
                            _system_clock,
                            (uint32_t)state_reg[0].pc,
                            (uint32_t)state_reg[0].br_op,
                            (uint32_t)state_reg[0].rs1_val,
                            (uint32_t)state_reg[0].imm,
                            (uint32_t)fallthrough_reg[0],
                            target,
                            (uint32_t)predicted_next_reg[0],
                            (bool)state_reg[0].valid);
                    }
                }
#endif
                pc._next = branch_actual_next_comb_func();
#ifndef SYNTHESIS
                trace_pc_write("execute-redirect", branch_actual_next_comb_func());
#endif
            }

            valid._next = !decode_indirect_branch_valid_comb_func();

            if (hazard_stall_comb_func()) {
                state_reg._next[0] = State{};
                state_reg._next[0].valid = false;
                predicted_next_reg._next[0] = pc;
                fallthrough_reg._next[0] = pc;
                predicted_taken_reg._next[0] = false;
            }
            else {
                if (fetch_valid_comb_func()) {
                    state_reg._next[0] = dec.state_out();
                    state_reg._next[0].valid = dec.instr_valid_in() && !branch_stall_comb_func() && !branch_flush_comb_func();
                    predicted_next_reg._next[0] = decode_branch_valid_comb_func() ? (uint32_t)bp.predict_next_out() : decode_fallthrough_comb_func();
                    fallthrough_reg._next[0] = decode_fallthrough_comb_func();
                    predicted_taken_reg._next[0] = decode_branch_valid_comb_func() && bp.predict_taken_out();
                    forward();
                }
                else {
                    state_reg._next[0] = State{};
                    state_reg._next[0].valid = false;
                    predicted_next_reg._next[0] = pc;
                    fallthrough_reg._next[0] = pc;
                    predicted_taken_reg._next[0] = false;
                }
            }
            state_reg._next[1] = state_reg[0];
            predicted_next_reg._next[1] = predicted_next_reg[0];
            fallthrough_reg._next[1] = fallthrough_reg[0];
            predicted_taken_reg._next[1] = predicted_taken_reg[0];
            alu_result_reg._next =
#ifdef ENABLE_ZICSR
                state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() :
#endif
#ifdef ENABLE_RV32IA
                (state_reg[0].amo_op == Amo::SC_W ? exe_mem.atomic_sc_result_out() : exe.alu_result_out());
#else
                 exe.alu_result_out();
#endif
            debug_branch_target_reg._next = exe.branch_target_out();
            debug_branch_taken_reg._next = exe.branch_taken_out();
        }

        regs._work(reset);
        dec._work(reset);
        exe._work(reset);
        exe_mem._work(reset);
        wb._work(reset);
        wb_mem._work(reset);
#ifdef ENABLE_ZICSR
        csr._work(reset);
#endif
#ifdef ENABLE_MMU_TLB
        immu._work(reset);
        dmmu._work(reset);
#endif
        icache._work(reset);
        dcache._work(reset);
        l2cache._work(reset);
        bp._work(reset);

        if (state_reg[0].valid &&
            (state_reg[0].sys_op == Sys::FENCEI || state_reg[0].sys_op == Sys::SFENCE_VMA) &&
            !memory_wait_comb_func()) {
            icache_invalidate_issued_reg._next = true;
        }
        else if (!state_reg[0].valid ||
                 (state_reg[0].sys_op != Sys::FENCEI && state_reg[0].sys_op != Sys::SFENCE_VMA)) {
            icache_invalidate_issued_reg._next = false;
        }

        if (reset) {
            state_reg._next[0].valid = 0;
            state_reg._next[1].valid = 0;
            pc._next = reset_pc_in();
#ifndef SYNTHESIS
            trace_pc_write("reset", (uint32_t)reset_pc_in());
#endif
            valid.clr();
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            output_write_active_reg.clr();
            interrupt_entry_guard_reg.clr();
            sbi_ret_a1_valid_reg.clr();
            sbi_ret_a1_reg.clr();
            icache_invalidate_issued_reg.clr();
        }
    }

    void _work_neg(bool reset)
    {
    }

    void debug()
    {
        State tmp;
        Zicsr instr = {{{icache.read_data_out()}}};
        instr.decode(tmp);

        std::print("({:d}/{:d}){} st[h{} b{} dc{} ic{} is{} ds{} ih{}]: [{:s}]{:08x}  rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({:d})ops:{:02d}/{}/{}/{} sys{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({:d}){:08x} => mem({:d}/{:d}@{:08x}){:08x}/{:01x} ({:d})wop({:x}),r({:d}){:08x}@{:02d}",
            (bool)valid, (bool)stall_comb_func(), pc,
            (bool)hazard_stall_comb_func(), (bool)branch_stall_comb_func(),
            (bool)dcache.busy_out(), (bool)icache.busy_out(),
            (uint32_t)icache.perf_out().state, (uint32_t)dcache.perf_out().state,
            (bool)icache.perf_out().hit,
            instr.mnemonic(), (instr.raw&3)==3?instr.raw:(instr.raw|0xFFFF0000),
            (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
            (bool)state_reg[0].valid, (uint8_t)state_reg[0].alu_op, (uint8_t)state_reg[0].mem_op, (uint8_t)state_reg[0].br_op, (uint8_t)state_reg[0].wb_op, (uint8_t)state_reg[0].sys_op,
            (int)state_reg[0].rs1, (int)state_reg[0].rs2, state_reg[0].rs1_val, state_reg[0].rs2_val, state_reg[0].imm, exe.alu_result_out(), (int)state_reg[0].rd,
            (bool)exe.branch_taken_out(), exe.branch_target_out(),
            (bool)exe_mem.mem_write_out(), (bool)exe_mem.mem_read_out(), exe_mem.mem_write_addr_out(), exe_mem.mem_write_data_out(), exe_mem.mem_write_mask_out(),
            (bool)state_reg[1].valid, (uint8_t)state_reg[1].wb_op, (bool)wb.regs_write_out(), wb.regs_data_out(), wb.regs_wr_id_out());

#ifndef SYNTHESIS
            // delayed by 1 to align EX to WB
        std::string interpret;
        if (state_reg[1].valid && state_reg[1].alu_op != Alu::ANONE) {
            interpret += std::format("r{:02d} r{:02d} {:5s}({:08x},{:08x}) ", (int)state_reg[1].rs1, (int)state_reg[1].rs2, AOPS[state_reg[1].alu_op],
                             (uint32_t)debug_alu_a_reg, (uint32_t)debug_alu_b_reg);
        }
        if (state_reg[1].valid && state_reg[1].br_op != Br::BNONE && debug_branch_taken_reg) {
            interpret += std::format("{}({:08x}) rd={:02d} ", BOPS[state_reg[1].br_op], (uint32_t)debug_branch_target_reg, (int)state_reg[1].rd);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::LOAD) {
            interpret += std::format("LOAD({:08x}) ", (uint32_t)alu_result_reg);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE) {
            interpret += std::format("STOR({:08x}) {:08x} from r{:02d} ", (uint32_t)alu_result_reg, state_reg[1].rs2_val, (int)state_reg[1].rs2);
        }
        if (state_reg[1].valid && state_reg[1].csr_op != Csr::CNONE) {
            interpret += std::format("{}({:03x}) ", COPS[state_reg[1].csr_op], (int)state_reg[1].csr_addr);
        }
        if (state_reg[1].valid && state_reg[1].wb_op != Wb::WNONE && wb.regs_write_out()) {
            interpret += std::format("wb {:08x} from {} to r{:02d} ", wb.regs_data_out(), WOPS[state_reg[1].wb_op], wb.regs_wr_id_out());
        }
            //
        std::print(": {}", interpret);
        debug_alu_a_reg._next = exe.debug_alu_a_out();
        debug_alu_b_reg._next = exe.debug_alu_b_out();
        debug_branch_target_reg = exe.branch_target_out();
#else
#endif
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        pc.strobe(checkpoint_fd);
        valid.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        predicted_next_reg.strobe(checkpoint_fd);
        fallthrough_reg.strobe(checkpoint_fd);
        predicted_taken_reg.strobe(checkpoint_fd);
        alu_result_reg.strobe(checkpoint_fd);
        debug_alu_a_reg.strobe(checkpoint_fd);
        debug_alu_b_reg.strobe(checkpoint_fd);
        debug_branch_target_reg.strobe(checkpoint_fd);
        debug_branch_taken_reg.strobe(checkpoint_fd);
        output_write_active_reg.strobe(checkpoint_fd);
        interrupt_entry_guard_reg.strobe(checkpoint_fd);
        sbi_ret_a1_valid_reg.strobe(checkpoint_fd);
        sbi_ret_a1_reg.strobe(checkpoint_fd);
        icache_invalidate_issued_reg.strobe(checkpoint_fd);

        regs._strobe(checkpoint_fd);
        exe._strobe(checkpoint_fd);
        exe_mem._strobe(checkpoint_fd);
        wb._strobe(checkpoint_fd);
        wb_mem._strobe(checkpoint_fd);
#ifdef ENABLE_ZICSR
        csr._strobe(checkpoint_fd);
#endif
#ifdef ENABLE_MMU_TLB
        immu._strobe(checkpoint_fd);
        dmmu._strobe(checkpoint_fd);
#endif
        icache._strobe(checkpoint_fd);
        dcache._strobe(checkpoint_fd);
        bp._strobe(checkpoint_fd);
        l2cache._strobe(checkpoint_fd);
    }


};
