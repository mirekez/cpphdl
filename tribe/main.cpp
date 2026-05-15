#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

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
#include "devices/Accelerator.cpp"
#include "cache/L1Cache.h"
#include "cache/L2Cache.h"

#include <cstdlib>
#include <vector>

// system configuration for cpp
static constexpr size_t DEFAULT_RAM_SIZE = 32768;
#ifndef TRIBE_RAM_BYTES_CONFIG
#define TRIBE_RAM_BYTES_CONFIG (448 * 1024)
#endif
#ifndef TRIBE_IO_REGION_SIZE_CONFIG
#define TRIBE_IO_REGION_SIZE_CONFIG (64 * 1024)
#endif
static constexpr size_t TRIBE_RAM_BYTES = TRIBE_RAM_BYTES_CONFIG;
static constexpr size_t TRIBE_IO_REGION_SIZE = TRIBE_IO_REGION_SIZE_CONFIG;
static constexpr size_t MAX_RAM_SIZE = TRIBE_RAM_BYTES + TRIBE_IO_REGION_SIZE;
static constexpr size_t TRIBE_MEM_REGION0_SIZE = TRIBE_RAM_BYTES / 2;
static constexpr size_t TRIBE_MEM_REGION1_SIZE = TRIBE_RAM_BYTES / 4;
static constexpr size_t TRIBE_MEM_REGION2_SIZE = TRIBE_RAM_BYTES - TRIBE_MEM_REGION0_SIZE - TRIBE_MEM_REGION1_SIZE;
#define L2_MEM_PORTS 4

#define L2_CACHE_ADDR_BITS cpphdl::clog2(MAX_RAM_SIZE)

long sys_clock = -1;

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
    _PORT(bool)      debug_fetch_valid_out;
    _PORT(bool)      debug_memory_wait_out;
    _PORT(bool)      debug_wb_load_ready_out;
    _PORT(bool)      debug_wb_mem_wait_out;
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
    _PORT(uint32_t)  reset_pc_in;
    _PORT(uint32_t)  boot_hartid_in;
    _PORT(uint32_t)  boot_dtb_addr_in;
    _PORT(u<2>)      boot_priv_in;
    _PORT(uint32_t)  memory_base_in;
    _PORT(uint32_t)  memory_size_in;
    _PORT(uint32_t)  mem_region_size_in[L2_MEM_PORTS];
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
    _PORT(bool)      clint_msip_in;
    _PORT(bool)      clint_mtip_in;
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
            _ASSIGN((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM) ? (uint32_t)dmmu.paddr_out() : (uint32_t)alu_result_reg);
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
        irq.__inst_name = __inst_name + "/irq";
        irq._assign();
#endif
        csr.state_in       = _ASSIGN_COMB( csr_state_comb_func() );
        csr.trap_check_state_in = _ASSIGN_REG(state_reg[0]);
        csr.reset_priv_in = boot_priv_in;
#ifdef ENABLE_ISR
        csr.interrupt_valid_in = irq.interrupt_valid_out;
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
        regs.reset_x10_in = boot_hartid_in;
        regs.reset_x11_in = boot_dtb_addr_in;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = _ASSIGN( state_reg[1].valid && exe_mem.mem_read_out() && !dcache.busy_out()
#ifdef ENABLE_MMU_TLB
            && !dmmu.busy_out() && !dmmu.fault_out()
#endif
            );
        dcache.write_in = _ASSIGN( state_reg[1].valid && exe_mem.mem_write_out() && !dcache.busy_out()
#ifdef ENABLE_MMU_TLB
            && !dmmu.busy_out() && !dmmu.fault_out()
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
        dcache.invalidate_in = _ASSIGN(false);
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
        debug_fetch_valid_out = _ASSIGN_COMB(fetch_valid_comb_func());
        debug_memory_wait_out = _ASSIGN_COMB(memory_wait_comb_func());
        debug_wb_load_ready_out = wb_mem.load_ready_out;
        debug_wb_mem_wait_out = _ASSIGN(state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && !wb_mem.load_ready_out());
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

    // Hold decode/execute when a pending load, split access, or atomic op would be observed too early.
    _LAZY_COMB(hazard_stall_comb, bool)
        hazard_stall_comb = false;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex hazard
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
#endif

    // Global pipeline memory wait, including split accesses, atomics, cache waits, and page-table walks.
    _LAZY_COMB(memory_wait_comb, bool)
        memory_wait_comb = dcache.busy_out() ||
            exe_mem.mem_split_busy_out() ||
#ifdef ENABLE_RV32IA
            exe_mem.atomic_busy_out() ||
#endif
#ifdef ENABLE_MMU_TLB
            immu.busy_out() ||
            dmmu.busy_out() ||
#endif
            (dcache.mem_read_out() && l2cache.d_wait_out()) ||
            ((exe_mem.mem_write_out() || (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE)) && l2cache.d_wait_out()) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
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

    // Legacy SBI set_timer ECALL is handled locally by programming CLINT mtimecmp.
    _LAZY_COMB(sbi_legacy_ecall_comb, bool)
        return sbi_legacy_ecall_comb = state_reg[0].valid &&
            state_reg[0].sys_op == Sys::ECALL &&
            csr.priv_out() == (u<2>)1 &&
            !memory_wait_comb_func();
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
        if (reg_id == 17) {
            return regs.x17_out();
        }
        return 0;
    }

    // Single-hart Linux still emits legacy remote fence SBI calls during VM changes; they are no-ops here.
    _LAZY_COMB(sbi_noop_comb, bool)
        uint32_t ext;
        ext = sbi_arg_value(17);
        return sbi_noop_comb = sbi_legacy_ecall_comb_func() && (ext == 5 || ext == 6 || ext == 7);
    }

    _LAZY_COMB(sbi_set_timer_comb, bool)
        return sbi_set_timer_comb = sbi_legacy_ecall_comb_func() &&
            sbi_arg_value(17) == 0 &&
            !memory_wait_comb_func();
    }

    // All locally handled legacy SBI calls retire as successful calls with a0=0.
    _LAZY_COMB(sbi_handled_comb, bool)
        return sbi_handled_comb = sbi_set_timer_comb_func() || sbi_noop_comb_func();
    }

    // Low word of the SBI timer value is passed in a0 on RV32.
    _LAZY_COMB(sbi_timer_lo_comb, uint32_t)
        return sbi_timer_lo_comb = sbi_arg_value(10);
    }

    // High word of the SBI timer value is passed in a1 on RV32.
    _LAZY_COMB(sbi_timer_hi_comb, uint32_t)
        return sbi_timer_hi_comb = sbi_arg_value(11);
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
             irq.interrupt_valid_out() ||
