#pragma once

#include "File.h"

#include "Config.h"
#include "TribeDebug.h"

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
    // Tribe forwards every primary writeback source explicitly in forward().
    // Disable the generic write-first read bypass here so memory-stage ready
    // cannot feed backward through decode, branch prediction, and both MMUs.
    // The independent write2 SBI return bypass remains enabled in File.
    File<32,32,false> regs;
    L1Cache<L1_ICACHE_SIZE,CACHE_LINE_SIZE,L1_CACHE_ASSOCIATIONS,0,ADDR_BITS,TRIBE_L2_AXI_WIDTH> icache;
    L1Cache<L1_DCACHE_SIZE,CACHE_LINE_SIZE,L1_CACHE_ASSOCIATIONS,1,ADDR_BITS,TRIBE_L2_AXI_WIDTH> dcache;
    BranchPredictor<BRANCH_PREDICTOR_ENTRIES, BRANCH_PREDICTOR_COUNTER_BITS> bp;
    reg<u1> icache_invalidate_issued_reg;
    // One-cycle interlock used when writeback updates an implicit SBI argument
    // register immediately before an ECALL.  This replaces a cache-data to
    // execute-redirect combinational bypass.
    reg<u1> sbi_arg_wait_reg;

public:

    _PORT(bool)      dmem_write_out;
    _PORT(uint32_t)  dmem_write_data_out;
    _PORT(uint8_t)   dmem_write_mask_out;
    _PORT(bool)      dmem_read_out;
    _PORT(uint32_t)  dmem_addr_out;
    _PORT(uint32_t)  imem_read_addr_out;
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
    _PORT(bool)      atomic_request_out = _ASSIGN_COMB(atomic_request_comb_func());
    _PORT(bool)      atomic_data_request_out = _ASSIGN_COMB(atomic_data_request_comb_func());
    _PORT(bool)      atomic_complete_out = _ASSIGN_COMB(atomic_complete_comb_func());
    _PORT(bool)      atomic_grant_in = _ASSIGN(true);
#endif
#ifdef ENABLE_MMU_TLB
    _PORT(TribeCoreDebug)      debug_core_out = _ASSIGN_COMB(debug_core_comb_func());
    _PORT(TribeMmuDebug)       debug_mmu_out = _ASSIGN_COMB(debug_mmu_comb_func());
    _PORT(TribeCacheDebug)     debug_cache_out = _ASSIGN_COMB(debug_cache_comb_func());
    _PORT(TribeWritebackDebug) debug_wb_out = _ASSIGN_COMB(debug_wb_comb_func());
    _PORT(TribeCsrDebug)       debug_csr_out = _ASSIGN_COMB(debug_csr_comb_func());
    _PORT(TribeIrqDebug)       debug_irq_out = _ASSIGN_COMB(debug_irq_comb_func());
    _PORT(TribeRegsDebug)      debug_regs_out = _ASSIGN_COMB(debug_regs_comb_func());
    _PORT(TribeBranchDebug)    debug_branch_out = _ASSIGN_COMB(debug_branch_comb_func());
    _PORT(TribeDecodeDebug)    debug_decode_out = _ASSIGN_COMB(debug_decode_comb_func());
#endif
    _PORT(bool)      sbi_set_timer_out = _ASSIGN_COMB(sbi_set_timer_comb_func());
    _PORT(uint32_t)  sbi_timer_lo_out = _ASSIGN_COMB(sbi_timer_lo_comb_func());
    _PORT(uint32_t)  sbi_timer_hi_out = _ASSIGN_COMB(sbi_timer_hi_comb_func());
#if defined(MULTICORE) && defined(ENABLE_ISR)
    _PORT(bool)      sbi_send_ipi_out = _ASSIGN_COMB(sbi_send_ipi_comb_func());
    _PORT(bool)      sbi_remote_fence_i_out = _ASSIGN_COMB(sbi_remote_fence_i_comb_func());
#endif
#if defined(MULTICORE) && defined(ENABLE_MMU_TLB)
    _PORT(bool)      sbi_remote_sfence_vma_out = _ASSIGN_COMB(sbi_remote_sfence_vma_comb_func());
#endif
#if defined(MULTICORE) && defined(ENABLE_ISR)
    _PORT(uint32_t)  sbi_hart_mask_out = _ASSIGN_COMB(sbi_hart_mask_comb_func());
    _PORT(uint32_t)  sbi_hart_base_out = _ASSIGN_COMB(sbi_hart_base_comb_func());
#endif
    _PORT(TribeSbiDebug) debug_sbi_out = _ASSIGN_COMB(debug_sbi_comb_func());
    _PORT(uint32_t)  reset_pc_in;
    _PORT(uint32_t)  boot_hartid_in;
    _PORT(uint32_t)  boot_dtb_addr_in;
    _PORT(u<2>)      boot_priv_in;
    _PORT(bool)      external_cache_invalidate_in;
#ifdef MULTICORE
    _PORT(bool)      peer_cache_invalidate_in = _ASSIGN(false);
    _PORT(uint32_t)  peer_cache_invalidate_addr_in = _ASSIGN((uint32_t)0);
#endif
    _PORT(uint32_t)  memory_base_in;
    _PORT(uint32_t)  memory_size_in;
    _PORT(uint32_t)  mem_region_size_in[L2_MEM_PORTS];
    // The parent cluster connects these L1 miss/PTW channels to its shared L2 cache.
    L1MemIf<TRIBE_L2_AXI_WIDTH> i_mem_out;
    L1MemIf<TRIBE_L2_AXI_WIDTH> d_mem_out;
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
    _PORT(bool)      clint_msip_in;
    _PORT(bool)      clint_mtip_in;
    _PORT(uint32_t)  time_lo_in = _ASSIGN((uint32_t)0);
    _PORT(uint32_t)  time_hi_in = _ASSIGN((uint32_t)0);
    _PORT(bool)      external_irq_in = _ASSIGN(false);
#ifdef MULTICORE
    _PORT(bool)      sbi_ipi_in = _ASSIGN(false);
    _PORT(bool)      remote_fence_i_in = _ASSIGN(false);
    _PORT(bool)      remote_sfence_vma_in = _ASSIGN(false);
