#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "File.h"

#define STAGES_NUM 3

#include "Decode.h"
#include "Execute.h"
#include "Writeback.h"
#include "CSR.h"
#include "BranchPredictor.h"
#include "cache/L1Cache.h"
#include "cache/L2Cache.h"

#define DEFAULT_RAM_SIZE 32768
#define MAX_RAM_SIZE 131072
#define CACHE_ADDR_BITS 19
#define L2_AXI_WIDTH 256
#define L2_AXI_BYTES (L2_AXI_WIDTH / 8)
#define L2_MEM_PORTS 4
#define BRANCH_PREDICTOR_ENTRIES 16
#define BRANCH_PREDICTOR_COUNTER_BITS 2

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
    Writeback       wb;
    CSR             csr;
    File<32,32>     regs;
    L1Cache<1024,32,2,0,32> icache;
    L1Cache<1024,32,2,1,32> dcache;
    L2Cache<8192,L2_AXI_WIDTH,32,4,32,CACHE_ADDR_BITS,L2_MEM_PORTS> l2cache;
    BranchPredictor<BRANCH_PREDICTOR_ENTRIES, BRANCH_PREDICTOR_COUNTER_BITS> bp;

public:

    __PORT(bool)      dmem_write_out;
    __PORT(uint32_t)  dmem_write_data_out;
    __PORT(uint8_t)   dmem_write_mask_out;
    __PORT(bool)      dmem_read_out;
    __PORT(uint32_t)  dmem_addr_out;
    __PORT(uint32_t)  imem_read_addr_out;
    __PORT(uint32_t)  reset_pc_in;
    __PORT(uint32_t)  memory_base_in;
    __PORT(uint32_t)  memory_size_in;

    __PORT(bool) axi_awvalid_out[L2_MEM_PORTS];
    __PORT(u<CACHE_ADDR_BITS>) axi_awaddr_out[L2_MEM_PORTS];
    __PORT(u<4>) axi_awid_out[L2_MEM_PORTS];
    __PORT(bool) axi_awready_in[L2_MEM_PORTS];
    __PORT(bool) axi_wvalid_out[L2_MEM_PORTS];
    __PORT(logic<L2_AXI_WIDTH>) axi_wdata_out[L2_MEM_PORTS];
    __PORT(bool) axi_wlast_out[L2_MEM_PORTS];
    __PORT(bool) axi_wready_in[L2_MEM_PORTS];
    __PORT(bool) axi_bready_out[L2_MEM_PORTS];
    __PORT(bool) axi_bvalid_in[L2_MEM_PORTS];
    __PORT(u<4>) axi_bid_in[L2_MEM_PORTS];
    __PORT(bool) axi_arvalid_out[L2_MEM_PORTS];
    __PORT(u<CACHE_ADDR_BITS>) axi_araddr_out[L2_MEM_PORTS];
    __PORT(u<4>) axi_arid_out[L2_MEM_PORTS];
    __PORT(bool) axi_arready_in[L2_MEM_PORTS];
    __PORT(bool) axi_rready_out[L2_MEM_PORTS];
    __PORT(bool) axi_rvalid_in[L2_MEM_PORTS];
    __PORT(logic<L2_AXI_WIDTH>) axi_rdata_in[L2_MEM_PORTS];
    __PORT(bool) axi_rlast_in[L2_MEM_PORTS];
    __PORT(u<4>) axi_rid_in[L2_MEM_PORTS];

    __PORT(TribePerf) perf_out = __VAR(perf_comb_func());
    bool              debugen_in;

    void _assign()
    {
        size_t i;
//        dec.state_in       = __VAR( state_reg[0] );  // execute stage input is same
        dec.pc_in          = __VAR( pc );
	        dec.instr_valid_in = __EXPR(fetch_valid_comb_func());
        dec.instr_in       = icache.read_data_out;
        dec.regs_data0_in  = __EXPR( dec.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        dec.regs_data1_in  = __EXPR( dec.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        dec._assign();  // outputs are ready

        exe.state_in       = __VAR( exe_state_comb_func() );
        exe.mem_stall_in   = __EXPR( dcache.busy_out() || l2cache.d_wait_out() );
        exe._assign();  // outputs are ready

        csr.state_in       = __VAR( csr_state_comb_func() );
        csr.__inst_name = __inst_name + "/csr";
        csr._assign();

        wb.state_in       = __VAR( state_reg[1] );
        wb.mem_data_in    = __VAR(wb_mem_data_comb_func());
        wb.mem_data_hi_in = __VAR(wb_mem_data_hi_comb_func());
        wb.mem_addr_in    = __VAR(alu_result_reg);
        wb.mem_split_in   = __VAR(split_load_comb_func());
        wb.alu_result_in  = __VAR( alu_result_reg );
        wb._assign();  // outputs are ready

        regs.read_addr0_in = __EXPR( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = __EXPR( (uint8_t)dec.rs2_out() );
        regs.write_in = __EXPR(wb.regs_write_out() &&
            !memory_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || load_data_ready_comb_func()));
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = __EXPR( exe.mem_read_out() && !dcache.busy_out() );
        dcache.write_in = __EXPR( exe.mem_write_out() && !dcache.busy_out() );
        dcache.addr_in = __EXPR( exe.mem_read_out() ? (uint32_t)exe.mem_read_addr_out() : (uint32_t)exe.mem_write_addr_out() );
        dcache.write_data_in = exe.mem_write_data_out;
        dcache.write_mask_in = exe.mem_write_mask_out;
        dcache.mem_read_data_in = l2cache.d_read_data_out;
        dcache.mem_wait_in = l2cache.d_wait_out;
        dcache.stall_in = __EXPR(branch_stall_comb_func());
        dcache.flush_in = __EXPR(false);
        dcache.invalidate_in = __EXPR(false);
        dcache.debugen_in = debugen_in;
        dcache.__inst_name = __inst_name + "/dcache";
        dcache._assign();

        bp.lookup_valid_in = __EXPR(decode_branch_valid_comb_func());
        bp.lookup_pc_in = __EXPR((uint32_t)dec.state_out().pc);
        bp.lookup_target_in = __EXPR(decode_branch_target_comb_func());
        bp.lookup_fallthrough_in = __EXPR(decode_fallthrough_comb_func());
        bp.lookup_br_op_in = __EXPR((u<4>)dec.state_out().br_op);
        bp.update_valid_in = __EXPR(state_reg[0].valid && state_reg[0].br_op != Br::BNONE && !memory_wait_comb_func());
        bp.update_pc_in = __EXPR((uint32_t)state_reg[0].pc);
        bp.update_taken_in = __EXPR(exe.branch_taken_out());
        bp.update_target_in = __EXPR(exe.branch_target_out());
        bp.__inst_name = __inst_name + "/bp";
        bp._assign();

        icache.read_in = __EXPR( true );
        icache.addr_in = __EXPR( fetch_addr_comb_func() );
        icache.write_in = __EXPR( false );
        icache.write_data_in = __EXPR( (uint32_t)0 );
        icache.write_mask_in = __EXPR( (uint8_t)0 );
        icache.mem_read_data_in = l2cache.i_read_data_out;
        icache.mem_wait_in = l2cache.i_wait_out;
        icache.stall_in = __EXPR(memory_wait_comb_func() || stall_comb_func());
        icache.flush_in = __EXPR(branch_mispredict_comb_func() && !memory_wait_comb_func());
        icache.invalidate_in = __VAR(icache_invalidate_comb_func());
        icache.debugen_in = debugen_in;
        icache.__inst_name = __inst_name + "/icache";
        icache._assign();

        l2cache.i_read_in = icache.mem_read_out;
        l2cache.i_write_in = __EXPR(false);
        l2cache.i_addr_in = icache.mem_addr_out;
        l2cache.i_write_data_in = __EXPR((uint32_t)0);
        l2cache.i_write_mask_in = __EXPR((uint8_t)0);
        l2cache.d_read_in = dcache.mem_read_out;
        l2cache.d_write_in = dcache.mem_write_out;
        l2cache.d_addr_in = dcache.mem_addr_out;
        l2cache.d_write_data_in = dcache.mem_write_data_out;
        l2cache.d_write_mask_in = dcache.mem_write_mask_out;
        l2cache.memory_base_in = memory_base_in;
        l2cache.memory_size_in = memory_size_in;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            l2cache.axi_awready_in[i] = axi_awready_in[i];
            l2cache.axi_wready_in[i] = axi_wready_in[i];
            l2cache.axi_bvalid_in[i] = axi_bvalid_in[i];
            l2cache.axi_bid_in[i] = axi_bid_in[i];
            l2cache.axi_arready_in[i] = axi_arready_in[i];
            l2cache.axi_rvalid_in[i] = axi_rvalid_in[i];
            l2cache.axi_rdata_in[i] = axi_rdata_in[i];
            l2cache.axi_rlast_in[i] = axi_rlast_in[i];
            l2cache.axi_rid_in[i] = axi_rid_in[i];
        }
        l2cache.debugen_in = debugen_in;
        l2cache.__inst_name = __inst_name + "/l2cache";
        l2cache._assign();

        dmem_write_out      = dcache.mem_write_out;
        dmem_write_data_out = dcache.mem_write_data_out;
        dmem_write_mask_out = dcache.mem_write_mask_out;
        dmem_read_out       = dcache.mem_read_out;
        dmem_addr_out       = dcache.mem_addr_out;
        imem_read_addr_out  = icache.mem_addr_out;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            axi_awvalid_out[i] = l2cache.axi_awvalid_out[i];
            axi_awaddr_out[i] = l2cache.axi_awaddr_out[i];
            axi_awid_out[i] = l2cache.axi_awid_out[i];
            axi_wvalid_out[i] = l2cache.axi_wvalid_out[i];
            axi_wdata_out[i] = l2cache.axi_wdata_out[i];
            axi_wlast_out[i] = l2cache.axi_wlast_out[i];
            axi_bready_out[i] = l2cache.axi_bready_out[i];
            axi_arvalid_out[i] = l2cache.axi_arvalid_out[i];
            axi_araddr_out[i] = l2cache.axi_araddr_out[i];
            axi_arid_out[i] = l2cache.axi_arid_out[i];
            axi_rready_out[i] = l2cache.axi_rready_out[i];
        }
    }

