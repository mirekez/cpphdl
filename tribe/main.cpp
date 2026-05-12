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
#undef L2_AXI_WIDTH
#else
static constexpr size_t TRIBE_L2_AXI_WIDTH = 256;  // default
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

// system configuration for cpp
static constexpr size_t DEFAULT_RAM_SIZE = 32768;
static constexpr size_t MAX_RAM_SIZE = 524288;
static constexpr size_t TRIBE_MEM_REGION0_SIZE = 256 * 1024;
static constexpr size_t TRIBE_MEM_REGION1_SIZE = 128 * 1024;
static constexpr size_t TRIBE_MEM_REGION2_SIZE = 64 * 1024;
static constexpr size_t TRIBE_IO_REGION_SIZE = MAX_RAM_SIZE - TRIBE_MEM_REGION0_SIZE - TRIBE_MEM_REGION1_SIZE - TRIBE_MEM_REGION2_SIZE;
static constexpr size_t TRIBE_RAM_BYTES = TRIBE_MEM_REGION0_SIZE + TRIBE_MEM_REGION1_SIZE + TRIBE_MEM_REGION2_SIZE;
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
    _PORT(uint32_t)  reset_pc_in;
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
        exe_mem.dcache_read_data_in = dcache.read_data_out;
#endif
        exe_mem.mem_stall_in = dcache.busy_out;
        exe_mem.hold_in = _ASSIGN( memory_wait_comb_func() );
        exe_mem.__inst_name = __inst_name + "/exe_mem";
        exe_mem._assign();

        wb_mem.state_in = _ASSIGN_REG(state_reg[1]);
        wb_mem.alu_result_in = _ASSIGN_REG(alu_result_reg);
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
        immu.execute_in = _ASSIGN(true);
#ifdef ENABLE_ZICSR
        immu.satp_in = csr.satp_out;
        immu.priv_in = csr.priv_out;
#else
        immu.satp_in = _ASSIGN((uint32_t)0);
        immu.priv_in = _ASSIGN((u<2>)3);
#endif
        immu.fill_in = _ASSIGN(false);
        immu.fill_index_in = _ASSIGN((u<3>)0);
        immu.fill_vpn_in = _ASSIGN((uint32_t)0);
        immu.fill_ppn_in = _ASSIGN((uint32_t)0);
        immu.fill_flags_in = _ASSIGN((uint8_t)0);
        immu.sfence_in = _ASSIGN(sfence_vma_comb_func());
        immu.__inst_name = __inst_name + "/immu";
        immu._assign();

        dmmu.vaddr_in = _ASSIGN(exe_mem.mem_read_out() ? (uint32_t)exe_mem.mem_read_addr_out() : (uint32_t)exe_mem.mem_write_addr_out());
        dmmu.read_in = exe_mem.mem_read_out;
        dmmu.write_in = exe_mem.mem_write_out;
        dmmu.execute_in = _ASSIGN(false);
#ifdef ENABLE_ZICSR
        dmmu.satp_in = csr.satp_out;
        dmmu.priv_in = csr.priv_out;
#else
        dmmu.satp_in = _ASSIGN((uint32_t)0);
        dmmu.priv_in = _ASSIGN((u<2>)3);
#endif
        dmmu.fill_in = _ASSIGN(false);
        dmmu.fill_index_in = _ASSIGN((u<3>)0);
        dmmu.fill_vpn_in = _ASSIGN((uint32_t)0);
        dmmu.fill_ppn_in = _ASSIGN((uint32_t)0);
        dmmu.fill_flags_in = _ASSIGN((uint8_t)0);
        dmmu.sfence_in = _ASSIGN(sfence_vma_comb_func());
        dmmu.__inst_name = __inst_name + "/dmmu";
        dmmu._assign();
