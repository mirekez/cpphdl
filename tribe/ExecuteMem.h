#pragma once

#include "Config.h"

using namespace cpphdl;

class ExecuteMem: public Module
{
public:
    // Execute-stage state after forwarding and trap-return adjustments.
    _PORT(State)    state_in;
    // ALU-computed effective address for load/store instructions.
    _PORT(uint32_t) alu_result_in;
#ifdef ENABLE_RV32IA
    _PORT(bool)     dcache_read_valid_in;
    _PORT(uint32_t) dcache_read_addr_in;
    _PORT(uint32_t) dcache_read_data_in;
#endif
    // Holds the current memory request stable while dcache/L2 cannot accept it.
    _PORT(bool)     mem_stall_in;
    // Preserves issued-request metadata while the pipeline waits for writeback.
    _PORT(bool)     hold_in;

    // Registered request driven to dcache in the memory stage.
    _PORT(bool)     mem_write_out      = _BIND_VAR(mem_write_reg);
    _PORT(uint32_t) mem_write_addr_out = _BIND_VAR(mem_addr_reg);
    _PORT(uint32_t) mem_write_data_out = _BIND_VAR(mem_data_reg);
    _PORT(uint8_t)  mem_write_mask_out = _BIND_VAR(mem_mask_reg);
    _PORT(bool)     mem_read_out       = _BIND_VAR(mem_read_reg);
    _PORT(uint32_t) mem_read_addr_out  = _BIND_VAR(mem_addr_reg);

    // Split transaction status used by hazard control and writeback assembly.
    _PORT(bool)     mem_split_out      = _BIND_VAR(mem_split_comb_func());
    _PORT(bool)     mem_split_busy_out = _BIND_VAR(mem_split_pending_reg);
    _PORT(bool)     split_load_out      = _BIND_VAR(split_load_reg);
    _PORT(uint32_t) split_load_low_out  = _BIND_VAR(split_load_low_addr_reg);
    _PORT(uint32_t) split_load_high_out = _BIND_VAR(split_load_high_addr_reg);
#ifdef ENABLE_RV32IA
    _PORT(bool)     atomic_busy_out     = _BIND_VAR(atomic_busy_comb_func());
    _PORT(uint32_t) atomic_sc_result_out = _BIND_VAR(atomic_sc_result_comb_func());
#endif

private:
    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8>  mem_mask_reg;
    reg<u1>  mem_write_reg;
    reg<u1>  mem_read_reg;
    reg<u1>  mem_split_pending_reg;
    reg<u32> mem_split_addr_reg;
    reg<u32> mem_split_data_reg;
    reg<u<2>> mem_split_offset_reg;
    reg<u<3>> mem_split_size_reg;
    reg<u1>  mem_split_write_reg;
    reg<u1>  mem_split_read_reg;
    reg<u1>  split_load_reg;
    reg<u32> split_load_low_addr_reg;
    reg<u32> split_load_high_addr_reg;
#ifdef ENABLE_RV32IA
    reg<u1>  reservation_valid_reg;
    reg<u32> reservation_addr_reg;
    reg<u1>  atomic_pending_reg;
    reg<u32> atomic_addr_reg;
    reg<u32> atomic_operand_reg;
    reg<u<4>> atomic_op_reg;
#endif

    // Decode funct3 into byte count for integer loads and stores.
    _LAZY_COMB(mem_size_comb, uint32_t)
        if (state_in().amo_op != Amo::AMONONE) {
            mem_size_comb = 4;
        }
        else {
            mem_size_comb = 0;
            switch (state_in().funct3) {
                case 0b000: mem_size_comb = 1; break;
                case 0b001: mem_size_comb = 2; break;
                case 0b010: mem_size_comb = 4; break;
                case 0b100: mem_size_comb = 1; break;
                case 0b101: mem_size_comb = 2; break;
                default: break;
            }
        }
        return mem_size_comb;
    }

    // Detect memory accesses that cross a 32-byte L1 cache line.
    _LAZY_COMB(mem_split_comb, bool)
        uint32_t addr;
        uint32_t size;
        addr = alu_result_in();
        size = mem_size_comb_func();
        mem_split_comb = state_in().valid &&
            (state_in().mem_op == Mem::LOAD || state_in().mem_op == Mem::STORE) &&
            state_in().amo_op == Amo::AMONONE &&
            size != 0 &&
            ((addr & 0x1f) + size > 32);
        return mem_split_comb;
    }