private:

    reg<u32>        pc;
    reg<u1>         valid;

    reg<u32>        alu_result_reg;
    reg<u32>        load_data_reg;
    reg<u1>         load_data_valid_reg;
    reg<u32>        split_load_low_reg;
    reg<u32>        split_load_high_reg;
    reg<u1>         split_load_low_valid_reg;
    reg<u1>         split_load_high_valid_reg;
    reg<array<u32,2>> store_forward_addr_reg;
    reg<array<u32,2>> store_forward_data_reg;
    reg<array<u8,2>>  store_forward_mask_reg;
    reg<array<u1,2>>  store_forward_valid_reg;

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

    __LAZY_COMB(hazard_stall_comb, bool)
        const auto& dec_state_tmp = dec.state_out();

        hazard_stall_comb = false;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex hazard
            if (state_reg[0].rd == dec_state_tmp.rs1) {
                hazard_stall_comb = true;
            }
            if (state_reg[0].rd == dec_state_tmp.rs2) {
                hazard_stall_comb = true;
            }
        }
        if (exe.mem_split_out() || exe.mem_split_busy_out()) {
            hazard_stall_comb = true;
        }
        return hazard_stall_comb;
    }

    __LAZY_COMB(branch_stall_comb, bool)
        branch_stall_comb = branch_mispredict_comb_func();
        return branch_stall_comb;
    }

    __LAZY_COMB(branch_flush_comb, bool)
        branch_flush_comb = branch_mispredict_comb_func();
        return branch_flush_comb;
    }

    __LAZY_COMB(stall_comb, bool)
        stall_comb = hazard_stall_comb_func() || branch_stall_comb_func();
        return stall_comb;
    }

    __LAZY_COMB(perf_comb, TribePerf)
        perf_comb.hazard_stall = hazard_stall_comb_func();
        perf_comb.branch_stall = branch_stall_comb_func();
        perf_comb.dcache_wait = dcache.busy_out();
        perf_comb.icache_wait = icache.busy_out();
        perf_comb.icache = icache.perf_out();
        perf_comb.dcache = dcache.perf_out();
        return perf_comb;
    }

    __LAZY_COMB(memory_wait_comb, bool)
        memory_wait_comb = dcache.busy_out() ||
            exe.mem_split_busy_out() ||
            ((exe.mem_write_out() || (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE)) && l2cache.d_wait_out()) ||
            (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !load_data_ready_comb_func());
        return memory_wait_comb;
    }

    __LAZY_COMB(split_load_comb, bool)
        uint32_t size;
        size = state_mem_size_comb_func();
        split_load_comb = state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            size != 0 && (((uint32_t)alu_result_reg & 0x1f) + size > 32);
        return split_load_comb;
    }

    __LAZY_COMB(state_mem_size_comb, uint32_t)
        state_mem_size_comb = 0;
        switch (state_reg[1].funct3) {
            case 0b000: state_mem_size_comb = 1; break;
            case 0b001: state_mem_size_comb = 2; break;
            case 0b010: state_mem_size_comb = 4; break;
            case 0b100: state_mem_size_comb = 1; break;
            case 0b101: state_mem_size_comb = 2; break;
            default: break;
        }
        return state_mem_size_comb;
    }

    __LAZY_COMB(split_load_low_addr_comb, uint32_t)
        return split_load_low_addr_comb = (uint32_t)alu_result_reg & ~3u;
    }

    __LAZY_COMB(split_load_high_addr_comb, uint32_t)
        return split_load_high_addr_comb = split_load_low_addr_comb_func() + 4;
    }

    __LAZY_COMB(split_load_current_low_valid_comb, bool)
        split_load_current_low_valid_comb = dcache.read_valid_out() &&
            dcache.read_addr_out() == split_load_low_addr_comb_func();
        return split_load_current_low_valid_comb;
    }

    __LAZY_COMB(split_load_current_high_valid_comb, bool)
        split_load_current_high_valid_comb = dcache.read_valid_out() &&
            dcache.read_addr_out() == split_load_high_addr_comb_func();
        return split_load_current_high_valid_comb;
    }

    __LAZY_COMB(split_load_low_ready_comb, bool)
        split_load_low_ready_comb = split_load_low_valid_reg || split_load_current_low_valid_comb_func();
        return split_load_low_ready_comb;
    }

    __LAZY_COMB(split_load_high_ready_comb, bool)
        split_load_high_ready_comb = split_load_high_valid_reg || split_load_current_high_valid_comb_func();
        return split_load_high_ready_comb;
    }

    __LAZY_COMB(split_load_low_data_comb, uint32_t)
        split_load_low_data_comb = split_load_low_valid_reg ? (uint32_t)split_load_low_reg :
            (split_load_current_low_valid_comb_func() ? dcache.read_data_out() : (uint32_t)0);
        return split_load_low_data_comb;
    }

    __LAZY_COMB(split_load_high_data_comb, uint32_t)
        split_load_high_data_comb = split_load_high_valid_reg ? (uint32_t)split_load_high_reg :
            (split_load_current_high_valid_comb_func() ? dcache.read_data_out() : (uint32_t)0);
        return split_load_high_data_comb;
    }

    __LAZY_COMB(load_data_ready_comb, bool)
        if (split_load_comb_func()) {
            load_data_ready_comb = split_load_low_ready_comb_func() && split_load_high_ready_comb_func();
        }
        else {
            load_data_ready_comb = state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
                (load_data_valid_reg || (dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg));
        }
        return load_data_ready_comb;
    }

    __LAZY_COMB(load_data_raw_comb, uint32_t)
        uint32_t raw;
        uint32_t result;
        uint32_t load_addr;
        uint32_t byte_addr;
        uint32_t store_addr;
        uint32_t store_data;
        uint32_t store_byte;
        uint32_t diff;
        uint8_t store_mask;
        uint32_t shift;
        if (split_load_comb_func()) {
            shift = ((uint32_t)alu_result_reg & 3u) * 8u;
            raw = (split_load_low_data_comb_func() >> shift) |
                (split_load_high_data_comb_func() << (32u - shift));
        }
        else {
            raw = load_data_valid_reg ? (uint32_t)load_data_reg : dcache.read_data_out();
        }

        result = raw;
        load_addr = (uint32_t)alu_result_reg;

        if (store_forward_valid_reg[1]) {
            store_addr = store_forward_addr_reg[1];
            store_data = store_forward_data_reg[1];
            store_mask = store_forward_mask_reg[1];

            byte_addr = load_addr;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xffu) | store_byte;
            }
            byte_addr = load_addr + 1u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff00u) | (store_byte << 8u);
            }
            byte_addr = load_addr + 2u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff0000u) | (store_byte << 16u);
            }
            byte_addr = load_addr + 3u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff000000u) | (store_byte << 24u);
            }
        }

        if (store_forward_valid_reg[0]) {
            store_addr = store_forward_addr_reg[0];
            store_data = store_forward_data_reg[0];
            store_mask = store_forward_mask_reg[0];

            byte_addr = load_addr;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xffu) | store_byte;
            }
            byte_addr = load_addr + 1u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff00u) | (store_byte << 8u);
            }
            byte_addr = load_addr + 2u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff0000u) | (store_byte << 16u);
            }
            byte_addr = load_addr + 3u;
            diff = byte_addr - store_addr;
            if (byte_addr >= store_addr && diff < 4 && (store_mask & (1u << diff))) {
                store_byte = (store_data >> (diff * 8u)) & 0xffu;
                result = (result & ~0xff000000u) | (store_byte << 24u);
            }
        }

        load_data_raw_comb = result;
        return load_data_raw_comb;
    }

    __LAZY_COMB(wb_mem_data_comb, uint32_t)
        if (split_load_comb_func()) {
            wb_mem_data_comb = split_load_low_data_comb_func();
        }
        else {
            wb_mem_data_comb = load_data_valid_reg ? (uint32_t)load_data_reg :
                ((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
                  dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg) ?
                    dcache.read_data_out() : (uint32_t)0);
        }
        return wb_mem_data_comb;
    }

    __LAZY_COMB(wb_mem_data_hi_comb, uint32_t)
        wb_mem_data_hi_comb = split_load_comb_func() ? split_load_high_data_comb_func() : (uint32_t)0;
        return wb_mem_data_hi_comb;
    }

    __LAZY_COMB(load_result_comb, uint32_t)
        uint32_t raw;
        raw = load_data_raw_comb_func();
        load_result_comb = 0;
        switch (state_reg[1].funct3) {
            case 0b000: load_result_comb = uint32_t(int32_t(int8_t(raw)));   break;
            case 0b001: load_result_comb = uint32_t(int32_t(int16_t(raw)));  break;
            case 0b010: load_result_comb = raw;                              break;
            case 0b100: load_result_comb = uint8_t(raw);                     break;
            case 0b101: load_result_comb = uint16_t(raw);                    break;
        }
        return load_result_comb;
    }

    __LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && icache.read_valid_out() && icache.read_addr_out() == (uint32_t)pc;
    }

    __LAZY_COMB(decode_fallthrough_comb, uint32_t)
        return decode_fallthrough_comb = pc + ((dec.instr_in()&3)==3?4:2);
    }

    __LAZY_COMB(decode_branch_valid_comb, bool)
        decode_branch_valid_comb = fetch_valid_comb_func() && dec.state_out().valid && dec.state_out().br_op != Br::BNONE && !stall_comb_func();
        return decode_branch_valid_comb;
    }

    __LAZY_COMB(decode_branch_target_comb, uint32_t)
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

    __LAZY_COMB(branch_actual_next_comb, uint32_t)
        return branch_actual_next_comb = exe.branch_taken_out() ? exe.branch_target_out() : (uint32_t)fallthrough_reg[0];
    }

    __LAZY_COMB(branch_mispredict_comb, bool)
        branch_mispredict_comb = state_reg[0].valid && state_reg[0].br_op != Br::BNONE &&
            branch_actual_next_comb_func() != (uint32_t)predicted_next_reg[0];
        return branch_mispredict_comb;
    }

    __LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        if (fetch_valid_comb_func() && !stall_comb_func()) {
            fetch_addr_comb = decode_fallthrough_comb_func();
        }
        if (decode_branch_valid_comb_func()) {
            fetch_addr_comb = bp.predict_next_out();
        }
        if (branch_mispredict_comb_func()) {
            fetch_addr_comb = branch_actual_next_comb_func();
        }
        return fetch_addr_comb;
    }

    __LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
        if (state_reg[0].valid && state_reg[0].sys_op == Sys::ECALL) {
            exe_state_comb.rs1_val = csr.trap_vector_out();
            exe_state_comb.imm = 0;
        }
        if (state_reg[0].valid && state_reg[0].sys_op == Sys::MRET) {
            exe_state_comb.rs1_val = csr.epc_out();
            exe_state_comb.imm = 0;
        }
        if (state_reg[0].valid && state_reg[0].sys_op == Sys::FENCEI) {
            exe_state_comb.rs1_val = state_reg[0].pc + 4;
            exe_state_comb.imm = 0;
        }
        if (load_data_ready_comb_func() && state_reg[1].rd != 0) {
            if (state_reg[0].rs1 == state_reg[1].rd) {
                exe_state_comb.rs1_val = load_result_comb_func();
            }
            if (state_reg[0].rs2 == state_reg[1].rd) {
                exe_state_comb.rs2_val = load_result_comb_func();
            }
        }
        return exe_state_comb;
    }

    __LAZY_COMB(csr_state_comb, State)
        csr_state_comb = exe_state_comb_func();
        if (memory_wait_comb_func()) {
            csr_state_comb.valid = false;
        }
        return csr_state_comb;
    }

    __LAZY_COMB(icache_invalidate_comb, bool)
        return icache_invalidate_comb = state_reg[0].valid && state_reg[0].sys_op == Sys::FENCEI && !memory_wait_comb_func();
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

        if (load_data_ready_comb_func() && state_reg[1].rd != 0) {  // Mem/Wb mem
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = load_result_comb_func();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS1\n", (uint32_t)load_result_comb_func());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = load_result_comb_func();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS2\n", (uint32_t)load_result_comb_func());
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
                state_reg._next[0].rs1_val = state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() : exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", state_reg[0].csr_op != Csr::CNONE ? (uint32_t)csr.read_data_out() : (uint32_t)exe.alu_result_out());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg._next[0].rs2_val = state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() : exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", state_reg[0].csr_op != Csr::CNONE ? (uint32_t)csr.read_data_out() : (uint32_t)exe.alu_result_out());
                }
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

        if (dcache.mem_write_out() && dcache.mem_write_mask_out()) {
            bool same_head = store_forward_valid_reg[0] &&
                (uint32_t)store_forward_addr_reg[0] == dcache.mem_addr_out() &&
                (uint32_t)store_forward_data_reg[0] == dcache.mem_write_data_out() &&
                (uint8_t)store_forward_mask_reg[0] == dcache.mem_write_mask_out();
            if (!same_head) {
                store_forward_addr_reg._next[1] = store_forward_addr_reg[0];
                store_forward_data_reg._next[1] = store_forward_data_reg[0];
                store_forward_mask_reg._next[1] = store_forward_mask_reg[0];
                store_forward_valid_reg._next[1] = store_forward_valid_reg[0];
            }
            store_forward_addr_reg._next[0] = dcache.mem_addr_out();
            store_forward_data_reg._next[0] = dcache.mem_write_data_out();
            store_forward_mask_reg._next[0] = dcache.mem_write_mask_out();
            store_forward_valid_reg._next[0] = true;
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
            if (split_load_comb_func()) {
                if (split_load_current_low_valid_comb_func()) {
                    split_load_low_reg._next = dcache.read_data_out();
                    split_load_low_valid_reg._next = true;
                }
                if (split_load_current_high_valid_comb_func()) {
                    split_load_high_reg._next = dcache.read_data_out();
                    split_load_high_valid_reg._next = true;
                }
                if (load_data_ready_comb_func() && state_reg[1].rd != 0 && state_reg[0].valid) {
                    if (state_reg[0].rs1 == state_reg[1].rd) {
                        state_reg._next[0].rs1_val = load_result_comb_func();
                    }
                    if (state_reg[0].rs2 == state_reg[1].rd) {
                        state_reg._next[0].rs2_val = load_result_comb_func();
                    }
                }
            }
            else {
                if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
                    dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg) {
                    load_data_reg._next = dcache.read_data_out();
                    load_data_valid_reg._next = true;
                    if (state_reg[1].rd != 0 && state_reg[0].valid) {
                        if (state_reg[0].rs1 == state_reg[1].rd) {
                            state_reg._next[0].rs1_val = load_result_comb_func();
                        }
                        if (state_reg[0].rs2 == state_reg[1].rd) {
                            state_reg._next[0].rs2_val = load_result_comb_func();
                        }
                    }
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
                state_reg._next[0] = dec.state_out();
                state_reg._next[0].valid = dec.instr_valid_in() && !branch_stall_comb_func() && !branch_flush_comb_func();
                predicted_next_reg._next[0] = decode_branch_valid_comb_func() ? (uint32_t)bp.predict_next_out() : decode_fallthrough_comb_func();
                fallthrough_reg._next[0] = decode_fallthrough_comb_func();
                predicted_taken_reg._next[0] = decode_branch_valid_comb_func() && bp.predict_taken_out();
                forward();
            }
            state_reg._next[1] = state_reg[0];
            predicted_next_reg._next[1] = predicted_next_reg[0];
            fallthrough_reg._next[1] = fallthrough_reg[0];
            predicted_taken_reg._next[1] = predicted_taken_reg[0];
            alu_result_reg._next = state_reg[0].csr_op != Csr::CNONE ? csr.read_data_out() : exe.alu_result_out();
            load_data_valid_reg._next = false;
            split_load_low_valid_reg._next = false;
            split_load_high_valid_reg._next = false;
            debug_branch_target_reg._next = exe.branch_target_out();
            debug_branch_taken_reg._next = exe.branch_taken_out();
        }

        regs._work(reset);
        dec._work(reset);
        exe._work(reset);
        wb._work(reset);
        csr._work(reset);
        icache._work(reset);
        dcache._work(reset);
        l2cache._work(reset);
        bp._work(reset);

        if (reset) {
            state_reg._next[0].valid = 0;
            state_reg._next[1].valid = 0;
            pc._next = reset_pc_in();
            valid.clr();
            load_data_reg.clr();
            load_data_valid_reg.clr();
            split_load_low_reg.clr();
            split_load_high_reg.clr();
            split_load_low_valid_reg.clr();
            split_load_high_valid_reg.clr();
            store_forward_addr_reg.clr();
            store_forward_data_reg.clr();
            store_forward_mask_reg.clr();
            store_forward_valid_reg.clr();
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
            (bool)exe.mem_write_out(), (bool)exe.mem_read_out(), exe.mem_write_addr_out(), exe.mem_write_data_out(), exe.mem_write_mask_out(),
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
        load_data_reg.strobe();
        load_data_valid_reg.strobe();
        split_load_low_reg.strobe();
        split_load_high_reg.strobe();
        split_load_low_valid_reg.strobe();
        split_load_high_valid_reg.strobe();
        store_forward_addr_reg.strobe();
        store_forward_data_reg.strobe();
        store_forward_mask_reg.strobe();
        store_forward_valid_reg.strobe();
        debug_alu_a_reg.strobe();
        debug_alu_b_reg.strobe();
        debug_branch_target_reg.strobe();
        debug_branch_taken_reg.strobe();
        output_write_active_reg.strobe();

        regs._strobe();
        dec._strobe();
        exe._strobe();
        wb._strobe();
        csr._strobe();
        icache._strobe();
        dcache._strobe();
        bp._strobe();
        l2cache._strobe();
    }


};

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

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

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH == WORDS * 32);
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