#endif
#endif

    _PORT(TribePerf) perf_out = _ASSIGN_COMB(perf_comb_func());
    bool              debugen_in;

    void _assign()
    {
//        dec.state_in       = _ASSIGN_REG( state_reg[0] );  // execute stage input is same
        dec.pc_in          = _ASSIGN_REG( pc );
        // Decode consumes only the registered fetch response.  A live L1
        // BRAM output must not pass through decode/hazard control and feed the
        // I-cache address or enable inputs again in the same 312 MHz cycle.
        dec.instr_in       = _ASSIGN_REG(fetch_instr_reg);
        dec.instr_valid_in = _ASSIGN(fetch_valid_comb_func());
        dec.regs_data0_in  = _ASSIGN( dec.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        dec.regs_data1_in  = _ASSIGN( dec.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        dec._assign();  // outputs are ready

        exe.state_in       = _ASSIGN_COMB( exe_state_comb_func() );
        exe.multicycle_state_in = _ASSIGN_REG(state_reg[0]);
        exe._assign();  // outputs are ready

        exe_mem.state_in = _ASSIGN_COMB(exe_state_comb_func());
        exe_mem.alu_result_in = exe.alu_result_out;
        exe_mem.transaction_owner_valid_in = _ASSIGN((bool)state_reg[1].valid);
#ifdef ENABLE_RV32IA
#ifdef ENABLE_MMU_TLB
        // AMO read responses are tagged by the physical D-cache address; match
        // them against the translated MMU address, not the architectural VA.
        exe_mem.dcache_read_expected_addr_in = _ASSIGN_REG(dmmu_paddr_reg);
#else
        exe_mem.dcache_read_expected_addr_in = exe_mem.mem_read_addr_out;
#endif
#ifdef MULTICORE
        exe_mem.reservation_invalidate_in = peer_cache_invalidate_in;
        exe_mem.reservation_invalidate_addr_in = peer_cache_invalidate_addr_in;
#endif
#endif
        exe_mem.hold_in = _ASSIGN( memory_wait_comb_func() );
        exe_mem.__inst_name = __inst_name + "/exe_mem";
        exe_mem._assign();

        wb_mem.state_in = _ASSIGN_REG(state_reg[1]);
        wb_mem.alu_result_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN(dcache.read_valid_out() ? (uint32_t)dcache.read_addr_out() :
                ((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM) ? (uint32_t)dmmu_paddr_reg : (uint32_t)alu_result_reg));
#else
            _ASSIGN_REG(alu_result_reg);
#endif
        wb_mem.split_load_in = exe_mem.split_load_out;
        wb_mem.split_load_low_addr_in = exe_mem.split_load_low_out;
        wb_mem.split_load_high_addr_in = exe_mem.split_load_high_out;
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
        csr.legality_state_in = _ASSIGN_COMB(dec.state_out());
        // The same-cycle Execute redirect only needs the current architectural
        // instruction.  MMU fault state is committed through csr.state_in and
        // selects its vector separately below; feeding it back here creates a
        // fetch/translation/redirect combinational cycle.
        csr.redirect_state_in = _ASSIGN_REG(state_reg[0]);
        csr.reset_priv_in = boot_priv_in;
        csr.hartid_in = boot_hartid_in;
#ifdef ENABLE_ISR
        csr.time_lo_in = time_lo_in;
        csr.time_hi_in = time_hi_in;
#else
        csr.time_lo_in = _ASSIGN((uint32_t)0);
        csr.time_hi_in = _ASSIGN((uint32_t)0);
#endif
#ifdef ENABLE_ISR
        csr.interrupt_valid_in = _ASSIGN_COMB(interrupt_accept_comb_func());
        csr.interrupt_cause_in = _ASSIGN_REG(interrupt_cause_reg);
        csr.interrupt_to_supervisor_in = _ASSIGN_REG(interrupt_to_supervisor_reg);
        csr.irq_pending_bits_in = irq.mip_out;
#ifdef MULTICORE
        csr.software_irq_set_in = sbi_ipi_in;
#endif
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
        // Translate only the registered fetch PC.  An execute-stage branch
        // redirect updates pc at the clock edge; feeding its combinational
        // target directly into the MMU creates an execute -> translation ->
        // fault/CSR path in one 312 MHz cycle.
        immu.vaddr_in = _ASSIGN((uint32_t)pc);
        immu.read_in = _ASSIGN(false);
        immu.write_in = _ASSIGN(false);
        // Only a live front-end fetch may request instruction translation.
        // During redirects and bubbles fetch_addr_comb can be a placeholder; translating
        // it would create a spurious instruction page fault.
        immu.execute_in = _ASSIGN((bool)valid && !immu_result_match_comb_func());
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
        immu.mem_wait_in = _ASSIGN(!immu_ptw_selected_comb_func() || d_mem_out.wait_out());
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
        dmmu.mem_wait_in = _ASSIGN(!dmmu_ptw_selected_comb_func() || d_mem_out.wait_out());
        dmmu.__inst_name = __inst_name + "/dmmu";
        dmmu._assign();
#endif

        wb.state_in       = _ASSIGN_REG( state_reg[1] );
        wb.mem_data_in    = wb_mem.load_raw_out;
        wb.mem_data_hi_in = _ASSIGN((uint32_t)0);
        wb.mem_addr_in    =
#ifdef ENABLE_MMU_TLB
            _ASSIGN((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM) ? (uint32_t)dmmu_paddr_reg : (uint32_t)alu_result_reg);
#else
            _ASSIGN_REG(alu_result_reg);
#endif
        wb.mem_split_in   = _ASSIGN(false);
        wb.alu_result_in  = _ASSIGN_REG( alu_result_reg );
        wb._assign();  // outputs are ready

        regs.read_addr0_in = _ASSIGN( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = _ASSIGN( (uint8_t)dec.rs2_out() );
        regs.write_in = _ASSIGN_COMB(register_write_commit_comb_func());
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
            // A cache/MMIO response now crosses two WritebackMem register
            // boundaries.  Once the first boundary owns a beat, do not issue
            // the same architectural read again while writeback assembles it.
            // This is essential for read-to-pop devices such as NS16550 RBR.
            && !wb_mem.load_ready_out()
            && (exe_mem.split_load_out() ?
                !(((uint32_t)dcache.addr_in() == (uint32_t)exe_mem.split_load_low_out() &&
                    wb_mem.debug_split_low_valid_out()) ||
                  ((uint32_t)dcache.addr_in() == (uint32_t)exe_mem.split_load_high_out() &&
                    wb_mem.debug_split_high_valid_out())) :
                !wb_mem.debug_held_load_valid_out())
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            && (!atomic_request_comb_func() || atomic_grant_in())
#endif
#ifdef ENABLE_MMU_TLB
            && dmmu_result_match_comb_func() && !dmmu_fault_reg
#endif
            );
        dcache.write_in = _ASSIGN( state_reg[1].valid && exe_mem.mem_write_out() && !dcache.busy_out()
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            && (!atomic_request_comb_func() || atomic_grant_in())
#endif
#ifdef ENABLE_MMU_TLB
            && dmmu_result_match_comb_func() && !dmmu_fault_reg
#endif
            );
        dcache.addr_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN_REG(dmmu_paddr_reg);
#else
            _ASSIGN( exe_mem.mem_read_out() ? (uint32_t)exe_mem.mem_read_addr_out() : (uint32_t)exe_mem.mem_write_addr_out() );
#endif
        dcache.write_data_in = exe_mem.mem_write_data_out;
        dcache.write_mask_in = exe_mem.mem_write_mask_out;
        // A memory-stage request is older than an execute-stage branch
        // redirect and must be allowed to drain.  Stalling L1 here deadlocks a
        // cache-hit load: its younger taken branch waits for memory while the
        // branch stall simultaneously suppresses the load response.
        dcache.stall_in = _ASSIGN(false);
        dcache.flush_in = _ASSIGN(false);
#ifdef MULTICORE
        dcache.invalidate_in = external_cache_invalidate_in;
        dcache.invalidate_line_in = peer_cache_invalidate_in;
        dcache.invalidate_addr_in = peer_cache_invalidate_addr_in;
#else
        dcache.invalidate_in = external_cache_invalidate_in;
#endif
        // MMIO region bypasses L1 caching; RAM stays cacheable and coherent through L2.
        dcache.cache_disable_in = _ASSIGN(
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            atomic_request_comb_func() ||
#endif
            (uint32_t)dcache.addr_in() >= memory_base_in() + mem_region_size_in[0]() + mem_region_size_in[1]() + mem_region_size_in[2]() &&
            (uint32_t)dcache.addr_in() < memory_base_in() + memory_size_in());
        dcache.debugen_in = debugen_in;
        dcache.__inst_name = __inst_name + "/dcache";
        dcache._assign();
        // L1 output ports are installed by L1Cache::_assign(); copy them only
        // after that call so consumers do not retain empty function_ref objects.
#ifdef ENABLE_RV32IA
        exe_mem.dcache_read_valid_in = dcache.read_valid_out;
        exe_mem.dcache_read_addr_in = dcache.read_addr_out;
        exe_mem.dcache_read_data_in = dcache.read_data_out;
#endif
        exe_mem.mem_stall_in = _ASSIGN(dcache.busy_out()
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            || (atomic_request_comb_func() && !atomic_grant_in())
#endif
            );
        wb_mem.dcache_read_valid_in = dcache.read_valid_out;
        wb_mem.dcache_read_addr_in = dcache.read_addr_out;
        wb_mem.dcache_read_data_in = dcache.read_data_out;
        // dcache.mem_out is populated by dcache._assign(); bind these after it
        // so WritebackMem does not capture empty interface port functions.
        wb_mem.dcache_write_valid_in = dcache.mem_out.write_in;
        wb_mem.dcache_write_addr_in = dcache.mem_out.addr_in;
        wb_mem.dcache_write_data_in = dcache.mem_out.write_data_in;
        wb_mem.dcache_write_mask_in = dcache.mem_out.write_mask_in;

        // At 312 MHz the I-cache/decode/predictor/PC cone cannot be a single
        // cycle. Keep predictor training, but fetch sequentially and redirect
        // taken branches from the registered execute stage.
        bp.lookup_valid_in = _ASSIGN(false);
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
            && immu_result_match_comb_func() && !immu_fault_reg
#endif
            );
        icache.addr_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN_REG(immu_paddr_reg);
#else
            _ASSIGN((uint32_t)pc);
#endif
        icache.write_in = _ASSIGN( false );
        icache.write_data_in = _ASSIGN( (uint32_t)0 );
        icache.write_mask_in = _ASSIGN( (uint8_t)0 );
        // The core pipeline itself holds PC/decode state during a data-memory
        // wait.  Feeding that wait back into the I-cache couples the complete
        // DMMU -> D-cache completion cone to the I-cache BRAM enable.  The
        // I-cache may finish or replay the registered PC while the pipeline is
        // held.  PC and the fetch buffer provide the required backpressure;
        // feeding decode hazards into L1 creates a BRAM -> decode -> BRAM
        // address loop that is much longer than one 312 MHz cycle.
        icache.stall_in = _ASSIGN(false);
        icache.flush_in = _ASSIGN(branch_mispredict_comb_func());
        icache.invalidate_in = _ASSIGN_COMB(icache_invalidate_comb_func());
        icache.cache_disable_in = _ASSIGN(false);
        icache.debugen_in = debugen_in;
        icache.__inst_name = __inst_name + "/icache";
        icache._assign();
        i_mem_out.read_in = icache.mem_out.read_in;
        i_mem_out.write_in = _ASSIGN(false);
        i_mem_out.addr_in = icache.mem_out.addr_in;
        i_mem_out.write_data_in = _ASSIGN((uint32_t)0);
        i_mem_out.write_mask_in = _ASSIGN((uint8_t)0);
        i_mem_out.cache_disable_in = _ASSIGN(false);
        // CPU data misses and MMU page-table walks share the L2 data-side request path.
        d_mem_out.read_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN(dmmu_ptw_selected_comb_func() ? dmmu.mem_read_out() :
                (immu_ptw_selected_comb_func() ? immu.mem_read_out() :
                    dcache.mem_out.read_in()));
        d_mem_out.write_in = _ASSIGN(l2_ptw_owner_reg == L2_PTW_OWNER_NONE &&
            dcache.mem_out.write_in());
#else
            dcache.mem_out.read_in;
        d_mem_out.write_in = dcache.mem_out.write_in;
#endif
        d_mem_out.addr_in = _ASSIGN_COMB(l2_data_addr_comb_func());
        d_mem_out.write_data_in = dcache.mem_out.write_data_in;
        d_mem_out.write_mask_in = dcache.mem_out.write_mask_in;
        // L1 and L2 bypass are separate decisions. Multicore atomics bypass
        // the private L1 but must still use the shared L2 coherence point;
        // only device-space accesses bypass both cache levels.
        d_mem_out.cache_disable_in = _ASSIGN(
            l2_data_addr_comb_func() >= memory_base_in() + mem_region_size_in[0]() +
                mem_region_size_in[1]() + mem_region_size_in[2]() &&
            l2_data_addr_comb_func() < memory_base_in() + memory_size_in());
        // The parent binds response functions before _assign(), allowing L1 to
        // retain the complete shared-L2 return path during structural wiring.
        dcache.mem_out.read_data_out = d_mem_out.read_data_out;
#ifdef ENABLE_MMU_TLB
        dcache.mem_out.wait_out = _ASSIGN(
            l2_ptw_owner_reg != L2_PTW_OWNER_NONE || d_mem_out.wait_out());
#else
        dcache.mem_out.wait_out = d_mem_out.wait_out;
#endif
        icache.mem_out.read_data_out = i_mem_out.read_data_out;
        icache.mem_out.wait_out = i_mem_out.wait_out;

        dmem_write_out      = dcache.mem_out.write_in;
        dmem_write_data_out = dcache.mem_out.write_data_in;
        dmem_write_mask_out = dcache.mem_out.write_mask_in;
        dmem_read_out       = dcache.mem_out.read_in;
        dmem_addr_out       = dcache.mem_out.addr_in;
        imem_read_addr_out  = icache.mem_out.addr_in;
    }

#ifdef ENABLE_MMU_TLB
    _LAZY_COMB(debug_core_comb, TribeCoreDebug)
        debug_core_comb.pc = pc;
        debug_core_comb.fetch_valid = fetch_valid_comb_func();
        debug_core_comb.memory_wait = memory_wait_comb_func();
        return debug_core_comb;
    }

    _LAZY_COMB(debug_mmu_comb, TribeMmuDebug)
        debug_mmu_comb.immu_ptw_read = immu.mem_read_out();
        debug_mmu_comb.immu_ptw_addr = immu.mem_addr_out();
        debug_mmu_comb.immu_busy = immu.busy_out();
        debug_mmu_comb.immu_fault = immu.fault_out();
        debug_mmu_comb.immu_paddr = immu.paddr_out();
        debug_mmu_comb.immu_last_addr = immu.debug_last_addr_out();
        debug_mmu_comb.immu_last_pte = immu.debug_last_pte_out();
        debug_mmu_comb.dmmu_ptw_read = dmmu.mem_read_out();
        debug_mmu_comb.dmmu_ptw_addr = dmmu.mem_addr_out();
        debug_mmu_comb.dmmu_busy = dmmu.busy_out();
        debug_mmu_comb.dmmu_fault = dmmu.fault_out();
        debug_mmu_comb.ptw_word = mmu_l2_read_word_comb_func();
        return debug_mmu_comb;
    }

    _LAZY_COMB(debug_cache_comb, TribeCacheDebug)
        debug_cache_comb.icache_read_valid = icache.read_valid_out();
        debug_cache_comb.icache_read_addr = icache.read_addr_out();
        debug_cache_comb.icache_read_in = icache.read_in();
        debug_cache_comb.icache_stall_in = icache.stall_in();
        debug_cache_comb.dcache_read_valid = dcache.read_valid_out();
        debug_cache_comb.dcache_read_addr = dcache.read_addr_out();
        debug_cache_comb.dcache_read_data = dcache.read_data_out();
        debug_cache_comb.dcache_cpu_read = dcache.read_in();
        debug_cache_comb.dcache_cpu_write = dcache.write_in();
        debug_cache_comb.dcache_cpu_addr = dcache.addr_in();
        debug_cache_comb.dcache_cpu_wdata = dcache.write_data_in();
        debug_cache_comb.dcache_cpu_wmask = dcache.write_mask_in();
        return debug_cache_comb;
    }

    _LAZY_COMB(debug_wb_comb, TribeWritebackDebug)
        debug_wb_comb.load_ready = wb_mem.load_ready_out();
        debug_wb_comb.mem_wait = state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && !wb_mem.load_ready_out();
        debug_wb_comb.load_data_valid = wb_mem.debug_load_data_valid_out();
        debug_wb_comb.load_addr = wb_mem.debug_load_addr_out();
        debug_wb_comb.split_low_valid = wb_mem.debug_split_low_valid_out();
        debug_wb_comb.split_high_valid = wb_mem.debug_split_high_valid_out();
        debug_wb_comb.held_load_valid = wb_mem.debug_held_load_valid_out();
        debug_wb_comb.split_load_in = exe_mem.split_load_out();
        debug_wb_comb.alu_addr = wb_mem.alu_result_in();
        debug_wb_comb.state_pc = (uint32_t)state_reg[1].pc;
        debug_wb_comb.state_wb_op = (uint8_t)state_reg[1].wb_op;
        debug_wb_comb.state_mem_op = (uint8_t)state_reg[1].mem_op;
        debug_wb_comb.state_rd = (uint8_t)state_reg[1].rd;
        debug_wb_comb.state_funct3 = (uint8_t)state_reg[1].funct3;
        return debug_wb_comb;
    }

    _LAZY_COMB(debug_csr_comb, TribeCsrDebug)
        debug_csr_comb.satp = csr.satp_out();
        debug_csr_comb.mstatus = csr.mstatus_out();
        debug_csr_comb.mtvec = csr.mtvec_out();
        debug_csr_comb.mepc = csr.mepc_out();
        debug_csr_comb.mcause = csr.mcause_out();
        debug_csr_comb.mtval = csr.mtval_out();
        debug_csr_comb.sepc = csr.sepc_out();
        debug_csr_comb.stvec = csr.stvec_out();
        debug_csr_comb.scause = csr.scause_out();
        debug_csr_comb.stval = csr.stval_out();
        debug_csr_comb.priv = csr.priv_out();
        return debug_csr_comb;
    }

    _LAZY_COMB(debug_irq_comb, TribeIrqDebug)
        debug_irq_comb.valid = irq.interrupt_valid_out();
        debug_irq_comb.cause = irq.interrupt_cause_out();
        debug_irq_comb.to_supervisor = irq.interrupt_to_supervisor_out();
        debug_irq_comb.mip = irq.mip_out();
        debug_irq_comb.mie = csr.mie_out();
        debug_irq_comb.mideleg = csr.mideleg_out();
        return debug_irq_comb;
    }

    _LAZY_COMB(debug_regs_comb, TribeRegsDebug)
        debug_regs_comb.ra = regs.x1_out();
        debug_regs_comb.write = wb.regs_write_out();
        debug_regs_comb.write_actual = wb.regs_write_out() &&
            !memory_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || wb_mem.load_ready_out());
        debug_regs_comb.wr_id = wb.regs_wr_id_out();
        debug_regs_comb.data = wb.regs_data_out();
        return debug_regs_comb;
    }

    _LAZY_COMB(debug_branch_comb, TribeBranchDebug)
        debug_branch_comb.taken_now = exe.branch_taken_out();
        debug_branch_comb.target_now = exe.branch_target_out();
        return debug_branch_comb;
    }

    _LAZY_COMB(debug_decode_comb, TribeDecodeDebug)
        debug_decode_comb.instr = fetch_instr_reg;
        debug_decode_comb.pc = (uint32_t)dec.state_out().pc;
        debug_decode_comb.br = (uint8_t)dec.state_out().br_op;
        debug_decode_comb.imm = (uint32_t)dec.state_out().imm;
        return debug_decode_comb;
    }