#endif

        wb.state_in       = _ASSIGN_REG( state_reg[1] );
        wb.mem_data_in    = wb_mem.load_raw_out;
        wb.mem_data_hi_in = _ASSIGN((uint32_t)0);
        wb.mem_addr_in    = _ASSIGN_REG(alu_result_reg);
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
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = _ASSIGN( exe_mem.mem_read_out() && !dcache.busy_out() );
        dcache.write_in = _ASSIGN( exe_mem.mem_write_out() && !dcache.busy_out() );
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

        icache.read_in = _ASSIGN( true );
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
        l2cache.d_read_in = dcache.mem_read_out;
        l2cache.d_write_in = dcache.mem_write_out;
        l2cache.d_addr_in = dcache.mem_addr_out;
        l2cache.d_write_data_in = dcache.mem_write_data_out;
        l2cache.d_write_mask_in = dcache.mem_write_mask_out;
        l2cache.memory_base_in = memory_base_in;
        l2cache.memory_size_in = memory_size_in;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            l2cache.mem_region_size_in[i] = mem_region_size_in[i];
        }
        l2cache.mem_region_uncached_in[0] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[1] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[2] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[3] = _ASSIGN(true);
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            AXI4_DRIVER_FROM(l2cache.axi_in[i], axi_in[i]);
            l2cache.axi_out[i].awready_out = _ASSIGN_I(axi_out[i].awready_out());
            l2cache.axi_out[i].wready_out = _ASSIGN_I(axi_out[i].wready_out());
            l2cache.axi_out[i].bvalid_out = _ASSIGN_I(axi_out[i].bvalid_out());
            l2cache.axi_out[i].bid_out = _ASSIGN_I(axi_out[i].bid_out());
            l2cache.axi_out[i].arready_out = _ASSIGN_I(axi_out[i].arready_out());
            l2cache.axi_out[i].rvalid_out = _ASSIGN_I(axi_out[i].rvalid_out());
            l2cache.axi_out[i].rdata_out = _ASSIGN_I(axi_out[i].rdata_out());
            l2cache.axi_out[i].rlast_out = _ASSIGN_I(axi_out[i].rlast_out());
            l2cache.axi_out[i].rid_out = _ASSIGN_I(axi_out[i].rid_out());
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
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            axi_out[i].awvalid_in = l2cache.axi_out[i].awvalid_in;
            axi_out[i].awaddr_in = l2cache.axi_out[i].awaddr_in;
            axi_out[i].awid_in = l2cache.axi_out[i].awid_in;
            axi_out[i].wvalid_in = l2cache.axi_out[i].wvalid_in;
            axi_out[i].wdata_in = l2cache.axi_out[i].wdata_in;
            axi_out[i].wlast_in = l2cache.axi_out[i].wlast_in;
            axi_out[i].bready_in = l2cache.axi_out[i].bready_in;
            axi_out[i].arvalid_in = l2cache.axi_out[i].arvalid_in;
            axi_out[i].araddr_in = l2cache.axi_out[i].araddr_in;
            axi_out[i].arid_in = l2cache.axi_out[i].arid_in;
            axi_out[i].rready_in = l2cache.axi_out[i].rready_in;
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

    _LAZY_COMB(branch_stall_comb, bool)
        branch_stall_comb = branch_mispredict_comb_func();
        return branch_stall_comb;
    }

    _LAZY_COMB(branch_flush_comb, bool)
        branch_flush_comb = branch_mispredict_comb_func();
        return branch_flush_comb;
    }

    _LAZY_COMB(stall_comb, bool)
        stall_comb = hazard_stall_comb_func() || branch_stall_comb_func();
        return stall_comb;
    }

    _LAZY_COMB(perf_comb, TribePerf)
        perf_comb.hazard_stall = hazard_stall_comb_func();
        perf_comb.branch_stall = branch_stall_comb_func();
        perf_comb.dcache_wait = dcache.busy_out();
        perf_comb.icache_wait = icache.busy_out();
        perf_comb.icache = icache.perf_out();
        perf_comb.dcache = dcache.perf_out();
        return perf_comb;
    }

    _LAZY_COMB(memory_wait_comb, bool)
        memory_wait_comb = dcache.busy_out() ||
            exe_mem.mem_split_busy_out() ||
#ifdef ENABLE_RV32IA
            exe_mem.atomic_busy_out() ||