#endif
             state_reg[0].sys_op == Sys::ECALL ||
             state_reg[0].sys_op == Sys::EBREAK ||
             state_reg[0].sys_op == Sys::TRAP ||
             state_reg[0].trap_op != Trap::TNONE ||
             csr.illegal_trap_out())) {
            exe_state_comb.rs1_val = csr.trap_vector_out();
            exe_state_comb.imm = 0;
            exe_state_comb.br_op = Br::JR;
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
            irq.interrupt_valid_out() ||
#endif
            csr.illegal_trap_out()) {
            csr_state_comb = state_reg[0];
#ifdef ENABLE_ISR
            if (irq.interrupt_valid_out()) {
                csr_state_comb.imm = 0;
            }
#endif
            csr_state_comb.sys_op = Sys::TRAP;
            csr_state_comb.trap_op =
#ifdef ENABLE_ISR
                irq.interrupt_valid_out() ? Trap::TNONE :
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
            !memory_wait_comb_func();
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
        const bool trace_pc_write_all = std::getenv("TRIBE_TRACE_PC_WRITE_ALL") != nullptr;
        auto trace_pc_write = [&](const char* reason, uint32_t next_pc) {
            if (trace_pc_write_from_env == nullptr) {
                return;
            }
            const long long from_cycle = std::strtoll(trace_pc_write_from_env, nullptr, 0);
            if (sys_clock < from_cycle) {
                return;
            }
            const uint32_t old_pc = (uint32_t)pc;
            if (!trace_pc_write_all && old_pc >= 0x10000u && next_pc >= 0x10000u) {
                return;
            }
            std::print("trace-pc-write cycle={} reason={} pc={:08x} next={:08x} valid={} fetch_valid={} memwait={} stall={} branch_mispredict={} branch_target={:08x} predicted={:08x} state0_valid={} state0_pc={:08x} state0_sys={} state0_trap={} state0_br={} state1_valid={} state1_pc={:08x}",
                sys_clock,
                reason,
                old_pc,
                next_pc,
                (bool)valid,
                (bool)fetch_valid_comb_func(),
                (bool)memory_wait_comb_func(),
                (bool)stall_comb_func(),
                (bool)branch_mispredict_comb_func(),
                (uint32_t)branch_actual_next_comb_func(),
                (uint32_t)predicted_next_reg[0],
                (bool)state_reg[0].valid,
                (uint32_t)state_reg[0].pc,
                (uint32_t)state_reg[0].sys_op,
                (uint32_t)state_reg[0].trap_op,
                (uint32_t)state_reg[0].br_op,
                (bool)state_reg[1].valid,
                (uint32_t)state_reg[1].pc);
#ifdef ENABLE_MMU_TLB
            std::print(" immu_fault={} immu_busy={} immu_paddr={:08x} dmmu_fault={} dmmu_active={}",
                (bool)immu.fault_out(),
                (bool)immu.busy_out(),
                (uint32_t)immu.paddr_out(),
                (bool)dmmu.fault_out(),
                (bool)dmmu_active_fault_comb_func());
#endif
#ifdef ENABLE_ZICSR
            std::print(" priv={} stvec={:08x} sepc={:08x} scause={:08x} stval={:08x} mepc={:08x} mtvec={:08x}",
                (uint32_t)csr.priv_out(),
                (uint32_t)csr.stvec_out(),
                (uint32_t)csr.sepc_out(),
                (uint32_t)csr.scause_out(),
                (uint32_t)csr.stval_out(),
                (uint32_t)csr.mepc_out(),
                (uint32_t)csr.mtvec_out());
#endif
            std::print("\n");
        };
#endif
        if (debugen_in && !reset) {
            debug();
        }

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
        }
        else
        if (!reset && state_reg[0].valid &&
            !sbi_handled_comb_func() &&
            (
#ifdef ENABLE_ISR
             irq.interrupt_valid_out() ||
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
        }
        else
#endif
        if (memory_wait_comb_func()) {
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("memory-wait", (uint32_t)pc);
#endif
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
                            sys_clock,
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

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

#include "Ram.h"
#include "Axi4Ram.h"

#include <tuple>
#include <utility>

static inline uint64_t tribe_runtime_tick()
{
#if defined(__i386__) || defined(__x86_64__)
    unsigned aux;
    return __rdtscp(&aux);
#else
    return (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
static TribePerf verilator_tribe_perf(uint64_t bits)
{
    uint64_t storage = bits;
    return *reinterpret_cast<TribePerf*>(&storage);
}

template<size_t WORDS>
static logic<WORDS * 32> verilator_wide_to_logic(const VlWide<WORDS>& bits)
{
    logic<WORDS * 32> out = 0;
    memcpy(out.bytes, bits.m_storage, sizeof(out.bytes));
    return out;
}

static logic<64> verilator_wide_to_logic(const QData& bits)
{
    return (uint64_t)bits;
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH == WORDS * 32);
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

static void verilator_logic_to_wide(QData& out, const logic<64>& bits)
{
    out = (uint64_t)bits;
}

#define PORT_VALUE(port) (port)
#define PERF_VALUE(port) verilator_tribe_perf((uint64_t)(port))
#else
#define PORT_VALUE(port) (port())
#define PERF_VALUE(port) (port())
#endif

class TestTribe : public Module
{
    static constexpr size_t AXI_RAM0_DEPTH = TRIBE_MEM_REGION0_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    static constexpr size_t AXI_RAM1_DEPTH = TRIBE_MEM_REGION1_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    static constexpr size_t AXI_RAM2_DEPTH = TRIBE_MEM_REGION2_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM0_DEPTH> mem0;
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM1_DEPTH> mem1;
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM2_DEPTH> mem2;
    Axi4RegionMux<3,clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> iospace;
    NS16550A<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> uart;
    CLINT<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> clint;
    Accelerator<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> accelerator;

#ifdef VERILATOR
    VERILATOR_MODEL tribe;
#else
    Tribe tribe;
#endif

    bool error;

    uint64_t perf_clocks = 0;
    uint64_t perf_stall = 0;
    uint64_t perf_hazard = 0;
    uint64_t perf_dcache_wait = 0;
    uint64_t perf_icache_wait = 0;
    uint64_t perf_branch = 0;
    uint64_t perf_icache_issue_wait_cycles = 0;
    uint64_t perf_icache_lookup_wait_cycles = 0;
    uint64_t perf_icache_refill_wait_cycles = 0;
    uint64_t perf_icache_init_wait_cycles = 0;
    uint64_t perf_icache_hit_lookup_cycles = 0;
    uint64_t runtime_strobe_ticks = 0;
    uint64_t runtime_tribe_strobe_ticks = 0;
    uint64_t runtime_checkpoint_ticks = 0;
    uint64_t runtime_perf_ticks = 0;
    uint64_t runtime_work_ticks = 0;
    uint64_t runtime_tribe_work_ticks = 0;
    uint64_t runtime_uart_ticks = 0;
    uint64_t runtime_trace_ticks = 0;
    uint64_t runtime_negedge_ticks = 0;
    uint64_t runtime_total_ticks = 0;
    uint32_t tohost_addr = 0;
    uint32_t tohost_value = 0;
    uint32_t reset_pc = 0;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    bool tohost_done = false;

    struct Elf32Header
    {
        unsigned char ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint32_t entry;
        uint32_t phoff;
        uint32_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    } __PACKED;

    struct Elf32ProgramHeader
    {
        uint32_t type;
        uint32_t offset;
        uint32_t vaddr;
        uint32_t paddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
        uint32_t align;
    } __PACKED;

    bool load_elf(FILE* fbin, std::vector<uint32_t>& ram, size_t& read_bytes, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t& entry, bool elf_phys_override, uint32_t elf_phys_offset)
    {
        static constexpr uint32_t PT_LOAD = 1;
        Elf32Header ehdr = {};
        fseek(fbin, 0, SEEK_SET);
        if (fread(&ehdr, 1, sizeof(ehdr), fbin) != sizeof(ehdr)) {
            return false;
        }
        if (ehdr.ident[0] != 0x7f || ehdr.ident[1] != 'E' || ehdr.ident[2] != 'L' || ehdr.ident[3] != 'F' ||
            ehdr.ident[4] != 1 || ehdr.ident[5] != 1 || ehdr.phentsize != sizeof(Elf32ProgramHeader)) {
            return false;
        }
        entry = elf_phys_override ? ehdr.entry + elf_phys_offset : ehdr.entry;

        for (uint16_t i = 0; i < ehdr.phnum; ++i) {
            Elf32ProgramHeader phdr = {};
            fseek(fbin, ehdr.phoff + i * sizeof(phdr), SEEK_SET);
            if (fread(&phdr, 1, sizeof(phdr), fbin) != sizeof(phdr)) {
                return false;
            }
            if (phdr.type != PT_LOAD || phdr.filesz == 0) {
                continue;
            }

            const uint32_t phys = elf_phys_override ? (phdr.vaddr + elf_phys_offset) : (phdr.paddr ? phdr.paddr : phdr.vaddr);
            if (phys < mem_base || phys - mem_base + phdr.filesz > mem_size_bytes) {
                std::print("ELF segment outside test RAM window: paddr={:08x}, mem_base={:08x}, size={}\n", phys, mem_base, phdr.filesz);
                return false;
            }
            const uint32_t base = phys - mem_base;

            fseek(fbin, phdr.offset, SEEK_SET);
            for (uint32_t byte = 0; byte < phdr.filesz; ++byte) {
                int c = fgetc(fbin);
                if (c == EOF) {
                    return false;
                }
                const uint32_t addr = base + byte;
                const uint32_t shift = (addr & 3u) * 8u;
                ram[addr / 4] = (ram[addr / 4] & ~(0xffu << shift)) | (uint32_t(uint8_t(c)) << shift);
            }
            read_bytes += phdr.filesz;
        }
        return read_bytes != 0;
    }

    bool load_blob(const std::string& filename, std::vector<uint32_t>& ram, uint32_t addr, uint32_t mem_base, uint32_t mem_size_bytes, size_t& read_bytes)
    {
        FILE* f = fopen(filename.c_str(), "rb");
        if (!f) {
            std::print("can't open blob '{}'\n", filename);
            return false;
        }
        if (addr < mem_base) {
            std::print("blob outside test RAM window: addr={:08x}, mem_base={:08x}\n", addr, mem_base);
            fclose(f);
            return false;
        }
        uint32_t offset = addr - mem_base;
        if (offset >= mem_size_bytes) {
            std::print("blob outside test RAM window: addr={:08x}, mem_base={:08x}, mem_size={}\n", addr, mem_base, mem_size_bytes);
            fclose(f);
            return false;
        }
        uint32_t byte = offset;
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (byte >= mem_size_bytes) {
                std::print("blob '{}' does not fit RAM window\n", filename);
                fclose(f);
                return false;
            }
            const uint32_t shift = (byte & 3u) * 8u;
            ram[byte / 4] = (ram[byte / 4] & ~(0xffu << shift)) | (uint32_t(uint8_t(c)) << shift);
            ++byte;
            ++read_bytes;
        }
        fclose(f);
        return true;
    }

    bool patch_word(std::vector<uint32_t>& ram, uint32_t addr, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t value)
    {
        if (addr < mem_base || addr - mem_base + 4 > mem_size_bytes) {
            std::print("patch outside test RAM window: addr={:08x}, mem_base={:08x}\n", addr, mem_base);
            return false;
        }
        ram[(addr - mem_base) / 4] = value;
        return true;
    }

    bool patch_linux_earlycon_mapbase(std::vector<uint32_t>& ram, uint32_t mem_base, uint32_t mem_size_bytes)
    {
        static constexpr std::array<uint32_t, 5> SEARCH_WORDS = {
            0x00050913u, // mv s2,a0
            0x00079863u, // bnez a5,...
            0x00c52783u, // lw a5,12(a0)
            0xfed00513u, // li a0,-19
            0x12078863u, // beqz a5,...
        };
        static constexpr std::array<uint32_t, 5> PATCH_WORDS = {
            0x00050913u, // mv s2,a0          ; preserve early_serial8250_setup's device pointer
            0x0d852783u, // lw a5,216(a0)     ; fallback to early_console_dev.port.mapbase
            0x00f52823u, // sw a5,16(a0)      ; make port.membase usable when early ioremap failed
            0x00079663u, // bnez a5,+12       ; keep using the configured mapbase when present
            0xfed00513u, // li a0,-19
        };
        uint32_t patch_phys = 0;
        bool found = false;
        const size_t words = mem_size_bytes / 4;
        for (size_t pos = 0; pos + SEARCH_WORDS.size() <= words; ++pos) {
            bool match = true;
            for (size_t i = 0; i < SEARCH_WORDS.size(); ++i) {
                if (ram[pos + i] != SEARCH_WORDS[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                patch_phys = mem_base + (uint32_t)(pos * 4);
                found = true;
                break;
            }
        }
        if (!found) {
            std::print("can't find Linux early 8250 membase fallback patch site\n");
            return false;
        }
        for (size_t i = 0; i < PATCH_WORDS.size(); ++i) {
            if (!patch_word(ram, patch_phys + (uint32_t)i * 4u, mem_base, mem_size_bytes, PATCH_WORDS[i])) {
                return false;
            }
        }
        std::print("Patched Linux early 8250 membase fallback at {:08x}\n", patch_phys);
        return true;
    }

    uint8_t ram_byte(const std::vector<uint32_t>& ram, uint32_t byte_addr)
    {
        return (uint8_t)((ram[byte_addr / 4] >> ((byte_addr & 3u) * 8u)) & 0xffu);
    }

    void set_ram_byte(std::vector<uint32_t>& ram, uint32_t byte_addr, uint8_t value)
    {
        const uint32_t shift = (byte_addr & 3u) * 8u;
        ram[byte_addr / 4] = (ram[byte_addr / 4] & ~(0xffu << shift)) | ((uint32_t)value << shift);
    }

    bool patch_dtb_bootargs(std::vector<uint32_t>& ram, uint32_t dtb_addr, uint32_t dtb_bytes,
                            uint32_t mem_base, uint32_t mem_size_bytes, const std::string& bootargs)
    {
        if (dtb_addr < mem_base || dtb_addr - mem_base + dtb_bytes > mem_size_bytes) {
            std::print("DTB bootargs patch outside test RAM window\n");
            return false;
        }

        static constexpr std::string_view prefix = "console=";
        const uint32_t base = dtb_addr - mem_base;
        for (uint32_t off = 0; off + prefix.size() < dtb_bytes; ++off) {
            bool match = true;
            for (uint32_t i = 0; i < prefix.size(); ++i) {
                if (ram_byte(ram, base + off + i) != (uint8_t)prefix[i]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            uint32_t old_len = 0;
            while (off + old_len < dtb_bytes && ram_byte(ram, base + off + old_len) != 0) {
                ++old_len;
            }
            if (bootargs.size() > old_len) {
                std::print("new --bootargs is longer than DTB bootargs slot ({} > {})\n", bootargs.size(), old_len);
                return false;
            }
            for (uint32_t i = 0; i < old_len; ++i) {
                set_ram_byte(ram, base + off + i, i < bootargs.size() ? (uint8_t)bootargs[i] : 0);
            }
            std::print("Patched DTB bootargs: {}\n", bootargs);
            return true;
        }

        std::print("can't find DTB bootargs string to patch\n");
        return false;
    }

//    size_t i;

public:

    bool      debugen_in;

    TestTribe(bool debug)
    {
        debugen_in = debug;
    }

    ~TestTribe()
    {
    }

    void _assign()
    {
        size_t i = 0;
	#ifndef VERILATOR
        tribe.debugen_in = debugen_in;
        tribe.reset_pc_in = _ASSIGN(reset_pc);
        tribe.boot_hartid_in = _ASSIGN(boot_hartid);
        tribe.boot_dtb_addr_in = _ASSIGN(boot_dtb_addr);
        tribe.boot_priv_in = _ASSIGN((u<2>)boot_priv);
        tribe.memory_base_in = _ASSIGN(start_mem_addr);
        tribe.memory_size_in = _ASSIGN((uint32_t)MAX_RAM_SIZE);
        tribe.mem_region_size_in[0] = _ASSIGN((uint32_t)TRIBE_MEM_REGION0_SIZE);
        tribe.mem_region_size_in[1] = _ASSIGN((uint32_t)TRIBE_MEM_REGION1_SIZE);
        tribe.mem_region_size_in[2] = _ASSIGN((uint32_t)TRIBE_MEM_REGION2_SIZE);
        tribe.mem_region_size_in[3] = _ASSIGN((uint32_t)TRIBE_IO_REGION_SIZE);
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_in[i].awvalid_in = _ASSIGN(false);
            tribe.axi_in[i].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].awid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].wvalid_in = _ASSIGN(false);
            tribe.axi_in[i].wdata_in = _ASSIGN((logic<TRIBE_L2_AXI_WIDTH>)0);
            tribe.axi_in[i].wlast_in = _ASSIGN(false);
            tribe.axi_in[i].bready_in = _ASSIGN(false);
            tribe.axi_in[i].arvalid_in = _ASSIGN(false);
            tribe.axi_in[i].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].arid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].rready_in = _ASSIGN(false);
        }
        tribe.axi_in[0].awvalid_in = _ASSIGN(accelerator.dma_out.awvalid_in());
        tribe.axi_in[0].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.awaddr_in());
        tribe.axi_in[0].awid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.awid_in());
        tribe.axi_in[0].wvalid_in = _ASSIGN(accelerator.dma_out.wvalid_in());
        tribe.axi_in[0].wdata_in = _ASSIGN(accelerator.dma_out.wdata_in());
        tribe.axi_in[0].wlast_in = _ASSIGN(accelerator.dma_out.wlast_in());
        tribe.axi_in[0].bready_in = _ASSIGN(accelerator.dma_out.bready_in());
        tribe.axi_in[0].arvalid_in = _ASSIGN(accelerator.dma_out.arvalid_in());
        tribe.axi_in[0].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.araddr_in());
        tribe.axi_in[0].arid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.arid_in());
        tribe.axi_in[0].rready_in = _ASSIGN(accelerator.dma_out.rready_in());
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out;
        tribe.clint_mtip_in = clint.mtip_out;