#endif

    _LAZY_COMB(debug_sbi_comb, TribeSbiDebug)
        debug_sbi_comb.ecall = sbi_ecall_debug_comb_func();
        debug_sbi_comb.a7 = sbi_a7_debug_comb_func();
        debug_sbi_comb.a6 = sbi_a6_debug_comb_func();
        debug_sbi_comb.a0 = sbi_a0_debug_comb_func();
        debug_sbi_comb.base = sbi_base_comb_func();
        debug_sbi_comb.noop = sbi_noop_comb_func();
        debug_sbi_comb.handled = sbi_handled_comb_func();
        return debug_sbi_comb;
    }

private:

    reg<u32>        pc;
    reg<u1>         valid;

    // Explicit instruction-fetch response stage.  The cache response address
    // is retained with the instruction so redirects can discard stale data
    // without relying on the live I-cache output.
    reg<u1>         fetch_buffer_valid_reg;
    reg<u32>        fetch_instr_reg;
    reg<u32>        fetch_pc_reg;

    reg<u32>        alu_result_reg;
    reg<array<STAGES_NUM-1,State>> state_reg;
    reg<array<STAGES_NUM-1,u32>> predicted_next_reg;
    reg<array<STAGES_NUM-1,u32>> fallthrough_reg;
    reg<array<STAGES_NUM-1,u1>> predicted_taken_reg;

    // debug
    reg<u32>        debug_alu_a_reg;
    reg<u32>        debug_alu_b_reg;
    reg<u32>        debug_branch_target_reg;
    reg<u1>         debug_branch_taken_reg;
    reg<u1>         output_write_active_reg;
    reg<u1>         interrupt_entry_guard_reg;
    // Capture interrupt metadata before redirecting the CSR and fetch paths.
    // The register boundary removes memory readiness from the same-cycle
    // trap-vector/front-end control cone.
    reg<u1>         interrupt_accept_reg;
    reg<u32>        interrupt_cause_reg;
    reg<u1>         interrupt_to_supervisor_reg;
    reg<u1>         sbi_ret_a1_valid_reg;
    reg<u32>        sbi_ret_a1_reg;