#endif
            (dcache.mem_read_out() && l2cache.d_wait_out()) ||
            ((exe_mem.mem_write_out() || (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE)) && l2cache.d_wait_out()) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !wb_mem.load_ready_out());
        return memory_wait_comb;
    }

    _LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && icache.read_valid_out() && icache.read_addr_out() == (uint32_t)pc;
    }

    _LAZY_COMB(decode_fallthrough_comb, uint32_t)
        return decode_fallthrough_comb = pc + ((dec.instr_in()&3)==3?4:2);
    }

    _LAZY_COMB(decode_branch_valid_comb, bool)
        decode_branch_valid_comb = fetch_valid_comb_func() && dec.state_out().valid && dec.state_out().br_op != Br::BNONE && !stall_comb_func();
        return decode_branch_valid_comb;
    }

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

    _LAZY_COMB(branch_actual_next_comb, uint32_t)
        return branch_actual_next_comb = exe.branch_taken_out() ? exe.branch_target_out() : (uint32_t)fallthrough_reg[0];
    }

    _LAZY_COMB(branch_mispredict_comb, bool)
        branch_mispredict_comb = state_reg[0].valid && exe_state_comb_func().br_op != Br::BNONE &&
            branch_actual_next_comb_func() != (uint32_t)predicted_next_reg[0];
        return branch_mispredict_comb;
    }

    _LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        if (branch_mispredict_comb_func()) {
            fetch_addr_comb = branch_actual_next_comb_func();
        }
        return fetch_addr_comb;
    }

    _LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
#ifdef ENABLE_ZICSR
        if (state_reg[0].valid &&
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
    _LAZY_COMB(csr_state_comb, State)
        csr_state_comb = exe_state_comb_func();
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
        if (memory_wait_comb_func()) {
            csr_state_comb.valid = false;
        }
        return csr_state_comb;
    }