#endif
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._assign();

        AXI4_DRIVER_FROM(mem0.axi_in, tribe.axi_out[0]);
        AXI4_DRIVER_FROM(mem1.axi_in, tribe.axi_out[1]);
        AXI4_DRIVER_FROM(mem2.axi_in, tribe.axi_out[2]);
        AXI4_DRIVER_FROM(iospace.slave_in, tribe.axi_out[3]);
        mem0.debugen_in = debugen_in;
        mem1.debugen_in = debugen_in;
        mem2.debugen_in = debugen_in;
        mem0.__inst_name = __inst_name + "/mem0";
        mem1.__inst_name = __inst_name + "/mem1";
        mem2.__inst_name = __inst_name + "/mem2";
        iospace.region_base_in[0] = _ASSIGN((uint32_t)0);
        iospace.region_size_in[0] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[1] = _ASSIGN((uint32_t)0x100);
        iospace.region_size_in[1] = _ASSIGN((uint32_t)0xC000);
        iospace.region_base_in[2] = _ASSIGN((uint32_t)0xC100);
        iospace.region_size_in[2] = _ASSIGN((uint32_t)0x1000);
        iospace.__inst_name = __inst_name + "/iospace";
        iospace._assign();
        AXI4_DRIVER_FROM(uart.axi_in, iospace.masters_out[0]);
        AXI4_DRIVER_FROM(clint.axi_in, iospace.masters_out[1]);
        AXI4_DRIVER_FROM(accelerator.axi_in, iospace.masters_out[2]);
        AXI4_RESPONDER_FROM(accelerator.dma_out, tribe.axi_in[0]);
        clint.set_mtimecmp_in = tribe.sbi_set_timer_out;
        clint.set_mtimecmp_lo_in = tribe.sbi_timer_lo_out;
        clint.set_mtimecmp_hi_in = tribe.sbi_timer_hi_out;
        uart.__inst_name = __inst_name + "/uart";
        clint.__inst_name = __inst_name + "/clint";
        accelerator.__inst_name = __inst_name + "/accelerator";
        mem0._assign();
        mem1._assign();
        mem2._assign();
        uart._assign();
        clint._assign();
        accelerator._assign();

        AXI4_RESPONDER_FROM(tribe.axi_out[0], mem0.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[1], mem1.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[2], mem2.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[0], uart.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[1], clint.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[2], accelerator.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[3], iospace.slave_in);
	#else  // connecting Verilator to CppHDL
        tribe.reset_pc_in = reset_pc;
        tribe.boot_hartid_in = boot_hartid;
        tribe.boot_dtb_addr_in = boot_dtb_addr;
        tribe.boot_priv_in = boot_priv;
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = MAX_RAM_SIZE;
        tribe.mem_region_size_in[0] = TRIBE_MEM_REGION0_SIZE;
        tribe.mem_region_size_in[1] = TRIBE_MEM_REGION1_SIZE;
        tribe.mem_region_size_in[2] = TRIBE_MEM_REGION2_SIZE;
        tribe.mem_region_size_in[3] = TRIBE_IO_REGION_SIZE;
        for (size_t i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_in___05Fawvalid_in[i] = false;
            tribe.axi_in___05Fawaddr_in[i] = 0;
            tribe.axi_in___05Fawid_in[i] = 0;
            tribe.axi_in___05Fwvalid_in[i] = false;
            verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[i], (logic<TRIBE_L2_AXI_WIDTH>)0);
            tribe.axi_in___05Fwlast_in[i] = false;
            tribe.axi_in___05Fbready_in[i] = false;
            tribe.axi_in___05Farvalid_in[i] = false;
            tribe.axi_in___05Faraddr_in[i] = 0;
            tribe.axi_in___05Farid_in[i] = 0;
            tribe.axi_in___05Frready_in[i] = false;
        }
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out();
        tribe.clint_mtip_in = clint.mtip_out();