#ifdef ENABLE_RV32IA
    _LAZY_COMB(atomic_read_ready_comb, bool)
        atomic_read_ready_comb = dcache_read_valid_in() &&
            dcache_read_addr_in() == (uint32_t)atomic_addr_reg;
        return atomic_read_ready_comb;
    }

    _LAZY_COMB(atomic_write_data_comb, uint32_t)
        uint32_t old_value;
        uint32_t operand;
        old_value = dcache_read_data_in();
        operand = atomic_operand_reg;
        atomic_write_data_comb = old_value;
        switch ((uint8_t)atomic_op_reg) {
            case Amo::AMOSWAP_W: atomic_write_data_comb = operand; break;
            case Amo::AMOADD_W:  atomic_write_data_comb = old_value + operand; break;
            case Amo::AMOXOR_W:  atomic_write_data_comb = old_value ^ operand; break;
            case Amo::AMOAND_W:  atomic_write_data_comb = old_value & operand; break;
            case Amo::AMOOR_W:   atomic_write_data_comb = old_value | operand; break;
            case Amo::AMOMIN_W:  atomic_write_data_comb = int32_t(old_value) < int32_t(operand) ? old_value : operand; break;
            case Amo::AMOMAX_W:  atomic_write_data_comb = int32_t(old_value) > int32_t(operand) ? old_value : operand; break;
            case Amo::AMOMINU_W: atomic_write_data_comb = old_value < operand ? old_value : operand; break;
            case Amo::AMOMAXU_W: atomic_write_data_comb = old_value > operand ? old_value : operand; break;
            default: break;
        }
        return atomic_write_data_comb;
    }

    _LAZY_COMB(atomic_sc_success_comb, bool)
        atomic_sc_success_comb = reservation_valid_reg &&
            ((uint32_t)reservation_addr_reg == (alu_result_in() & ~3u));
        return atomic_sc_success_comb;
    }

    _LAZY_COMB(atomic_sc_result_comb, uint32_t)
        return atomic_sc_result_comb = atomic_sc_success_comb_func() ? 0u : 1u;
    }

    _LAZY_COMB(atomic_busy_comb, bool)
        return atomic_busy_comb = atomic_pending_reg;
    }