#ifdef ENABLE_MMU_TLB
    // Instruction translation is also an explicit 312 MHz stage. The I-cache
    // and fault/CSR logic consume only this registered result, never the live
    // associative TLB lookup.
    reg<u1>         immu_result_valid_reg;
    reg<u32>        immu_vaddr_reg;
    reg<u32>        immu_paddr_reg;
    reg<u1>         immu_fault_reg;
    // Data translation is a separate 312 MHz pipeline stage.  The memory-stage
    // virtual address remains held until these registers contain its DMMU
    // result; D-cache then sees only a registered physical address/fault.
    reg<u1>         dmmu_result_valid_reg;
    reg<u32>        dmmu_vaddr_reg;
    reg<u32>        dmmu_paddr_reg;
    reg<u1>         dmmu_fault_reg;
    // D-cache remains the implicit owner of the shared data port. Page-table
    // walks acquire a registered grant only while D-cache has no outgoing L2
    // request, and retain it until the granted MMU drops mem_read_out.
    static constexpr uint8_t L2_PTW_OWNER_NONE = 0;
    static constexpr uint8_t L2_PTW_OWNER_DMMU = 1;
    static constexpr uint8_t L2_PTW_OWNER_IMMU = 2;
    reg<u<2>>       l2_ptw_owner_reg;
#endif

#if defined(MULTICORE) && defined(ENABLE_RV32IA)
    // Request cluster ownership for the complete lifetime of an AMO instruction.
    _LAZY_COMB(atomic_request_comb, bool)
        return atomic_request_comb =
            (state_reg[0].valid && state_reg[0].amo_op != Amo::AMONONE) ||
            (state_reg[1].valid && state_reg[1].amo_op != Amo::AMONONE);
    }

    // Classify the currently driven D-memory request independently from a
    // younger AMO waiting in execute, so the cluster does not hide an older
    // ordinary response when it grants atomic ownership.
    _LAZY_COMB(atomic_data_request_comb, bool)
        return atomic_data_request_comb = state_reg[1].valid &&
            state_reg[1].amo_op != Amo::AMONONE;
    }

    // End cluster ownership when the memory-stage AMO can retire. Looking only
    // for an empty AMO pipeline deadlocks on tight back-to-back AMO loops.
    _LAZY_COMB(atomic_complete_comb, bool)
        return atomic_complete_comb = atomic_grant_in() && state_reg[1].valid &&
            state_reg[1].amo_op != Amo::AMONONE && !memory_wait_comb_func();
    }

#endif

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

    // Select the shared-L2 data address once so the request and its cache-level
    // bypass attribute always describe the same D-cache or page-table access.
    _LAZY_COMB(l2_data_addr_comb, uint32_t)
#ifdef ENABLE_MMU_TLB
        l2_data_addr_comb = dmmu_ptw_selected_comb_func() ?
            (uint32_t)dmmu.mem_addr_out() :
            (immu_ptw_selected_comb_func() ? (uint32_t)immu.mem_addr_out() :
                (uint32_t)dcache.mem_out.addr_in());