#endif
        AXI4_DRIVER_FROM_VERILATOR(mem0.axi_in, tribe, 0, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR(mem1.axi_in, tribe, 1, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR(mem2.axi_in, tribe, 2, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR(iospace.slave_in, tribe, 3, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        mem0.debugen_in = debugen_in;
        mem1.debugen_in = debugen_in;
        mem2.debugen_in = debugen_in;
        mem0.__inst_name = __inst_name + "/mem0";
        mem1.__inst_name = __inst_name + "/mem1";
        mem2.__inst_name = __inst_name + "/mem2";
        iospace.region_base_in[0] = _ASSIGN((uint32_t)0);
        iospace.region_size_in[0] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[1] = _ASSIGN((uint32_t)0x100);
        iospace.region_size_in[1] = _ASSIGN((uint32_t)0xC000);
        iospace.region_base_in[2] = _ASSIGN((uint32_t)0xC100);
        iospace.region_size_in[2] = _ASSIGN((uint32_t)0x1000);
        iospace.__inst_name = __inst_name + "/iospace";
        iospace._assign();
        AXI4_DRIVER_FROM(uart.axi_in, iospace.masters_out[0]);
        AXI4_DRIVER_FROM(clint.axi_in, iospace.masters_out[1]);
        AXI4_DRIVER_FROM(accelerator.axi_in, iospace.masters_out[2]);
        clint.set_mtimecmp_in = _ASSIGN((bool)tribe.sbi_set_timer_out);
        clint.set_mtimecmp_lo_in = _ASSIGN((uint32_t)tribe.sbi_timer_lo_out);
        clint.set_mtimecmp_hi_in = _ASSIGN((uint32_t)tribe.sbi_timer_hi_out);
        accelerator.dma_out.awready_out = _ASSIGN((bool)tribe.axi_in___05Fawready_out[0]);
        accelerator.dma_out.wready_out = _ASSIGN((bool)tribe.axi_in___05Fwready_out[0]);
        accelerator.dma_out.bvalid_out = _ASSIGN((bool)tribe.axi_in___05Fbvalid_out[0]);
        accelerator.dma_out.bid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Fbid_out[0]);
        accelerator.dma_out.arready_out = _ASSIGN((bool)tribe.axi_in___05Farready_out[0]);
        accelerator.dma_out.rvalid_out = _ASSIGN((bool)tribe.axi_in___05Frvalid_out[0]);
        accelerator.dma_out.rdata_out = _ASSIGN(verilator_wide_to_logic(tribe.axi_in___05Frdata_out[0]));
        accelerator.dma_out.rlast_out = _ASSIGN((bool)tribe.axi_in___05Frlast_out[0]);
        accelerator.dma_out.rid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Frid_out[0]);
        uart.__inst_name = __inst_name + "/uart";
        clint.__inst_name = __inst_name + "/clint";
        accelerator.__inst_name = __inst_name + "/accelerator";
        mem0._assign();
        mem1._assign();
        mem2._assign();
        uart._assign();
        clint._assign();
        accelerator._assign();
        AXI4_RESPONDER_FROM(iospace.masters_out[0], uart.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[1], clint.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[2], accelerator.axi_in);
#endif
    }

    void _work(bool reset)
    {
#ifndef VERILATOR
        uint64_t tribe_work_time_start = tribe_runtime_tick();
        tribe._work(reset);
        runtime_tribe_work_ticks += tribe_runtime_tick() - tribe_work_time_start;
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;
        tribe.reset_pc_in = reset_pc;
        tribe.boot_hartid_in = boot_hartid;
        tribe.boot_dtb_addr_in = boot_dtb_addr;
        tribe.boot_priv_in = boot_priv;
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = MAX_RAM_SIZE;
        tribe.mem_region_size_in[0] = TRIBE_MEM_REGION0_SIZE;
        tribe.mem_region_size_in[1] = TRIBE_MEM_REGION1_SIZE;
        tribe.mem_region_size_in[2] = TRIBE_MEM_REGION2_SIZE;
        tribe.mem_region_size_in[3] = TRIBE_IO_REGION_SIZE;
        tribe.axi_in___05Fawvalid_in[0] = accelerator.dma_out.awvalid_in();
        tribe.axi_in___05Fawaddr_in[0] = (uint32_t)accelerator.dma_out.awaddr_in();
        tribe.axi_in___05Fawid_in[0] = (uint32_t)accelerator.dma_out.awid_in();
        tribe.axi_in___05Fwvalid_in[0] = accelerator.dma_out.wvalid_in();
        verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[0], accelerator.dma_out.wdata_in());
        tribe.axi_in___05Fwlast_in[0] = accelerator.dma_out.wlast_in();
        tribe.axi_in___05Fbready_in[0] = accelerator.dma_out.bready_in();
        tribe.axi_in___05Farvalid_in[0] = accelerator.dma_out.arvalid_in();
        tribe.axi_in___05Faraddr_in[0] = (uint32_t)accelerator.dma_out.araddr_in();
        tribe.axi_in___05Farid_in[0] = (uint32_t)accelerator.dma_out.arid_in();
        tribe.axi_in___05Frready_in[0] = accelerator.dma_out.rready_in();
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out();
        tribe.clint_mtip_in = clint.mtip_out();
#endif

        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();
#endif
        mem0._work(reset);
        mem1._work(reset);
        mem2._work(reset);
        iospace._work(reset);
        uart._work(reset);
        clint._work(reset);
        accelerator._work(reset);
#ifdef VERILATOR
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem0.axi_in, 0);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem1.axi_in, 1);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem2.axi_in, 2);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, iospace.slave_in, 3);
        tribe.clk = 1;
        tribe.reset = reset;
        uint64_t tribe_work_time_start = tribe_runtime_tick();
        tribe.eval();  // eval of verilator should be in the end
        runtime_tribe_work_ticks += tribe_runtime_tick() - tribe_work_time_start;