#endif

    _LAZY_COMB(icache_invalidate_comb, bool)
        return icache_invalidate_comb = state_reg[0].valid &&
            (state_reg[0].sys_op == Sys::FENCEI || state_reg[0].sys_op == Sys::SFENCE_VMA) &&
            !memory_wait_comb_func();
    }

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

        if (memory_wait_comb_func()) {
            pc._next = pc;
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
            if (fetch_valid_comb_func() && !stall_comb_func()) {
                pc._next = decode_fallthrough_comb_func();
            }
            if (decode_branch_valid_comb_func()) {
                pc._next = bp.predict_next_out();
            }
            if (branch_mispredict_comb_func()) {
                pc._next = branch_actual_next_comb_func();
            }

            valid._next = true;

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

        std::print("({:d}/{:d}){} st[h{} b{} dc{} ic{} is{} ds{} ih{}]: [{:s}]{:08x}  rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({:d})ops:{:02d}/{}/{}/{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({:d}){:08x} => mem({:d}/{:d}@{:08x}){:08x}/{:01x} ({:d})wop({:x}),r({:d}){:08x}@{:02d}",
            (bool)valid, (bool)stall_comb_func(), pc,
            (bool)hazard_stall_comb_func(), (bool)branch_stall_comb_func(),
            (bool)dcache.busy_out(), (bool)icache.busy_out(),
            (uint32_t)icache.perf_out().state, (uint32_t)dcache.perf_out().state,
            (bool)icache.perf_out().hit,
            instr.mnemonic(), (instr.raw&3)==3?instr.raw:(instr.raw|0xFFFF0000),
            (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
            (bool)state_reg[0].valid, (uint8_t)state_reg[0].alu_op, (uint8_t)state_reg[0].mem_op, (uint8_t)state_reg[0].br_op, (uint8_t)state_reg[0].wb_op,
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

    void _strobe()
    {
        pc.strobe();
        valid.strobe();
        state_reg.strobe();
        predicted_next_reg.strobe();
        fallthrough_reg.strobe();
        predicted_taken_reg.strobe();
        alu_result_reg.strobe();
        debug_alu_a_reg.strobe();
        debug_alu_b_reg.strobe();
        debug_branch_target_reg.strobe();
        debug_branch_taken_reg.strobe();
        output_write_active_reg.strobe();

        regs._strobe();
        dec._strobe();
        exe._strobe();
        exe_mem._strobe();
        wb._strobe();
        wb_mem._strobe();
#ifdef ENABLE_ZICSR
        csr._strobe();
#endif
#ifdef ENABLE_MMU_TLB
        immu._strobe();
        dmmu._strobe();
#endif
        icache._strobe();
        dcache._strobe();
        bp._strobe();
        l2cache._strobe();
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

#include "Ram.h"
#include "Axi4Ram.h"

#include <tuple>
#include <utility>

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
    uint32_t tohost_addr = 0;
    uint32_t tohost_value = 0;
    uint32_t reset_pc = 0;
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

    bool load_elf(FILE* fbin, std::array<uint32_t, MAX_RAM_SIZE/4>& ram, size_t& read_bytes, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t& entry)
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
        entry = ehdr.entry;

        for (uint16_t i = 0; i < ehdr.phnum; ++i) {
            Elf32ProgramHeader phdr = {};
            fseek(fbin, ehdr.phoff + i * sizeof(phdr), SEEK_SET);
            if (fread(&phdr, 1, sizeof(phdr), fbin) != sizeof(phdr)) {
                return false;
            }
            if (phdr.type != PT_LOAD || phdr.filesz == 0) {
                continue;
            }

            const uint32_t phys = phdr.paddr ? phdr.paddr : phdr.vaddr;
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
        tribe._work(reset);
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;
        tribe.reset_pc_in = reset_pc;
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
        tribe.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            error = false;
            return;
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        tribe._strobe();
#endif
        mem0._strobe();  // we use these modules in Verilator test
        mem1._strobe();
        mem2._strobe();
        iospace._strobe();
        uart._strobe();
        clint._strobe();
        accelerator._strobe();
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
    }

    bool run(std::string filename, size_t start_offset, std::string expected_log = "rv32i.log", int max_cycles = 2000000, uint32_t tohost = 0, uint32_t mem_base = 0, uint32_t ram_words = DEFAULT_RAM_SIZE, bool raw_program = false)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTribe...");
#else
        std::print("CppHDL TestTribe...");
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", "w");
        fclose(out);
        tohost_addr = tohost;
        tohost_value = 0;
        tohost_done = false;
        start_mem_addr = mem_base;
        reset_pc = mem_base;
        ram_size = ram_words;
        if (ram_size == 0 || ram_size > TRIBE_RAM_BYTES/4) {
            std::print("invalid --ram-size {}; supported range is 1..{} words\n", ram_size, TRIBE_RAM_BYTES/4);
            return false;
        }

        /////////////// read program to memory
        std::array<uint32_t, MAX_RAM_SIZE/4> ram = {};
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        size_t read_bytes = 0;
        uint32_t elf_entry = 0;
        if (!raw_program && load_elf(fbin, ram, read_bytes, start_mem_addr, ram_size * 4, elf_entry)) {
            reset_pc = elf_entry;
            std::print("Reading ELF program into memory (size: {})\n", read_bytes);
        }
        else {
            fseek(fbin, start_offset, SEEK_SET);
            read_bytes = fread(ram.data(), 1, 4 * ram_size, fbin);
            std::print("Reading raw program into memory (size: {}, offset: {})\n", read_bytes, start_offset);
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
        std::string expected_output;
        std::string captured_output;
        if (!tohost_addr) {
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
        int cycles = max_cycles;
        while (--cycles && !error && !tohost_done) {
            _strobe();
            ++sys_clock;
            perf_sample();
            _work(0);
            if (uart.uart_valid_out()) {
                FILE* uart_out = fopen("out.txt", "ab");
                if (uart_out) {
                    char ch = (char)uart.uart_data_out();
                    fputc(ch, uart_out);
                    fclose(uart_out);
                    if (!tohost_addr) {
                        captured_output.push_back(ch);
                        if (captured_output.size() >= expected_output.size()) {
                            error = captured_output != expected_output;
                            break;
                        }
                    }
                }
            }
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
            _strobe_neg();
            _work_neg(0);
        }

        if (tohost_addr) {
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
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
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
    use_executable_workdir_if_needed(argv[0]);

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        const auto source_root = tribe_source_root_dir();
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
                  "Amo_pkg",
                  "Trap_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
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
        && ((only != -1 && only != 0) || TestTribe(debug).run(program, start_offset, expected_log, max_cycles, tohost, start_mem_addr, ram_size, raw_program))
    );
}

#endif  // !NO_MAINFILE

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