#define PORT_VALUE(port) (port)
#define PERF_VALUE(port) verilator_tribe_perf((uint64_t)(port))
#else
#define PORT_VALUE(port) (port())
#define PERF_VALUE(port) (port())
#endif

class TestTribe : public Module
{
    static constexpr size_t AXI_RAM_LINES = MAX_RAM_SIZE * 4 / L2_AXI_BYTES;
    static constexpr size_t AXI_RAM_LINES_PER_PORT = AXI_RAM_LINES / L2_MEM_PORTS;
    Axi4Ram<CACHE_ADDR_BITS,4,L2_AXI_WIDTH,AXI_RAM_LINES_PER_PORT> mem[L2_MEM_PORTS];

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

    bool load_elf(FILE* fbin, std::array<uint32_t, MAX_RAM_SIZE>& ram, size_t& read_bytes, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t& entry)
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
        size_t i;
#ifndef VERILATOR
        tribe._assign();

        for (i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_awready_in[i] = mem[i].axi_awready_out;
            tribe.axi_wready_in[i] = mem[i].axi_wready_out;
            tribe.axi_bvalid_in[i] = mem[i].axi_bvalid_out;
            tribe.axi_bid_in[i] = mem[i].axi_bid_out;
            tribe.axi_arready_in[i] = mem[i].axi_arready_out;
            tribe.axi_rvalid_in[i] = mem[i].axi_rvalid_out;
            tribe.axi_rdata_in[i] = mem[i].axi_rdata_out;
            tribe.axi_rlast_in[i] = mem[i].axi_rlast_out;
            tribe.axi_rid_in[i] = mem[i].axi_rid_out;
        }
        tribe.debugen_in = debugen_in;
        tribe.reset_pc_in = __EXPR(reset_pc);
        tribe.memory_base_in = __EXPR(start_mem_addr);
        tribe.memory_size_in = __EXPR((uint32_t)(ram_size * 4));
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._assign();