#endif

    // Mask for the first beat of a split access; stores write the low bytes.
    _LAZY_COMB(first_split_mask_comb, uint8_t)
        uint32_t size;
        uint32_t offset;
        uint32_t low_size;
        size = mem_size_comb_func();
        offset = alu_result_in() & 3u;
        low_size = 4u - offset;
        if (state_in().mem_op == Mem::STORE) {
            first_split_mask_comb = (uint8_t)((1u << low_size) - 1u);
        }
        else {
            first_split_mask_comb = (uint8_t)((((1u << size) - 1u) << offset) & 0xfu);
        }
        return first_split_mask_comb;
    }

    // Mask for the delayed second beat of a split access.
    _LAZY_COMB(second_split_mask_comb, uint8_t)
        uint32_t overflow;
        overflow = (uint32_t)mem_split_offset_reg + (uint32_t)mem_split_size_reg - 4u;
        second_split_mask_comb = (uint8_t)((1u << overflow) - 1u);
        return second_split_mask_comb;
    }

    // Low aligned word address used later to match the first split-load response.
    _LAZY_COMB(split_load_low_addr_comb, uint32_t)
        return split_load_low_addr_comb = alu_result_in() & ~3u;
    }

    // High aligned word address used later to match the second split-load response.
    _LAZY_COMB(split_load_high_addr_comb, uint32_t)
        return split_load_high_addr_comb = split_load_low_addr_comb_func() + 4;
    }

    void do_memory()
    {
        mem_write_reg._next = 0;
        mem_read_reg._next = 0;
        mem_mask_reg._next = 0;
        if (mem_stall_in()) {
            mem_addr_reg._next = mem_addr_reg;
            mem_data_reg._next = mem_data_reg;
            mem_write_reg._next = mem_write_reg;
            mem_read_reg._next = mem_read_reg;
            mem_mask_reg._next = mem_mask_reg;
            mem_split_pending_reg._next = mem_split_pending_reg;
            mem_split_addr_reg._next = mem_split_addr_reg;
            mem_split_data_reg._next = mem_split_data_reg;
            mem_split_offset_reg._next = mem_split_offset_reg;
            mem_split_size_reg._next = mem_split_size_reg;
            mem_split_write_reg._next = mem_split_write_reg;
            mem_split_read_reg._next = mem_split_read_reg;
            split_load_reg._next = split_load_reg;
            split_load_low_addr_reg._next = split_load_low_addr_reg;
            split_load_high_addr_reg._next = split_load_high_addr_reg;
#ifdef ENABLE_RV32IA
            reservation_valid_reg._next = reservation_valid_reg;
            reservation_addr_reg._next = reservation_addr_reg;
            atomic_pending_reg._next = atomic_pending_reg;
            atomic_addr_reg._next = atomic_addr_reg;
            atomic_operand_reg._next = atomic_operand_reg;
            atomic_op_reg._next = atomic_op_reg;
#endif
            return;
        }

#ifdef ENABLE_RV32IA
        if (atomic_pending_reg) {
            if (atomic_read_ready_comb_func()) {
                if ((uint8_t)atomic_op_reg == Amo::LR_W) {
                    reservation_valid_reg._next = true;
                    reservation_addr_reg._next = atomic_addr_reg;
                }
                else {
                    mem_addr_reg._next = atomic_addr_reg;
                    mem_data_reg._next = atomic_write_data_comb_func();
                    mem_write_reg._next = true;
                    mem_mask_reg._next = 0xf;
                    reservation_valid_reg._next = false;
                }
                atomic_pending_reg._next = false;
            }
            else {
                mem_addr_reg._next = atomic_addr_reg;
                mem_read_reg._next = true;
            }
            return;
        }
#endif

        if (mem_split_pending_reg) {
            uint32_t overflow = (uint32_t)mem_split_offset_reg + (uint32_t)mem_split_size_reg - 4u;
            mem_addr_reg._next = ((uint32_t)mem_split_addr_reg & ~3u) + 4;
            mem_data_reg._next = (uint32_t)mem_split_data_reg >> (((uint32_t)mem_split_size_reg - overflow) * 8u);
            mem_write_reg._next = mem_split_write_reg;
            mem_read_reg._next = mem_split_read_reg;
            mem_mask_reg._next = second_split_mask_comb_func();
            mem_split_pending_reg._next = false;
            return;
        }

        if (hold_in()) {
            mem_addr_reg._next = mem_addr_reg;
            mem_data_reg._next = mem_data_reg;
            mem_write_reg._next = mem_write_reg;
            mem_read_reg._next = mem_read_reg;
            mem_mask_reg._next = mem_mask_reg;
            split_load_reg._next = split_load_reg;
            split_load_low_addr_reg._next = split_load_low_addr_reg;
            split_load_high_addr_reg._next = split_load_high_addr_reg;
            return;
        }

        mem_addr_reg._next = state_in().amo_op != Amo::AMONONE ? (alu_result_in() & ~3u) : alu_result_in();
        mem_data_reg._next = state_in().rs2_val;
        split_load_reg._next = state_in().valid && state_in().mem_op == Mem::LOAD && mem_split_comb_func();
        split_load_low_addr_reg._next = split_load_low_addr_comb_func();
        split_load_high_addr_reg._next = split_load_high_addr_comb_func();

#ifdef ENABLE_RV32IA
        if (state_in().valid && state_in().amo_op != Amo::AMONONE) {
            if (state_in().amo_op == Amo::SC_W) {
                if (atomic_sc_success_comb_func()) {
                    mem_write_reg._next = true;
                    mem_mask_reg._next = 0xf;
                }
                reservation_valid_reg._next = false;
            }
            else {
                mem_read_reg._next = true;
                atomic_pending_reg._next = true;
                atomic_addr_reg._next = alu_result_in() & ~3u;
                atomic_operand_reg._next = state_in().rs2_val;
                atomic_op_reg._next = state_in().amo_op;
            }
            return;
        }
#endif

        if (mem_split_comb_func()) {
            uint32_t offset = alu_result_in() & 3u;
            mem_addr_reg._next = state_in().mem_op == Mem::STORE ?
                alu_result_in() : (alu_result_in() & ~3u);
            mem_data_reg._next = state_in().mem_op == Mem::STORE ?
                state_in().rs2_val : (state_in().rs2_val << (offset * 8u));
            mem_write_reg._next = state_in().mem_op == Mem::STORE;
            mem_read_reg._next = state_in().mem_op == Mem::LOAD;
            mem_mask_reg._next = state_in().mem_op == Mem::STORE ? first_split_mask_comb_func() : (uint8_t)0;
            mem_split_pending_reg._next = true;
            mem_split_addr_reg._next = alu_result_in();
            mem_split_data_reg._next = state_in().rs2_val;
            mem_split_offset_reg._next = alu_result_in() & 3u;
            mem_split_size_reg._next = mem_size_comb_func();
            mem_split_write_reg._next = state_in().mem_op == Mem::STORE;
            mem_split_read_reg._next = state_in().mem_op == Mem::LOAD;
            return;
        }

        if (state_in().mem_op == Mem::STORE && state_in().valid) {
            switch (state_in().funct3)
            {
                case 0b000: mem_write_reg._next = state_in().valid; mem_mask_reg._next = 0x1; break;
                case 0b001: mem_write_reg._next = state_in().valid; mem_mask_reg._next = 0x3; break;
                case 0b010: mem_write_reg._next = state_in().valid; mem_mask_reg._next = 0xF; break;
            }
        }

        if (state_in().mem_op == Mem::LOAD && state_in().valid)
        {
            switch (state_in().funct3)
            {
                case 0b000: mem_read_reg._next = 1; break;
                case 0b001: mem_read_reg._next = 1; break;
                case 0b010: mem_read_reg._next = 1; break;
                case 0b100: mem_read_reg._next = 1; break;
                case 0b101: mem_read_reg._next = 1; break;
                default: break;
            }
        }
    }