#else
        l2_data_addr_comb = dcache.mem_out.addr_in();
#endif
        return l2_data_addr_comb;
    }

#ifdef ENABLE_MMU_TLB
    // Registered page-table-walk grants sever live D-cache hit/miss selection
    // from both MMU state machines.
    _LAZY_COMB(dmmu_ptw_selected_comb, bool)
        dmmu_ptw_selected_comb = l2_ptw_owner_reg == L2_PTW_OWNER_DMMU;
        return dmmu_ptw_selected_comb;
    }

    _LAZY_COMB(immu_ptw_selected_comb, bool)
        immu_ptw_selected_comb = l2_ptw_owner_reg == L2_PTW_OWNER_IMMU;
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
        mmu_l2_read_word_comb = (uint32_t)d_mem_out.read_data_out().bits(lane * 32 + 31, lane * 32);
        return mmu_l2_read_word_comb;
    }

    _LAZY_COMB(immu_result_match_comb, bool)
        immu_result_match_comb = valid && immu_result_valid_reg &&
            (uint32_t)immu_vaddr_reg == (uint32_t)pc;
        return immu_result_match_comb;
    }

    _LAZY_COMB(immu_active_fault_comb, bool)
        immu_active_fault_comb = immu_result_match_comb_func() && immu_fault_reg;
        return immu_active_fault_comb;
    }

    // Do not let D-cache consume dmmu.paddr_out until the current translated access has a TLB hit.
    _LAZY_COMB(dmmu_access_ready_comb, bool)
        bool access;
        access = state_reg[1].valid && (exe_mem.mem_read_out() || exe_mem.mem_write_out());
        dmmu_access_ready_comb = !access || !dmmu.translated_out() || dmmu.hit_out() || dmmu.fault_out();
        return dmmu_access_ready_comb;
    }

    _LAZY_COMB(dmmu_vaddr_comb, uint32_t)
        dmmu_vaddr_comb = exe_mem.mem_read_out() ?
            (uint32_t)exe_mem.mem_read_addr_out() :
            (uint32_t)exe_mem.mem_write_addr_out();
        return dmmu_vaddr_comb;
    }

    _LAZY_COMB(dmmu_result_match_comb, bool)
        bool live_request;
        bool memory_owner;
        live_request = state_reg[1].valid &&
            (exe_mem.mem_read_out() || exe_mem.mem_write_out());
        memory_owner = state_reg[1].valid &&
            (live_request || state_reg[1].mem_op == Mem::STORE ||
                state_reg[1].wb_op == Wb::MEM);
        // ExecuteMem drops its read/write level after the cache accepts the
        // transaction, while WritebackMem deliberately keeps the same
        // instruction in the memory stage for two registered response
        // boundaries.  With no live request there is only one possible owner,
        // so the saved translation remains its match until retirement.
        dmmu_result_match_comb = memory_owner && dmmu_result_valid_reg &&
            (!live_request || (uint32_t)dmmu_vaddr_reg == dmmu_vaddr_comb_func());
        return dmmu_result_match_comb;
    }
#endif

    // Pipeline wait for operations already in decode/execute or memory/writeback.
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
        dmmu_faulted_access = dmmu_result_match_comb_func() && dmmu_fault_reg;
#endif
        memory_wait_comb =
#ifdef ENABLE_ISR
            interrupt_capture_comb_func() ||
#endif
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            (atomic_request_comb_func() && !atomic_grant_in()) ||
#endif
#ifdef ENABLE_RV32IA
            (data_mem_access && !dmmu_faulted_access && exe_mem.atomic_busy_out()) ||
#endif
#ifdef ENABLE_MMU_TLB
            // Hold prefetched instructions while an instruction translation is
            // pending. An older data-memory operation is the exception: it must
            // retire so the shared D-memory path remains available to the page
            // table walk that will resume instruction fetch.
            ((bool)valid && !immu_result_match_comb_func() && !data_mem_access) ||
            (data_mem_access && !dmmu_faulted_access && !dmmu_result_match_comb_func()) ||
#endif
            sbi_arg_hazard_comb_func() ||
            exe.multicycle_wait_out() ||
            (next_data_mem_access && dcache.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && dcache.busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && exe_mem.mem_split_busy_out()) ||
            (data_mem_access && !dmmu_faulted_access && dcache.mem_out.read_in() && d_mem_out.wait_out()) ||
            (data_mem_access && !dmmu_faulted_access &&
                (exe_mem.mem_write_out() || state_reg[1].mem_op == Mem::STORE) && d_mem_out.wait_out()) ||
            (state_reg[0].valid && state_reg[0].sys_op == Sys::FENCE &&
                (dcache.busy_out() || d_mem_out.wait_out() || i_mem_out.wait_out())) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !dmmu_faulted_access &&
            !wb_mem.load_ready_out());
        return memory_wait_comb;
    }

    // A live I-cache response may enter the fetch-response register only when
    // it belongs to the current architectural fetch PC.
    _LAZY_COMB(icache_response_match_comb, bool)
        return icache_response_match_comb = valid && icache.read_valid_out() &&
#ifdef ENABLE_MMU_TLB
            // I-cache stores physical refill addresses; the architectural PC can be virtual.
            immu_result_match_comb_func() &&
            icache.read_addr_out() == (uint32_t)immu_paddr_reg;
#else
            icache.read_addr_out() == (uint32_t)pc;
#endif
    }

    // Decode sees only a clocked fetch response.  Comparing the saved virtual
    // PC rejects an old response immediately after a branch or trap redirect.
    _LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && fetch_buffer_valid_reg &&
            (uint32_t)fetch_pc_reg == (uint32_t)pc;
    }

    // Sequential next PC respects 16-bit compressed and 32-bit instructions.
    _LAZY_COMB(decode_fallthrough_comb, uint32_t)
        return decode_fallthrough_comb = fetch_pc_reg + ((dec.instr_in()&3)==3?4:2);
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

    // Fetch address is deliberately registered.  Redirect resolution writes
    // pc in _work(), and the corrected translation/cache request starts on the
    // following cycle.
    _LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        return fetch_addr_comb;
    }

    static constexpr uint32_t SBI_EXT_BASE = 0x10;
    static constexpr uint32_t SBI_EXT_TIME = 0x54494d45;
    static constexpr uint32_t SBI_EXT_RFENCE = 0x52464e43;
#ifdef MULTICORE
    static constexpr uint32_t SBI_EXT_IPI = 0x735049;
#endif
    static constexpr uint32_t SBI_SUCCESS = 0;

    // SBI ECALLs are handled locally because this model has no M-mode firmware.
    bool sbi_legacy_ecall_comb;
    bool& sbi_legacy_ecall_comb_func()
    {
        return sbi_legacy_ecall_comb = state_reg[0].valid &&
            state_reg[0].sys_op == Sys::ECALL &&
            csr.priv_out() == (u<2>)1;
    }

    // Architectural writeback pulse.  Keeping this separate from the global
    // front-end wait allows an older result to commit while fetch is stopped.
    _LAZY_COMB(register_write_commit_comb, bool)
        return register_write_commit_comb = wb.regs_write_out() &&
            !interrupt_retire_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || wb_mem.load_ready_out());
    }

    // ECALL arguments a0/a1/a6/a7 are implicit and therefore absent from the
    // decoded rs fields.  If one is written in this cycle, hold the ECALL once
    // so it observes the clocked register-file value on the following cycle.
    _LAZY_COMB(sbi_arg_hazard_comb, bool)
        uint32_t rd;
        bool writes_argument;
        rd = wb.regs_wr_id_out();
        writes_argument = rd == 10 || rd == 11 || rd == 16 || rd == 17;
        return sbi_arg_hazard_comb = sbi_legacy_ecall_comb_func() &&
            !sbi_arg_wait_reg && register_write_commit_comb_func() &&
            writes_argument;
    }

    uint32_t sbi_arg_value(uint8_t reg_id)
    {
        // A preceding implicit-argument write is serialized by
        // sbi_arg_hazard_comb_func(), so all SBI logic reads registered values.
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
            (ext == 5 || ext == 6 || ext == 7
#ifndef MULTICORE
             || ext == SBI_EXT_RFENCE
#endif
            );
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
                    probe_ext == SBI_EXT_RFENCE
#if defined(MULTICORE) && defined(ENABLE_ISR)
                    || probe_ext == SBI_EXT_IPI
#endif
                    ) ? 1 : 0;
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
        return sbi_writes_a1_comb = sbi_base_comb_func() || sbi_set_timer_comb_func()