        for (i = 0; i < L2_MEM_PORTS; ++i) {
            mem[i].axi_awvalid_in = tribe.axi_awvalid_out[i];
            mem[i].axi_awaddr_in = tribe.axi_awaddr_out[i];
            mem[i].axi_awid_in = tribe.axi_awid_out[i];
            mem[i].axi_wvalid_in = tribe.axi_wvalid_out[i];
            mem[i].axi_wdata_in = tribe.axi_wdata_out[i];
            mem[i].axi_wlast_in = tribe.axi_wlast_out[i];
            mem[i].axi_bready_in = tribe.axi_bready_out[i];
            mem[i].axi_arvalid_in = tribe.axi_arvalid_out[i];
            mem[i].axi_araddr_in = tribe.axi_araddr_out[i];
            mem[i].axi_arid_in = tribe.axi_arid_out[i];
            mem[i].axi_rready_in = tribe.axi_rready_out[i];
            mem[i].debugen_in = debugen_in;
            mem[i].__inst_name = __inst_name + "/mem" + std::to_string(i);
            mem[i]._assign();
        }
#else  // connecting Verilator to CppHDL
        tribe.reset_pc_in = reset_pc;
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = ram_size * 4;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            mem[i].axi_awvalid_in = __EXPR_I((bool)tribe.axi_awvalid_out[i]);
            mem[i].axi_awaddr_in = __EXPR_I((u<CACHE_ADDR_BITS>)(uint32_t)tribe.axi_awaddr_out[i]);
            mem[i].axi_awid_in = __EXPR_I((u<4>)(uint32_t)tribe.axi_awid_out[i]);
            mem[i].axi_wvalid_in = __EXPR_I((bool)tribe.axi_wvalid_out[i]);
            mem[i].axi_wdata_in = __EXPR_I(verilator_wide_to_logic(tribe.axi_wdata_out[i]));
            mem[i].axi_wlast_in = __EXPR_I((bool)tribe.axi_wlast_out[i]);
            mem[i].axi_bready_in = __EXPR_I((bool)tribe.axi_bready_out[i]);
            mem[i].axi_arvalid_in = __EXPR_I((bool)tribe.axi_arvalid_out[i]);
            mem[i].axi_araddr_in = __EXPR_I((u<CACHE_ADDR_BITS>)(uint32_t)tribe.axi_araddr_out[i]);
            mem[i].axi_arid_in = __EXPR_I((u<4>)(uint32_t)tribe.axi_arid_out[i]);
            mem[i].axi_rready_in = __EXPR_I((bool)tribe.axi_rready_out[i]);
            mem[i].debugen_in = debugen_in;
            mem[i].__inst_name = __inst_name + "/mem" + std::to_string(i);
            mem[i]._assign();
        }