public:
    void _work(bool reset)
    {
        do_memory();
        if (reset) {
            mem_write_reg.clr();
            mem_read_reg.clr();
            mem_split_pending_reg.clr();
            split_load_reg.clr();
#ifdef ENABLE_RV32IA
            reservation_valid_reg.clr();
            atomic_pending_reg.clr();
#endif
        }
    }

    void _strobe()
    {
        mem_addr_reg.strobe();
        mem_data_reg.strobe();
        mem_mask_reg.strobe();
        mem_write_reg.strobe();
        mem_read_reg.strobe();
        mem_split_pending_reg.strobe();
        mem_split_addr_reg.strobe();
        mem_split_data_reg.strobe();
        mem_split_offset_reg.strobe();
        mem_split_size_reg.strobe();
        mem_split_write_reg.strobe();
        mem_split_read_reg.strobe();
        split_load_reg.strobe();
        split_load_low_addr_reg.strobe();
        split_load_high_addr_reg.strobe();
#ifdef ENABLE_RV32IA
        reservation_valid_reg.strobe();
        reservation_addr_reg.strobe();
        atomic_pending_reg.strobe();
        atomic_addr_reg.strobe();
        atomic_operand_reg.strobe();
        atomic_op_reg.strobe();
#endif
    }

    void _assign()
    {
    }
};