#if defined(MULTICORE) && defined(ENABLE_ISR)
            || sbi_send_ipi_comb_func() || sbi_remote_fence_i_comb_func() ||
#ifdef ENABLE_MMU_TLB
                sbi_remote_sfence_vma_comb_func()
#else
                false
#endif
#endif
            ;
    }

    // All locally handled SBI calls retire as successful calls with a0=0.
    bool sbi_handled_comb;
    bool& sbi_handled_comb_func()
    {
        return sbi_handled_comb = sbi_set_timer_comb_func() || sbi_noop_comb_func() || sbi_base_comb_func()
#if defined(MULTICORE) && defined(ENABLE_ISR)
            || sbi_send_ipi_comb_func() || sbi_remote_fence_i_comb_func() ||
#ifdef ENABLE_MMU_TLB
                sbi_remote_sfence_vma_comb_func()
#else
                false
#endif
#endif
            ;
    }

#if defined(MULTICORE) && defined(ENABLE_ISR)
    // SBI v0.2 IPI requests carry a direct hart mask and base in a0/a1.
    _LAZY_COMB(sbi_send_ipi_comb, bool)
        return sbi_send_ipi_comb = sbi_legacy_ecall_comb_func() &&
            sbi_arg_value(17) == SBI_EXT_IPI && sbi_arg_value(16) == 0;
    }

    // Remote FENCE.I invalidates instruction caches selected by the SBI hart mask.
    _LAZY_COMB(sbi_remote_fence_i_comb, bool)
        return sbi_remote_fence_i_comb = sbi_legacy_ecall_comb_func() &&
            sbi_arg_value(17) == SBI_EXT_RFENCE && sbi_arg_value(16) == 0;
    }

    // Both remote SFENCE.VMA variants can conservatively invalidate the full target TLB.
#ifdef ENABLE_MMU_TLB
    _LAZY_COMB(sbi_remote_sfence_vma_comb, bool)
        uint32_t function_id;
        function_id = sbi_arg_value(16);
        return sbi_remote_sfence_vma_comb = sbi_legacy_ecall_comb_func() &&
            sbi_arg_value(17) == SBI_EXT_RFENCE && (function_id == 1 || function_id == 2);
    }
#endif

    _LAZY_COMB(sbi_hart_mask_comb, uint32_t)
        return sbi_hart_mask_comb = sbi_arg_value(10);
    }

    _LAZY_COMB(sbi_hart_base_comb, uint32_t)
        return sbi_hart_base_comb = sbi_arg_value(11);
    }
#endif

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

    // Capture an interrupt only at a normal execute boundary. The capture
    // cycle holds the pipeline; CSR/PC redirection uses the registered event
    // on the following cycle.
    _LAZY_COMB(interrupt_capture_comb, bool)
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
        return interrupt_capture_comb = state_reg[0].valid && irq.interrupt_valid_out() &&
            !interrupt_entry_guard_reg && !interrupt_accept_reg && !trap_redirect
#ifdef ENABLE_RV32IA
            // Do not flush an atomic instruction in the cycle before
            // ExecuteMem's registered atomic_busy indication becomes visible.
            && state_reg[0].amo_op == Amo::AMONONE
#endif
            // A decode/load-use stall is younger than this execute boundary
            // and will be flushed by trap entry. Including it here creates a
            // redirect -> Execute -> split hazard -> interrupt feedback loop.
            && !interrupt_retire_wait_comb_func();
#else
        return interrupt_capture_comb = false;
#endif
    }

    _LAZY_COMB(interrupt_accept_comb, bool)
#ifdef ENABLE_ISR
        return interrupt_accept_comb = interrupt_accept_reg && state_reg[0].valid;
#else
        return interrupt_accept_comb = false;
#endif
    }

    // Only an older memory-stage instruction can prevent interrupt entry.  A
    // front-end translation wait or the current Execute result is restartable;
    // including either in this decision closes a redirect -> fetch/MMU -> wait
    // -> interrupt loop.  All signals below originate in registered memory,
    // cache, MMU, and pipeline state.
    _LAZY_COMB(interrupt_retire_wait_comb, bool)
        bool data_mem_access;
        bool dmmu_faulted_access;
        data_mem_access = state_reg[1].valid && (
            exe_mem.mem_read_out() || exe_mem.mem_write_out() ||
            state_reg[1].mem_op == Mem::STORE || state_reg[1].wb_op == Wb::MEM);
        dmmu_faulted_access = false;
#ifdef ENABLE_MMU_TLB
        dmmu_faulted_access = dmmu_result_match_comb_func() && dmmu_fault_reg;
#endif
        interrupt_retire_wait_comb =
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            (state_reg[1].valid && state_reg[1].amo_op != Amo::AMONONE &&
                !atomic_grant_in()) ||
#endif
#ifdef ENABLE_RV32IA
            (data_mem_access && !dmmu_faulted_access && exe_mem.atomic_busy_out()) ||
#endif
#ifdef ENABLE_MMU_TLB
            (data_mem_access && !dmmu_faulted_access && !dmmu_result_match_comb_func()) ||
#endif
            // Load completion is represented by WritebackMem's registered
            // result-ready bit below. Stores that miss are covered by the
            // shared-memory wait terms. A live L1 busy/hit decision here would
            // otherwise drive every CSR clock enable in the same cycle.
            (data_mem_access && !dmmu_faulted_access && exe_mem.mem_split_busy_out()) ||
            (data_mem_access && !dmmu_faulted_access &&
                dcache.mem_out.read_in() && d_mem_out.wait_out()) ||
            (data_mem_access && !dmmu_faulted_access &&
                (exe_mem.mem_write_out() || state_reg[1].mem_op == Mem::STORE) &&
                d_mem_out.wait_out()) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
                !dmmu_faulted_access && !wb_mem.load_ready_out());
        return interrupt_retire_wait_comb;
    }

    // Execute sees only registered pipeline state.  Synthetic SBI completion
    // is constructed in its prioritized retirement branch below; exposing the
    // a7/a6/a0 SBI decode to Execute couples register-file reads to branch
    // resolution, front-end stall, instruction translation, and CSR commit.
    _LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
#ifdef ENABLE_ZICSR
        // Trap/xRET/FENCE.I redirection is handled by the explicit, prioritized
        // paths in _work().  Do not rewrite Execute operands or branch controls
        // for those operations: their results are discarded at the same edge,
        // while exposing them to Execute creates a redirect -> fetch/MMU/cache
        // -> global-wait feedback path.  Decoded system instructions already
        // carry BNONE/MNONE/WNONE where required.
#endif
        // Load forwarding is captured at a clock boundary: forward() supplies
        // a newly decoded consumer when the pipe advances, and the memory-wait
        // path below updates an already-held consumer.  Driving the live
        // Execute operands directly from D-cache data would couple the L1 tag
        // lookup to branch/SBI redirect and the entire instruction front end.
        return exe_state_comb;
    }