#endif
    }

    void _work(bool reset)
    {
        size_t i;
#ifndef VERILATOR
        tribe._work(reset);
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;
        tribe.reset_pc_in = reset_pc;
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = ram_size * 4;

//        data_in           = (array<DTYPE,LENGTH>*) &tribe.data_out.m_storage;

        if (reset) {
            tribe.clk = 0;
            tribe.reset = reset;
            tribe.eval();
        }
        tribe.clk = 1;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_awready_in[i] = mem[i].axi_awready_out();
            tribe.axi_wready_in[i] = mem[i].axi_wready_out();
            tribe.axi_bvalid_in[i] = mem[i].axi_bvalid_out();
            tribe.axi_bid_in[i] = mem[i].axi_bid_out();
            tribe.axi_arready_in[i] = mem[i].axi_arready_out();
            tribe.axi_rvalid_in[i] = mem[i].axi_rvalid_out();
            verilator_logic_to_wide(tribe.axi_rdata_in[i], mem[i].axi_rdata_out());
            tribe.axi_rlast_in[i] = mem[i].axi_rlast_out();
            tribe.axi_rid_in[i] = mem[i].axi_rid_out();
        }
#endif
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            mem[i]._work(reset);
        }

        if (reset) {
            error = false;
            return;
        }
    }

    void _strobe()
    {
        size_t i;
#ifndef VERILATOR
        tribe._strobe();
#endif
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            mem[i]._strobe();  // we use this module in Verilator test
        }
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

    void mirror_dmem_write(uint32_t addr, uint32_t data, uint8_t mask)
    {
        for (uint32_t byte = 0; byte < 4; ++byte) {
            if (!(mask & (1u << byte))) {
                continue;
            }
            const uint32_t full_addr = addr + byte;
            if (full_addr < start_mem_addr || full_addr - start_mem_addr >= ram_size * 4) {
                continue;
            }
            const uint32_t mem_addr = full_addr - start_mem_addr;
            const uint32_t line_idx = mem_addr / L2_AXI_BYTES;
            const uint32_t port = line_idx / AXI_RAM_LINES_PER_PORT;
            const uint32_t local_line_idx = line_idx % AXI_RAM_LINES_PER_PORT;
            const uint32_t line_byte = mem_addr % L2_AXI_BYTES;
            logic<L2_AXI_WIDTH> line = mem[port].ram.buffer[local_line_idx];
            line.bits(line_byte * 8 + 7, line_byte * 8) = (data >> (byte * 8)) & 0xffu;
            mem[port].ram.buffer[local_line_idx] = line;
        }
    }

    bool run(std::string filename, size_t start_offset, std::string expected_log = "rv32i.log", int max_cycles = 2000000, uint32_t tohost = 0, uint32_t mem_base = 0, uint32_t ram_words = DEFAULT_RAM_SIZE)
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
        if (ram_size == 0 || ram_size > MAX_RAM_SIZE) {
            std::print("invalid --ram-size {}; supported range is 1..{} words\n", ram_size, MAX_RAM_SIZE);
            return false;
        }

        /////////////// read program to memory
        std::array<uint32_t, MAX_RAM_SIZE> ram = {};
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        size_t read_bytes = 0;
        uint32_t elf_entry = 0;
        if (load_elf(fbin, ram, read_bytes, start_mem_addr, ram_size * 4, elf_entry)) {
            reset_pc = elf_entry;
            std::print("Reading ELF program into memory (size: {})\n", read_bytes);
        }
        else {
            fseek(fbin, start_offset, SEEK_SET);
            read_bytes = fread(ram.data(), 1, 4 * ram_size, fbin);
            std::print("Reading raw program into memory (size: {}, offset: {})\n", read_bytes, start_offset);
        }

        const size_t active_lines = (ram_size * 4 + L2_AXI_BYTES - 1) / L2_AXI_BYTES;
        for (size_t line_idx = 0; line_idx < active_lines; ++line_idx) {
            logic<L2_AXI_WIDTH> line = 0;
            for (size_t word = 0; word < L2_AXI_BYTES / 4; ++word) {
                size_t addr = line_idx * (L2_AXI_BYTES / 4) + word;
                line.bits(word * 32 + 31, word * 32) = ram[addr];
                if (debugen_in) {
                    std::print("{:04x}: {:08x}\n", addr, ram[addr]);
                }
            }
            size_t port = line_idx / AXI_RAM_LINES_PER_PORT;
            size_t local_line_idx = line_idx % AXI_RAM_LINES_PER_PORT;
            mem[port].ram.buffer[local_line_idx] = line;
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
        int cycles = max_cycles;
        while (--cycles && !error && !tohost_done) {
            _strobe();
            ++sys_clock;
            perf_sample();
            _work(0);
            if (PORT_VALUE(tribe.dmem_write_out) && PORT_VALUE(tribe.dmem_write_mask_out)) {
                mirror_dmem_write(PORT_VALUE(tribe.dmem_addr_out), PORT_VALUE(tribe.dmem_write_data_out), PORT_VALUE(tribe.dmem_write_mask_out));
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
            if (!a) {
                std::print("can't open expected output '{}'\n", expected_log);
                error = true;
            }
            error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));
        }

        perf_print();
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    std::string program = "rv32i.bin";
    std::string expected_log = "rv32i.log";
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
            continue;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            expected_log = argv[++i];
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

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
                  "Rv32im_pkg",
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
                  "Decode",
                  "Execute",
                  "CSR",
                  "Writeback"}, {"../../../../include", "../../../../tribe", "../../../../tribe/common", "../../../../tribe/spec", "../../../../tribe/cache"});
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
        && ((only != -1 && only != 0) || TestTribe(debug).run(program, start_offset, expected_log, max_cycles, tohost, start_mem_addr, ram_size))
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