#endif

        if (reset) {
            error = false;
            return;
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        checkpoint_value(checkpoint_fd, perf_clocks);
        checkpoint_value(checkpoint_fd, perf_stall);
        checkpoint_value(checkpoint_fd, perf_hazard);
        checkpoint_value(checkpoint_fd, perf_dcache_wait);
        checkpoint_value(checkpoint_fd, perf_icache_wait);
        checkpoint_value(checkpoint_fd, perf_branch);
        checkpoint_value(checkpoint_fd, perf_icache_issue_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_lookup_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_refill_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_init_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_hit_lookup_cycles);
        checkpoint_value(checkpoint_fd, tohost_addr);
        checkpoint_value(checkpoint_fd, tohost_value);
        checkpoint_value(checkpoint_fd, reset_pc);
        checkpoint_value(checkpoint_fd, boot_hartid);
        checkpoint_value(checkpoint_fd, boot_dtb_addr);
        checkpoint_value(checkpoint_fd, boot_priv);
        checkpoint_value(checkpoint_fd, start_mem_addr);
        checkpoint_value(checkpoint_fd, ram_size);
        checkpoint_value(checkpoint_fd, tohost_done);
        checkpoint_value(checkpoint_fd, sys_clock);
#ifndef VERILATOR
        uint64_t tribe_strobe_time_start = tribe_runtime_tick();
        tribe._strobe(checkpoint_fd);
        runtime_tribe_strobe_ticks += tribe_runtime_tick() - tribe_strobe_time_start;
#endif
        mem0._strobe(checkpoint_fd);  // we use these modules in Verilator test
        mem1._strobe(checkpoint_fd);
        mem2._strobe(checkpoint_fd);
        iospace._strobe(checkpoint_fd);
        uart._strobe(checkpoint_fd);
        clint._strobe(checkpoint_fd);
        accelerator._strobe(checkpoint_fd);
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
#else
        tribe._work_neg(reset);
#endif

        if (debugen_in) {
            printf("----------- %ld\n", sys_clock);
        }
    }

    void _strobe_neg()
    {
    }

    void perf_reset()
    {
        perf_clocks = 0;
        perf_stall = 0;
        perf_hazard = 0;
        perf_dcache_wait = 0;
        perf_icache_wait = 0;
        perf_branch = 0;
        perf_icache_issue_wait_cycles = 0;
        perf_icache_lookup_wait_cycles = 0;
        perf_icache_refill_wait_cycles = 0;
        perf_icache_init_wait_cycles = 0;
        perf_icache_hit_lookup_cycles = 0;
        runtime_strobe_ticks = 0;
        runtime_tribe_strobe_ticks = 0;
        runtime_checkpoint_ticks = 0;
        runtime_perf_ticks = 0;
        runtime_work_ticks = 0;
        runtime_tribe_work_ticks = 0;
        runtime_uart_ticks = 0;
        runtime_trace_ticks = 0;
        runtime_negedge_ticks = 0;
        runtime_total_ticks = 0;
    }

    void perf_sample()
    {
        auto perf = PERF_VALUE(tribe.perf_out);
        bool hazard = perf.hazard_stall;
        bool branch = perf.branch_stall;
        bool dcache_wait = perf.dcache_wait;
        bool icache_wait = perf.icache_wait;

        ++perf_clocks;
        perf_hazard += hazard;
        perf_branch += branch;
        perf_dcache_wait += dcache_wait;
        perf_icache_wait += icache_wait;
        perf_icache_issue_wait_cycles += perf.icache.issue_wait;
        perf_icache_lookup_wait_cycles += perf.icache.lookup_wait;
        perf_icache_refill_wait_cycles += perf.icache.refill_wait;
        perf_icache_init_wait_cycles += perf.icache.init_wait;
        perf_icache_hit_lookup_cycles += perf.icache.hit && perf.icache.lookup_wait;
        perf_stall += hazard || branch || dcache_wait || icache_wait;
    }

    void debug_perf_counters_print()
    {
        std::print(" perf[c{:04x} s{:04x} h{:04x} b{:04x} dc{:04x} ic{:04x} ii{:04x} il{:04x} ih{:04x} ir{:04x} in{:04x}]\n",
            (uint16_t)perf_clocks,
            (uint16_t)perf_stall,
            (uint16_t)perf_hazard,
            (uint16_t)perf_branch,
            (uint16_t)perf_dcache_wait,
            (uint16_t)perf_icache_wait,
            (uint16_t)perf_icache_issue_wait_cycles,
            (uint16_t)perf_icache_lookup_wait_cycles,
            (uint16_t)perf_icache_hit_lookup_cycles,
            (uint16_t)perf_icache_refill_wait_cycles,
            (uint16_t)perf_icache_init_wait_cycles);
    }

    void perf_print()
    {
        auto percent = [&](uint64_t value) {
            return perf_clocks ? (100.0 * (double)value / (double)perf_clocks) : 0.0;
        };
        auto runtime_part_percent = [&](uint64_t ticks) {
            return runtime_total_ticks ? (100.0 * (double)ticks / (double)runtime_total_ticks) : 0.0;
        };

        std::print("Performance: clocks={}, stalled={:.2f}% ({})"
                   ", hazards={:.2f}% ({})"
                   ", dcache_wait={:.2f}% ({})"
                   ", icache_wait={:.2f}% ({})"
                   ", branching={:.2f}% ({})\n",
            perf_clocks,
            percent(perf_stall), perf_stall,
            percent(perf_hazard), perf_hazard,
            percent(perf_dcache_wait), perf_dcache_wait,
            percent(perf_icache_wait), perf_icache_wait,
            percent(perf_branch), perf_branch);
        std::print("I-cache wait detail: issue={:.2f}% ({})"
                   ", lookup={:.2f}% ({})"
                   ", lookup_hit={:.2f}% ({})"
                   ", refill={:.2f}% ({})"
                   ", init={:.2f}% ({})\n",
            percent(perf_icache_issue_wait_cycles), perf_icache_issue_wait_cycles,
            percent(perf_icache_lookup_wait_cycles), perf_icache_lookup_wait_cycles,
            percent(perf_icache_hit_lookup_cycles), perf_icache_hit_lookup_cycles,
            percent(perf_icache_refill_wait_cycles), perf_icache_refill_wait_cycles,
            percent(perf_icache_init_wait_cycles), perf_icache_init_wait_cycles);
        std::print("Runtime detail: checkpoint={:.2f}% strobe={:.2f}% tribe_strobe={:.2f}% perf={:.2f}% work={:.2f}% tribe_work={:.2f}% uart={:.2f}% trace_probe={:.2f}% negedge={:.2f}%\n",
            runtime_part_percent(runtime_checkpoint_ticks),
            runtime_part_percent(runtime_strobe_ticks),
            runtime_part_percent(runtime_tribe_strobe_ticks),
            runtime_part_percent(runtime_perf_ticks),
            runtime_part_percent(runtime_work_ticks),
            runtime_part_percent(runtime_tribe_work_ticks),
            runtime_part_percent(runtime_uart_ticks),
            runtime_part_percent(runtime_trace_ticks),
            runtime_part_percent(runtime_negedge_ticks));
    }

    bool run(std::string filename, size_t start_offset, std::string expected_log = "rv32i.log", int max_cycles = 2000000, uint32_t tohost = 0, uint32_t mem_base = 0, uint32_t ram_words = DEFAULT_RAM_SIZE, bool raw_program = false, uint32_t boot_hartid_arg = 0, uint32_t boot_dtb_addr_arg = 0, uint32_t boot_priv_arg = 3, bool elf_phys_override = false, uint32_t elf_phys_offset = 0, const std::string& dtb_file = "", bool linux_earlycon_mapbase = false, const std::string& initramfs_file = "", uint32_t initramfs_addr = 0, const std::string& checkpoint_load_file = "", const std::string& checkpoint_save_file = "", uint64_t checkpoint_save_cycle = 0, bool append_output = false, const std::string& bootargs = "", bool checkpoint_save_only_success = false, const std::string& expected_output_contains = "")
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTribe...");
#else
        std::print("CppHDL TestTribe...");
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", append_output ? "ab" : "wb");
        fclose(out);
        tohost_addr = tohost;
        tohost_value = 0;
        tohost_done = false;
        start_mem_addr = mem_base;
        reset_pc = mem_base;
        boot_hartid = boot_hartid_arg;
        boot_dtb_addr = boot_dtb_addr_arg;
        boot_priv = boot_priv_arg;
        ram_size = ram_words;
        if (ram_size == 0 || ram_size > TRIBE_RAM_BYTES/4) {
            std::print("invalid --ram-size {}; supported range is 1..{} words\n", ram_size, TRIBE_RAM_BYTES/4);
            return false;
        }

        /////////////// read program to memory
        std::vector<uint32_t> ram(MAX_RAM_SIZE / 4);
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        size_t read_bytes = 0;
        uint32_t elf_entry = 0;
        if (!raw_program && load_elf(fbin, ram, read_bytes, start_mem_addr, ram_size * 4, elf_entry, elf_phys_override, elf_phys_offset)) {
            reset_pc = elf_entry;
            std::print("Reading ELF program into memory (size: {})\n", read_bytes);
            if (linux_earlycon_mapbase && !patch_linux_earlycon_mapbase(ram, start_mem_addr, ram_size * 4)) {
                fclose(fbin);
                return false;
            }
        }
        else {
            fseek(fbin, start_offset, SEEK_SET);
            read_bytes = fread(ram.data(), 1, 4 * ram_size, fbin);
            std::print("Reading raw program into memory (size: {}, offset: {})\n", read_bytes, start_offset);
        }
        if (!dtb_file.empty()) {
            if (!boot_dtb_addr) {
                std::print("--dtb requires --boot-dtb-addr\n");
                fclose(fbin);
                return false;
            }
            size_t dtb_bytes = 0;
            if (!load_blob(dtb_file, ram, boot_dtb_addr, start_mem_addr, ram_size * 4, dtb_bytes)) {
                fclose(fbin);
                return false;
            }
            if (!bootargs.empty() && !patch_dtb_bootargs(ram, boot_dtb_addr, (uint32_t)dtb_bytes, start_mem_addr, ram_size * 4, bootargs)) {
                fclose(fbin);
                return false;
            }
            std::print("Reading DTB into memory (size: {}, addr: {:08x})\n", dtb_bytes, boot_dtb_addr);
        }
        if (!initramfs_file.empty()) {
            if (!initramfs_addr) {
                std::print("--initramfs requires --initramfs-addr\n");
                fclose(fbin);
                return false;
            }
            size_t initramfs_bytes = 0;
            if (!load_blob(initramfs_file, ram, initramfs_addr, start_mem_addr, ram_size * 4, initramfs_bytes)) {
                fclose(fbin);
                return false;
            }
            std::print("Reading initramfs into memory (size: {}, addr: {:08x})\n", initramfs_bytes, initramfs_addr);
        }

        const size_t active_lines = (ram_size * 4 + (TRIBE_L2_AXI_WIDTH/8) - 1) / (TRIBE_L2_AXI_WIDTH/8);
        for (size_t line_idx = 0; line_idx < active_lines; ++line_idx) {
            logic<TRIBE_L2_AXI_WIDTH> line = 0;
            for (size_t word = 0; word < (TRIBE_L2_AXI_WIDTH/8) / 4; ++word) {
                size_t addr = line_idx * ((TRIBE_L2_AXI_WIDTH/8) / 4) + word;
                line.bits(word * 32 + 31, word * 32) = ram[addr];
                if (debugen_in) {
                    std::print("{:04x}: {:08x}\n", addr, ram[addr]);
                }
            }
            if (line_idx < AXI_RAM0_DEPTH) {
                mem0.ram.buffer[line_idx] = line;
            }
            else if (line_idx < AXI_RAM0_DEPTH + AXI_RAM1_DEPTH) {
                mem1.ram.buffer[line_idx - AXI_RAM0_DEPTH] = line;
            }
            else {
                mem2.ram.buffer[line_idx - AXI_RAM0_DEPTH - AXI_RAM1_DEPTH] = line;
            }
        }
        fclose(fbin);
        ///////////////////////////////////////

        __inst_name = "tribe_test";
        _assign();
        _strobe();
        ++sys_clock;
        _work(1);
        _strobe_neg();
        _work_neg(1);

        auto start = std::chrono::high_resolution_clock::now();
        perf_reset();
        bool checkpoint_loaded_pending_work = false;
        if (!checkpoint_load_file.empty()) {
            FILE* checkpoint_in = fopen(checkpoint_load_file.c_str(), "rb");
            if (!checkpoint_in) {
                std::print("can't open checkpoint input '{}'\n", checkpoint_load_file);
                return false;
            }
            _strobe(checkpoint_read_fd(checkpoint_in));
            fclose(checkpoint_in);
            checkpoint_loaded_pending_work = true;
            std::print("Loaded checkpoint '{}'\n", checkpoint_load_file);
        }
        const char* trace_period_env = std::getenv("TRIBE_TRACE_PC_PERIOD");
        uint32_t trace_period = trace_period_env ? std::stoul(trace_period_env, nullptr, 0) : 0;
        const char* trace_addr_env = std::getenv("TRIBE_TRACE_ADDR");
        uint32_t trace_addr = trace_addr_env ? std::stoul(trace_addr_env, nullptr, 0) : 0;
        const char* trace_pc_from_env = std::getenv("TRIBE_TRACE_PC_FROM");
        const char* trace_pc_to_env = std::getenv("TRIBE_TRACE_PC_TO");
        uint32_t trace_pc_from = trace_pc_from_env ? std::stoul(trace_pc_from_env, nullptr, 0) : 0;
        uint32_t trace_pc_to = trace_pc_to_env ? std::stoul(trace_pc_to_env, nullptr, 0) : 0;
        const char* debug_start_env = std::getenv("TRIBE_DEBUG_START");
        uint32_t debug_start = debug_start_env ? std::stoul(debug_start_env, nullptr, 0) : 0;
        const char* debug_pc_ge_env = std::getenv("TRIBE_DEBUG_PC_GE");
        uint32_t debug_pc_ge = debug_pc_ge_env ? std::stoul(debug_pc_ge_env, nullptr, 0) : 0;
        const bool trace_mmu = std::getenv("TRIBE_TRACE_MMU") != nullptr;
        const bool trace_csr = std::getenv("TRIBE_TRACE_CSR") != nullptr;
        const bool trace_io = std::getenv("TRIBE_TRACE_IO") != nullptr;
        const bool trace_sbi = std::getenv("TRIBE_TRACE_SBI") != nullptr;
        const bool trace_mmu_fault = std::getenv("TRIBE_TRACE_MMU_FAULT") != nullptr;
        const bool trace_ra = std::getenv("TRIBE_TRACE_RA") != nullptr;
        const bool trace_bad_branch = std::getenv("TRIBE_TRACE_BAD_BRANCH") != nullptr;
        bool last_immu_fault = false;
        std::string expected_output;
        std::string captured_output;
        bool expected_marker_seen = false;
        if (!tohost_addr && expected_output_contains.empty()) {
            std::ifstream expected_file(expected_log, std::ios::binary);
            if (!expected_file) {
                std::print("can't open expected output '{}'\n", expected_log);
                error = true;
            }
            else {
                expected_output.assign(std::istreambuf_iterator<char>(expected_file), std::istreambuf_iterator<char>());
            }
        }
        auto output_file_reached_expected = [&]() {
            if (tohost_addr || expected_output.empty()) {
                return false;
            }
            std::ifstream out_file("out.txt", std::ios::binary);
            if (!out_file) {
                return false;
            }
            std::string current_output((std::istreambuf_iterator<char>(out_file)), std::istreambuf_iterator<char>());
            if (current_output.size() < expected_output.size()) {
                return false;
            }
            error = current_output != expected_output;
            return true;
        };
        auto capture_uart_output = [&]() {
            if (!uart.uart_valid_out()) {
                return false;
            }

            FILE* uart_out = fopen("out.txt", "ab");
            if (!uart_out) {
                return false;
            }

            char ch = (char)uart.uart_data_out();
            fputc(ch, uart_out);
            fclose(uart_out);
            if (!tohost_addr) {
                captured_output.push_back(ch);
                if (!expected_output_contains.empty() && captured_output.find(expected_output_contains) != std::string::npos) {
                    expected_marker_seen = true;
                    return true;
                }
                if (!expected_output.empty() && captured_output.size() >= expected_output.size()) {
                    error = captured_output != expected_output;
                    return true;
                }
            }
            return false;
        };
        int cycles = max_cycles;
        bool checkpoint_saved = false;
        while (--cycles && !error && !tohost_done) {
            uint64_t cycle_time_start = tribe_runtime_tick();
            uint64_t section_time_start = cycle_time_start;
            uint64_t strobe_ticks_before = runtime_strobe_ticks;
            if (checkpoint_loaded_pending_work) {
                checkpoint_loaded_pending_work = false;
            }
            else {
                FILE* checkpoint_out = nullptr;
                if (!checkpoint_saved && !checkpoint_save_file.empty() && checkpoint_save_cycle && perf_clocks + 1 == checkpoint_save_cycle) {
                    checkpoint_out = fopen(checkpoint_save_file.c_str(), "wb");
                    if (!checkpoint_out) {
                        std::print("can't open checkpoint output '{}'\n", checkpoint_save_file);
                        error = true;
                        break;
                    }
                }
                uint64_t strobe_time_start = tribe_runtime_tick();
                _strobe(checkpoint_out);
                runtime_strobe_ticks += tribe_runtime_tick() - strobe_time_start;
                if (checkpoint_out) {
                    fclose(checkpoint_out);
                    checkpoint_saved = true;
                    std::print("Saved checkpoint '{}' at cycle {}\n", checkpoint_save_file, perf_clocks + 1);
                }
            }
            runtime_checkpoint_ticks += tribe_runtime_tick() - section_time_start - (runtime_strobe_ticks - strobe_ticks_before);
            section_time_start = tribe_runtime_tick();
            ++sys_clock;
            perf_sample();
            if (capture_uart_output()) {
                break;
            }
            runtime_uart_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            if (debug_start && perf_clocks >= debug_start) {
                debugen_in = true;
#ifndef VERILATOR
                tribe.debugen_in = true;
#endif
            }
            if (debug_pc_ge && (uint32_t)PORT_VALUE(tribe.debug_pc_out) >= debug_pc_ge) {
                debugen_in = true;
#ifndef VERILATOR
                tribe.debugen_in = true;
#endif
            }
            runtime_perf_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            _work(0);
            runtime_work_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            if (trace_period && (perf_clocks % trace_period) == 0) {
                auto perf_now = PERF_VALUE(tribe.perf_out);
                std::print("trace cycle={} pc={:08x} imem={:08x} dmem={:08x} rd={} wr={} data={:08x} hst={} bst={} dcw={} icw={} is={} ih={} fv={} irv={} ira={:08x} ipa={:08x} ibusy={} ifault={} mw={} wblr={} wbmemw={} iread={} istall={}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (bool)perf_now.hazard_stall,
                    (bool)perf_now.branch_stall,
                    (bool)perf_now.dcache_wait,
                    (bool)perf_now.icache_wait,
                    (uint32_t)perf_now.icache.state,
                    (bool)perf_now.icache.hit,
#ifdef ENABLE_MMU_TLB
                    (bool)PORT_VALUE(tribe.debug_fetch_valid_out),
                    (bool)PORT_VALUE(tribe.debug_icache_read_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_icache_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_paddr_out),
                    (bool)PORT_VALUE(tribe.debug_immu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_immu_fault_out),
                    (bool)PORT_VALUE(tribe.debug_memory_wait_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_ready_out),
                    (bool)PORT_VALUE(tribe.debug_wb_mem_wait_out),
                    (bool)PORT_VALUE(tribe.debug_icache_read_in_out),
                    (bool)PORT_VALUE(tribe.debug_icache_stall_in_out)
#else
                    false, false, 0u, 0u, false, false, false, false, false, false, false
#endif
                    );
            }
            if (trace_csr && trace_period && (perf_clocks % trace_period) == 0) {
                std::print("trace-csr cycle={} priv={} ra={:08x} satp={:08x} mstatus={:08x} mtvec={:08x} mepc={:08x} mcause={:08x} mtval={:08x} stvec={:08x} sepc={:08x} scause={:08x} stval={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                    (uint32_t)PORT_VALUE(tribe.debug_ra_out),
                    (uint32_t)PORT_VALUE(tribe.debug_satp_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mstatus_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mtvec_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mepc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mcause_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mtval_out),
                    (uint32_t)PORT_VALUE(tribe.debug_stvec_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sepc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_scause_out),
                    (uint32_t)PORT_VALUE(tribe.debug_stval_out)
#else
                    3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
#endif
                    );
            }
#ifdef ENABLE_MMU_TLB
            if (trace_mmu_fault) {
                bool immu_fault_now = (bool)PORT_VALUE(tribe.debug_immu_fault_out);
                if (immu_fault_now && !last_immu_fault) {
                    std::print("trace-immu-fault cycle={} pc={:08x} ra={:08x} imem={:08x} satp={:08x} priv={} scause={:08x} sepc={:08x} stval={:08x} stvec={:08x} last_pte_addr={:08x} last_pte={:08x}\n",
                        perf_clocks,
                        (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                        (uint32_t)PORT_VALUE(tribe.debug_ra_out),
                        (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                        (uint32_t)PORT_VALUE(tribe.debug_satp_out),
                        (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                        (uint32_t)PORT_VALUE(tribe.debug_scause_out),
                        (uint32_t)PORT_VALUE(tribe.debug_sepc_out),
                        (uint32_t)PORT_VALUE(tribe.debug_stval_out),
                        (uint32_t)PORT_VALUE(tribe.debug_stvec_out),
                        (uint32_t)PORT_VALUE(tribe.debug_immu_last_addr_out),
                        (uint32_t)PORT_VALUE(tribe.debug_immu_last_pte_out));
                }
                last_immu_fault = immu_fault_now;
            }
#endif
            if (trace_sbi && (bool)PORT_VALUE(tribe.sbi_set_timer_out)) {
                std::print("trace-sbi cycle={} pc={:08x} set_timer={:08x}{:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_hi_out),
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_lo_out));
            }
            if (trace_ra && (bool)PORT_VALUE(tribe.debug_regs_write_out) && (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out) == 1) {
                std::print("trace-ra cycle={} pc={:08x} ra={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.debug_regs_data_out));
            }
            if (trace_bad_branch && (bool)PORT_VALUE(tribe.debug_branch_taken_now_out)) {
                uint32_t target = (uint32_t)PORT_VALUE(tribe.debug_branch_target_now_out);
                if (target < 0x10000u || (target >= 0x80000000u && target < 0x80001000u)) {
                    std::print("trace-bad-branch cycle={} pc={:08x} target={:08x} imem={:08x} dmem={:08x} rd={} wr={} wbwr={} wbid={} wbdata={:08x}\n",
                        perf_clocks,
#ifdef ENABLE_MMU_TLB
                        (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                        (uint32_t)0,
#endif
                        target,
                        (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                        (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                        (bool)PORT_VALUE(tribe.dmem_read_out),
                        (bool)PORT_VALUE(tribe.dmem_write_out),
                        (bool)PORT_VALUE(tribe.debug_regs_write_actual_out),
                        (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out),
                        (uint32_t)PORT_VALUE(tribe.debug_regs_data_out));
                }
            }
            if (trace_addr && (uint32_t)PORT_VALUE(tribe.dmem_addr_out) == trace_addr &&
                ((bool)PORT_VALUE(tribe.dmem_read_out) || (bool)PORT_VALUE(tribe.dmem_write_out))) {
                std::print("trace-addr cycle={} pc={:08x} imem={:08x} addr={:08x} rd={} wr={} wdata={:08x} mask={:02x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out));
            }
            if (trace_pc_from && (uint32_t)PORT_VALUE(tribe.debug_pc_out) >= trace_pc_from &&
                (uint32_t)PORT_VALUE(tribe.debug_pc_out) < trace_pc_to) {
                std::print("trace-pc-range cycle={} pc={:08x} imem={:08x} dinstr={:08x} dpc={:08x} dbr={} dimm={:08x} dmem={:08x} rd={} wr={} wdata={:08x} mask={:02x} wbwr={} wbact={} wbid={} wbdata={:08x} loadready={} memwait={} brtake={} brtarget={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_decode_instr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_decode_pc_out),
                    (uint8_t)PORT_VALUE(tribe.debug_decode_br_out),
                    (uint32_t)PORT_VALUE(tribe.debug_decode_imm_out),
#else
                    0u, 0u, 0u, 0u,
#endif
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out),
                    (bool)PORT_VALUE(tribe.debug_regs_write_out),
                    (bool)PORT_VALUE(tribe.debug_regs_write_actual_out),
                    (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out),
                    (uint32_t)PORT_VALUE(tribe.debug_regs_data_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_ready_out),
                    (bool)PORT_VALUE(tribe.debug_memory_wait_out),
                    (bool)PORT_VALUE(tribe.debug_branch_taken_now_out),
                    (uint32_t)PORT_VALUE(tribe.debug_branch_target_now_out));
            }
            if (trace_io &&
                (uint32_t)PORT_VALUE(tribe.dmem_addr_out) >= start_mem_addr + TRIBE_RAM_BYTES &&
                (uint32_t)PORT_VALUE(tribe.dmem_addr_out) < start_mem_addr + MAX_RAM_SIZE &&
                ((bool)PORT_VALUE(tribe.dmem_read_out) || (bool)PORT_VALUE(tribe.dmem_write_out))) {
                std::print("trace-io cycle={} pc={:08x} imem={:08x} addr={:08x} rd={} wr={} wdata={:08x} mask={:02x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out));
            }
#ifdef ENABLE_MMU_TLB
            if (trace_mmu && (PORT_VALUE(tribe.debug_immu_ptw_read_out) || PORT_VALUE(tribe.debug_dmmu_ptw_read_out) ||
                              PORT_VALUE(tribe.debug_immu_busy_out) || PORT_VALUE(tribe.debug_immu_fault_out) ||
                              PORT_VALUE(tribe.debug_dmmu_busy_out) || PORT_VALUE(tribe.debug_dmmu_fault_out))) {
                std::print("mmu cycle={} pc={:08x} imem={:08x} dmem={:08x} d_rd={} d_wr={} i_ptw={} i_addr={:08x} i_busy={} i_fault={} i_last_addr={:08x} i_last_pte={:08x} d_ptw={} d_addr={:08x} word={:08x} d_busy={} d_fault={}\n",
                    perf_clocks,
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (bool)PORT_VALUE(tribe.debug_immu_ptw_read_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_ptw_addr_out),
                    (bool)PORT_VALUE(tribe.debug_immu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_immu_fault_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_last_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_last_pte_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_ptw_read_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dmmu_ptw_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mmu_ptw_word_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_fault_out));
            }
#endif
            if (!tohost_addr && (perf_clocks & 0xffu) == 0 && output_file_reached_expected()) {
                break;
            }
            if (tohost_addr && PORT_VALUE(tribe.dmem_write_out) && PORT_VALUE(tribe.dmem_addr_out) == tohost_addr &&
                PORT_VALUE(tribe.dmem_write_mask_out) && PORT_VALUE(tribe.dmem_write_data_out)) {
                tohost_value = PORT_VALUE(tribe.dmem_write_data_out);
                tohost_done = true;
            }
            if (debugen_in) {
                debug_perf_counters_print();
            }
            runtime_trace_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            _strobe_neg();
            _work_neg(0);
            runtime_negedge_ticks += tribe_runtime_tick() - section_time_start;
            runtime_total_ticks += tribe_runtime_tick() - cycle_time_start;
        }

        if (checkpoint_save_only_success) {
            if (!checkpoint_saved) {
                std::print("checkpoint was not saved before cycle limit\n");
                error = true;
            }
        }
        else if (!expected_output_contains.empty()) {
            if (!expected_marker_seen) {
                std::print("UART output marker '{}' was not seen before cycle limit\n", expected_output_contains);
                error = true;
            }
        }
        else if (tohost_addr) {
            if (!tohost_done) {
                std::print("tohost was not written before cycle limit\n");
                error = true;
            }
            else if (tohost_value != 1) {
                std::print("tohost reported failure value {:08x}\n", tohost_value);
                error = true;
            }
        }
        else {
            std::ifstream a(expected_log, std::ios::binary), b("out.txt", std::ios::binary);
            error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));
        }

        perf_print();
        std::print(" {} ({} microseconds)\n", !error ? (checkpoint_save_only_success ? "CHECKPOINT SAVED" : "PASSED") : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

[[maybe_unused]] static std::filesystem::path absolute_from(const std::filesystem::path& base, const std::string& path)
{
    std::filesystem::path p(path);
    return p.is_absolute() ? p : std::filesystem::absolute(base / p);
}

[[maybe_unused]] static std::filesystem::path tribe_source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

[[maybe_unused]] static std::string shell_quote_path(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

[[maybe_unused]] static bool regenerate_tribe_sv(const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;

    fs::path cpphdl = fs::current_path() / ".." / "cpphdl";
    if (!fs::exists(cpphdl)) {
        cpphdl = source_root / "build" / "cpphdl";
    }
    if (!fs::exists(cpphdl)) {
        std::print("can't find cpphdl generator near build directory or source root\n");
        return false;
    }

    std::string command;
    command += shell_quote_path(cpphdl);
    command += " " + shell_quote_path(source_root / "tribe" / "main.cpp");
    command += " -DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
    command += " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES);
    command += " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE);
    command += " -I " + shell_quote_path(source_root / "include");
    command += " -I " + shell_quote_path(source_root / "tribe" / "common");
    command += " -I " + shell_quote_path(source_root / "tribe" / "spec");
    command += " -I " + shell_quote_path(source_root / "tribe" / "devices");
    return std::system(command.c_str()) == 0;
}

[[maybe_unused]] static void use_executable_workdir_if_needed(const char* argv0)
{
    namespace fs = std::filesystem;

    if (fs::exists("generated/Tribe.sv") || fs::exists("rv32i.elf") || fs::exists("uart.elf") || fs::exists("rv32i.bin")) {
        return;
    }

    fs::path exe = fs::absolute(argv0);
    fs::path exe_dir = exe.parent_path();
    if (!exe_dir.empty() && (fs::exists(exe_dir / "generated" / "Tribe.sv") || fs::exists(exe_dir / "rv32i.elf") || fs::exists(exe_dir / "uart.elf") || fs::exists(exe_dir / "rv32i.bin"))) {
        fs::current_path(exe_dir);
    }
}

#if !defined(NO_MAINFILE)

int main (int argc, char** argv)
{
    const std::filesystem::path original_cwd = std::filesystem::current_path();
    bool debug = false;
    bool noveril = false;
    std::string program = "rv32i.elf";
    std::string expected_log = "rv32i.log";
    bool program_arg = false;
    bool log_arg = false;
    bool raw_program = false;
    size_t start_offset = 0x37c;
    int max_cycles = 2000000;
    uint32_t tohost = 0;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    bool elf_phys_override = false;
    uint32_t elf_phys_offset = 0;
    std::string dtb_file;
    std::string initramfs_file;
    uint32_t initramfs_addr = 0;
    std::string checkpoint_load_file;
    std::string checkpoint_save_file;
    uint64_t checkpoint_save_cycle = 0;
    bool append_output = false;
    std::string bootargs;
    bool linux_earlycon_mapbase = false;
    int only = -1;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
            program = argv[++i];
            program_arg = true;
            raw_program = false;
            continue;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            expected_log = argv[++i];
            log_arg = true;
            continue;
        }
        if (strcmp(argv[i], "--raw") == 0) {
            raw_program = true;
            continue;
        }
        if (strcmp(argv[i], "--elf") == 0) {
            raw_program = false;
            continue;
        }
        if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
            start_offset = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = std::stoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--tohost") == 0 && i + 1 < argc) {
            tohost = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--start-mem-addr") == 0 && i + 1 < argc) {
            start_mem_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--ram-size") == 0 && i + 1 < argc) {
            ram_size = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-hartid") == 0 && i + 1 < argc) {
            boot_hartid = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-dtb-addr") == 0 && i + 1 < argc) {
            boot_dtb_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-priv") == 0 && i + 1 < argc) {
            std::string value = argv[++i];
            if (value == "m" || value == "M") {
                boot_priv = 3;
            }
            else if (value == "s" || value == "S") {
                boot_priv = 1;
            }
            else if (value == "u" || value == "U") {
                boot_priv = 0;
            }
            else {
                boot_priv = std::stoul(value, nullptr, 0);
            }
            continue;
        }
        if (strcmp(argv[i], "--dtb") == 0 && i + 1 < argc) {
            dtb_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--initramfs") == 0 && i + 1 < argc) {
            initramfs_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--initramfs-addr") == 0 && i + 1 < argc) {
            initramfs_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-load") == 0 && i + 1 < argc) {
            checkpoint_load_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-save") == 0 && i + 1 < argc) {
            checkpoint_save_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-save-cycle") == 0 && i + 1 < argc) {
            checkpoint_save_cycle = std::stoull(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--append-output") == 0) {
            append_output = true;
            continue;
        }
        if (strcmp(argv[i], "--linux-earlycon-mapbase") == 0) {
            linux_earlycon_mapbase = true;
            continue;
        }
        if (strcmp(argv[i], "--bootargs") == 0 && i + 1 < argc) {
            bootargs = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--elf-phys-offset") == 0 && i + 1 < argc) {
            elf_phys_offset = std::stoul(argv[++i], nullptr, 0);
            elf_phys_override = true;
            continue;
        }
        if (strcmp(argv[i], "--elf-phys-base") == 0 && i + 1 < argc) {
            uint32_t phys_base = std::stoul(argv[++i], nullptr, 0);
            elf_phys_offset = phys_base - 0xc0000000u;
            elf_phys_override = true;
            continue;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[argc-1]);
        }
    }

    if (program_arg) {
        program = absolute_from(original_cwd, program).string();
    }
    if (log_arg) {
        expected_log = absolute_from(original_cwd, expected_log).string();
    }
    if (!dtb_file.empty()) {
        dtb_file = absolute_from(original_cwd, dtb_file).string();
    }
    if (!initramfs_file.empty()) {
        initramfs_file = absolute_from(original_cwd, initramfs_file).string();
    }
    if (!checkpoint_load_file.empty()) {
        checkpoint_load_file = absolute_from(original_cwd, checkpoint_load_file).string();
    }
    if (!checkpoint_save_file.empty()) {
        checkpoint_save_file = absolute_from(original_cwd, checkpoint_save_file).string();
    }
    use_executable_workdir_if_needed(argv[0]);

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        const auto source_root = tribe_source_root_dir();
        auto start = std::chrono::high_resolution_clock::now();
        if (!regenerate_tribe_sv(source_root)) {
            ok = false;
        }
        else {
            ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
                  "Amo_pkg",
                  "Trap_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32im_pkg",
                  "Rv32ia_pkg",
                  "Zicsr_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Sys_pkg",
                  "Csr_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "L1CachePerf_pkg",
                  "TribePerf_pkg",
                  "File",
                  "RAM1PORT",
                  "L1Cache",
                  "L2Cache",
                  "BranchPredictor",
                  "InterruptController",
	                  "Decode",
	                  "Execute",
	                  "ExecuteMem",
	                  "CSR",
	                  "MMU_TLB",
	                  "Writeback",
	                  "WritebackMem"}, {
                          (source_root / "include").string(),
                          (source_root / "tribe").string(),
                          (source_root / "tribe" / "common").string(),
                          (source_root / "tribe" / "spec").string(),
                          (source_root / "tribe" / "cache").string(),
                          (source_root / "tribe" / "devices").string()});
        }
        std::cout << "Executing tests... ===========================================================================\n";
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Tribe/obj_dir/VTribe") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestTribe(debug).run(program, start_offset, expected_log, max_cycles, tohost, start_mem_addr, ram_size, raw_program, boot_hartid, boot_dtb_addr, boot_priv, elf_phys_override, elf_phys_offset, dtb_file, linux_earlycon_mapbase, initramfs_file, initramfs_addr, checkpoint_load_file, checkpoint_save_file, checkpoint_save_cycle, append_output, bootargs))
    );
}

#endif  // !NO_MAINFILE

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