#if defined(ENABLE_ZICSR) && defined(ENABLE_MMU_TLB)
    // Page-fault vector selection depends only on registered CSR state.  It is
    // deliberately separate from CSR's current-instruction redirect output so
    // an instruction-fetch fault cannot feed back into its own virtual address.
    _LAZY_COMB(inst_page_fault_vector_comb, uint32_t)
        uint32_t tvec;
        tvec = ((uint32_t)csr.priv_out() != 3u &&
            (((uint32_t)csr.medeleg_out() >> 12) & 1u)) ?
            (uint32_t)csr.stvec_out() : (uint32_t)csr.mtvec_out();
        return inst_page_fault_vector_comb = tvec & ~3u;
    }

    _LAZY_COMB(data_page_fault_vector_comb, uint32_t)
        uint32_t cause;
        uint32_t tvec;
        cause = exe_mem.mem_write_out() ? 15u : 13u;
        tvec = ((uint32_t)csr.priv_out() != 3u &&
            (((uint32_t)csr.medeleg_out() >> cause) & 1u)) ?
            (uint32_t)csr.stvec_out() : (uint32_t)csr.mtvec_out();
        return data_page_fault_vector_comb = tvec & ~3u;
    }
#endif

#ifdef ENABLE_ZICSR
#ifdef ENABLE_MMU_TLB
    // DMMU faults matter only while a valid memory-stage instruction is still
    // actively reading or writing. After a trap flush the DMMU fault output can
    // remain asserted for one cycle, but it must not overwrite sepc/stval.
    _LAZY_COMB(dmmu_active_fault_comb, bool)
        return dmmu_active_fault_comb = dmmu_result_match_comb_func() && dmmu_fault_reg;
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
        if (immu_active_fault_comb_func() && !state_reg[0].valid && !state_reg[1].valid) {
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
        // CSR/trap commit waits only for an older memory-stage instruction.
        // Front-end translation/cache readiness must not feed the CSR write
        // enable or make a completed CSR instruction execute repeatedly.
        if (interrupt_retire_wait_comb_func()
#ifdef ENABLE_ISR
            && !interrupt_accept_comb_func()
#endif
#ifdef ENABLE_MMU_TLB
            && !immu_active_fault_comb_func() && !dmmu_active_fault_comb_func()
#endif
            ) {
            csr_state_comb.valid = false;
        }
        return csr_state_comb;
    }
#endif

    // FENCE.I discards fetched instructions. SFENCE.VMA invalidates address
    // translations only and must not turn every context switch into an I-cache clear.
    _LAZY_COMB(icache_invalidate_comb, bool)
        return icache_invalidate_comb =
#if defined(MULTICORE) && defined(ENABLE_ISR)
            remote_fence_i_in() ||
#endif
            (state_reg[0].valid &&
            state_reg[0].sys_op == Sys::FENCEI &&
            !memory_wait_comb_func() && !icache_invalidate_issued_reg);
    }

    // SFENCE.VMA invalidates cached translations once the instruction can retire.
    _LAZY_COMB(sfence_vma_comb, bool)
        return sfence_vma_comb =
#if defined(MULTICORE) && defined(ENABLE_MMU_TLB)
            remote_sfence_vma_in() ||
#endif
            (state_reg[0].valid && state_reg[0].sys_op == Sys::SFENCE_VMA && !memory_wait_comb_func());
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
        static const char* trace_pc_write_from_env = std::getenv("TRIBE_TRACE_PC_WRITE_FROM");
        static const char* trace_pc_write_target_env = std::getenv("TRIBE_TRACE_PC_WRITE_TARGET");
        static const char* trace_pc_write_reason_env = std::getenv("TRIBE_TRACE_PC_WRITE_REASON");
        static const char* trace_pc_write_file_env = std::getenv("TRIBE_TRACE_PC_WRITE_FILE");
        static const bool trace_pc_write_all = std::getenv("TRIBE_TRACE_PC_WRITE_ALL") != nullptr;
        static const bool trace_pc_write_zero_only = std::getenv("TRIBE_TRACE_PC_WRITE_ZERO_ONLY") != nullptr;
        static const bool trace_pc_write_user_kernel = std::getenv("TRIBE_TRACE_PC_WRITE_USER_KERNEL") != nullptr;
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
            std::print(trace_pc_write_out, "trace-pc-write inst={} cycle={} reason={} pc={:08x} next={:08x} valid={} fetch_valid={} memwait={} stall={} hazard={} branch_mispredict={} branch_target={:08x} predicted={:08x} state0_valid={} state0_pc={:08x} state0_wb={} state0_rd={} state0_sys={} state0_trap={} state0_br={} state1_valid={} state1_pc={:08x} state1_wb={} state1_rd={}",
                __inst_name,
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
#if defined(ENABLE_RV32IA) && defined(MULTICORE)
            std::print(trace_pc_write_out,
                " atomic_request={} atomic_grant={} atomic_data={} atomic_complete={} atomic_busy={} dcache_valid={} dcache_addr={:08x} wb_ready={}",
                (bool)atomic_request_comb_func(),
                (bool)atomic_grant_in(),
                (bool)atomic_data_request_comb_func(),
                (bool)atomic_complete_comb_func(),
                (bool)exe_mem.atomic_busy_out(),
                (bool)dcache.read_valid_out(),
                (uint32_t)dcache.read_addr_out(),
                (bool)wb_mem.load_ready_out());
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
#ifdef ENABLE_MMU_TLB
        l2_ptw_owner_reg._next = l2_ptw_owner_reg;
        if (l2_ptw_owner_reg == L2_PTW_OWNER_NONE) {
            if (!(dcache.mem_out.read_in() || dcache.mem_out.write_in())) {
                if (dmmu.mem_read_out()) {
                    l2_ptw_owner_reg._next = L2_PTW_OWNER_DMMU;
                }
                else if (immu.mem_read_out()) {
                    l2_ptw_owner_reg._next = L2_PTW_OWNER_IMMU;
                }
            }
        }
        else if ((l2_ptw_owner_reg == L2_PTW_OWNER_DMMU && !dmmu.mem_read_out()) ||
                 (l2_ptw_owner_reg == L2_PTW_OWNER_IMMU && !immu.mem_read_out())) {
            l2_ptw_owner_reg._next = L2_PTW_OWNER_NONE;
        }
#endif
        fetch_buffer_valid_reg._next = fetch_buffer_valid_reg;
        fetch_instr_reg._next = fetch_instr_reg;
        fetch_pc_reg._next = fetch_pc_reg;
        if (!valid || (fetch_buffer_valid_reg && (uint32_t)fetch_pc_reg != (uint32_t)pc)) {
            fetch_buffer_valid_reg._next = false;
        }
        if (icache_response_match_comb_func() &&
            (!fetch_buffer_valid_reg || (uint32_t)fetch_pc_reg != (uint32_t)pc)) {
            fetch_buffer_valid_reg._next = true;
            fetch_instr_reg._next = icache.read_data_out();
            fetch_pc_reg._next = pc;
        }
        sbi_ret_a1_valid_reg._next = false;
        sbi_ret_a1_reg._next = sbi_ret_a1_reg;
#ifdef ENABLE_MMU_TLB
        if (!valid) {
            immu_result_valid_reg._next = false;
        }
        else if (!immu_result_match_comb_func() && !immu.busy_out()) {
            immu_result_valid_reg._next = true;
            immu_vaddr_reg._next = pc;
            immu_paddr_reg._next = immu.paddr_out();
            immu_fault_reg._next = immu.fault_out();
        }
        if (!(state_reg[1].valid &&
            (exe_mem.mem_read_out() || exe_mem.mem_write_out() ||
                state_reg[1].mem_op == Mem::STORE || state_reg[1].wb_op == Wb::MEM))) {
            dmmu_result_valid_reg._next = false;
        }
        else if (!dmmu_result_match_comb_func() && !dmmu.busy_out() &&
            dmmu_access_ready_comb_func()) {
            dmmu_result_valid_reg._next = true;
            dmmu_vaddr_reg._next = dmmu_vaddr_comb_func();
            dmmu_paddr_reg._next = dmmu.paddr_out();
            dmmu_fault_reg._next = dmmu.fault_out();
        }
        if (sfence_vma_comb_func()) {
            immu_result_valid_reg._next = false;
            dmmu_result_valid_reg._next = false;
        }
#endif
#ifdef ENABLE_ISR
        interrupt_accept_reg._next = interrupt_accept_reg;
        interrupt_cause_reg._next = interrupt_cause_reg;
        interrupt_to_supervisor_reg._next = interrupt_to_supervisor_reg;
        if (interrupt_accept_comb_func()) {
            interrupt_accept_reg._next = false;
        }
        else if (interrupt_capture_comb_func()) {
            interrupt_accept_reg._next = true;
            interrupt_cause_reg._next = irq.interrupt_cause_out();
            interrupt_to_supervisor_reg._next = irq.interrupt_to_supervisor_out();
        }
#endif
        if (!state_reg[0].valid || state_reg[0].sys_op != Sys::ECALL) {
            sbi_arg_wait_reg._next = false;
        }
        else if (sbi_arg_hazard_comb_func()) {
            sbi_arg_wait_reg._next = true;
        }
        else if (!memory_wait_comb_func()) {
            sbi_arg_wait_reg._next = false;
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
            interrupt_entry_guard_reg._next = false;
        }
        else
        if (!reset && state_reg[0].valid &&
            (!interrupt_retire_wait_comb_func()
#ifdef ENABLE_ISR
             || interrupt_accept_comb_func()
#endif
            ) &&
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
        if (!reset && immu_active_fault_comb_func() && (state_reg[0].valid || state_reg[1].valid) &&
            !dmmu_active_fault_comb_func() && !memory_wait_comb_func()) {
            // Fetch faults are younger than both pipeline stages. Drain one
            // stage per cycle so loads and JAL/JALR links retire before the
            // fault updates sepc and flushes the pipe. A waiting memory-stage
            // instruction is held by the normal memory-wait path below.
            pc._next = pc;
#ifndef SYNTHESIS
            trace_pc_write("fetch-fault-drain", (uint32_t)pc);
#endif
            // Keep the faulting translation request asserted while the older
            // instruction retires. Otherwise MMU_TLB clears its fault when
            // execute_in drops and the next cycle restarts the same walk
            // instead of entering the trap handler.
            valid._next = true;
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
        if (!reset && (immu_active_fault_comb_func() || dmmu_active_fault_comb_func())) {
            pc._next = dmmu_active_fault_comb_func() ?
                data_page_fault_vector_comb_func() : inst_page_fault_vector_comb_func();
#ifndef SYNTHESIS
            trace_pc_write("mmu-fault-trap", dmmu_active_fault_comb_func() ?
                data_page_fault_vector_comb_func() : inst_page_fault_vector_comb_func());
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
            debug_branch_target_reg._next = dmmu_active_fault_comb_func() ?
                data_page_fault_vector_comb_func() : inst_page_fault_vector_comb_func();
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
            state_reg._next[1] = state_reg[0];
            state_reg._next[1].sys_op = Sys::SNONE;
            state_reg._next[1].trap_op = Trap::TNONE;
            state_reg._next[1].csr_op = Csr::CNONE;
            state_reg._next[1].mem_op = Mem::MNONE;
            state_reg._next[1].br_op = Br::BNONE;
            state_reg._next[1].alu_op = Alu::ADD;
            state_reg._next[1].rs1_val = 0;
            state_reg._next[1].rs2_val = 0;
            state_reg._next[1].imm = 0;
            state_reg._next[1].rd = 10;
            state_reg._next[1].wb_op = Wb::ALU;
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

            if (branch_mispredict_comb_func()) {
                // Redirect resolution has priority over all live fetch/decode
                // inputs.  Materializing the bubble explicitly prevents the
                // branch result from reaching every State bit through the
                // I-cache/MMU/fetch-valid selection cone.
                state_reg._next[0] = State{};
                state_reg._next[0].valid = false;
                predicted_next_reg._next[0] = pc;
                fallthrough_reg._next[0] = pc;
                predicted_taken_reg._next[0] = false;
            }
            else if (hazard_stall_comb_func()) {
                state_reg._next[0] = State{};
                state_reg._next[0].valid = false;
                predicted_next_reg._next[0] = pc;
                fallthrough_reg._next[0] = pc;
                predicted_taken_reg._next[0] = false;
            }
            else {
                if (fetch_valid_comb_func()) {
                    state_reg._next[0] = dec.state_out();
                    // Align the CSR legality result with the instruction as it
                    // crosses the decode/execute register.  Copies, holds, and
                    // flushes of State then preserve alignment automatically.
                    state_reg._next[0].csr_illegal = csr.legality_out();
                    state_reg._next[0].valid = dec.instr_valid_in() && !branch_stall_comb_func() && !branch_flush_comb_func();
                    predicted_next_reg._next[0] = decode_fallthrough_comb_func();
                    fallthrough_reg._next[0] = decode_fallthrough_comb_func();
                    predicted_taken_reg._next[0] = false;
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
        bp._work(reset);

        if (state_reg[0].valid && state_reg[0].sys_op == Sys::FENCEI &&
            !memory_wait_comb_func()) {
            icache_invalidate_issued_reg._next = true;
        }
        else if (!state_reg[0].valid || state_reg[0].sys_op != Sys::FENCEI) {
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
            fetch_buffer_valid_reg.clr();
            fetch_instr_reg.clr();
            fetch_pc_reg.clr();
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
            output_write_active_reg.clr();
            interrupt_entry_guard_reg.clr();
#ifdef ENABLE_ISR
            interrupt_accept_reg.clr();
            interrupt_cause_reg.clr();
            interrupt_to_supervisor_reg.clr();
#endif
            sbi_ret_a1_valid_reg.clr();
            sbi_ret_a1_reg.clr();
            icache_invalidate_issued_reg.clr();
            sbi_arg_wait_reg.clr();
#ifdef ENABLE_MMU_TLB
            immu_result_valid_reg.clr();
            immu_vaddr_reg.clr();
            immu_paddr_reg.clr();
            immu_fault_reg.clr();
            dmmu_result_valid_reg.clr();
            dmmu_vaddr_reg.clr();
            dmmu_paddr_reg.clr();
            dmmu_fault_reg.clr();
            l2_ptw_owner_reg.clr();
#endif
        }
    }

    void _work_neg(bool reset)
    {
    }

    void debug()
    {
        State tmp;
        Zicsr instr = {{{(uint32_t)fetch_instr_reg}}};
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
        // Fetch response state is transient and is refilled after restore.
        fetch_buffer_valid_reg.strobe();
        fetch_instr_reg.strobe();
        fetch_pc_reg.strobe();
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
#ifdef ENABLE_ISR
        interrupt_accept_reg.strobe(checkpoint_fd);
        interrupt_cause_reg.strobe(checkpoint_fd);
        interrupt_to_supervisor_reg.strobe(checkpoint_fd);
#endif
        sbi_ret_a1_valid_reg.strobe(checkpoint_fd);
        sbi_ret_a1_reg.strobe(checkpoint_fd);
        icache_invalidate_issued_reg.strobe(checkpoint_fd);
        sbi_arg_wait_reg.strobe(checkpoint_fd);
#ifdef ENABLE_MMU_TLB
        // Transient translation-stage state is deliberately omitted from the
        // checkpoint stream; restored fetch and memory operations simply
        // translate again before touching their L1 caches.
        immu_result_valid_reg.strobe();
        immu_vaddr_reg.strobe();
        immu_paddr_reg.strobe();
        immu_fault_reg.strobe();
        dmmu_result_valid_reg.strobe();
        dmmu_vaddr_reg.strobe();
        dmmu_paddr_reg.strobe();
        dmmu_fault_reg.strobe();
        l2_ptw_owner_reg.strobe();
#endif

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
    }


};
