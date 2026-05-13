#pragma once

#include "Config.h"

using namespace cpphdl;

class WritebackMem: public Module
{
public:
    _PORT(State)    state_in;
    _PORT(uint32_t) alu_result_in;
    _PORT(bool)     split_load_in;
    _PORT(uint32_t) split_load_low_addr_in;
    _PORT(uint32_t) split_load_high_addr_in;

    _PORT(bool)     dcache_read_valid_in;
    _PORT(uint32_t) dcache_read_addr_in;
    _PORT(uint32_t) dcache_read_data_in;

    _PORT(bool)     dcache_write_valid_in;
    _PORT(uint32_t) dcache_write_addr_in;
    _PORT(uint32_t) dcache_write_data_in;
    _PORT(uint8_t)  dcache_write_mask_in;

    _PORT(bool)     hold_in;

    _PORT(bool)     load_ready_out    = _ASSIGN_COMB(load_ready_comb_func());
    _PORT(uint32_t) load_raw_out      = _ASSIGN_COMB(load_raw_comb_func());
    _PORT(uint32_t) load_result_out   = _ASSIGN_COMB(load_result_comb_func());
    _PORT(uint32_t) wb_mem_data_out   = _ASSIGN_COMB(wb_mem_data_comb_func());
    _PORT(uint32_t) wb_mem_data_hi_out = _ASSIGN_COMB(wb_mem_data_hi_comb_func());

private:
    reg<u32> load_data_reg;
    reg<u32> load_addr_reg;
    reg<u1>  load_data_valid_reg;
    reg<u32> split_load_low_reg;
    reg<u32> split_load_high_reg;
    reg<u1>  split_load_low_valid_reg;
    reg<u1>  split_load_high_valid_reg;
    reg<array<u32,2>> store_forward_addr_reg;
    reg<array<u32,2>> store_forward_data_reg;
    reg<array<u8,2>>  store_forward_mask_reg;
    reg<array<u1,2>>  store_forward_valid_reg;

    _LAZY_COMB(split_load_current_low_valid_comb, bool)
        split_load_current_low_valid_comb = dcache_read_valid_in() &&
            dcache_read_addr_in() == split_load_low_addr_in();
        return split_load_current_low_valid_comb;
    }

    _LAZY_COMB(split_load_current_high_valid_comb, bool)
        split_load_current_high_valid_comb = dcache_read_valid_in() &&
            dcache_read_addr_in() == split_load_high_addr_in();
        return split_load_current_high_valid_comb;
    }

    _LAZY_COMB(split_load_low_ready_comb, bool)
        split_load_low_ready_comb = split_load_low_valid_reg || split_load_current_low_valid_comb_func();
        return split_load_low_ready_comb;
    }

    _LAZY_COMB(split_load_high_ready_comb, bool)
        split_load_high_ready_comb = split_load_high_valid_reg || split_load_current_high_valid_comb_func();
        return split_load_high_ready_comb;
    }

    _LAZY_COMB(split_load_low_data_comb, uint32_t)
        split_load_low_data_comb = split_load_low_valid_reg ? (uint32_t)split_load_low_reg :
            (split_load_current_low_valid_comb_func() ? dcache_read_data_in() : (uint32_t)0);
        return split_load_low_data_comb;
    }

    _LAZY_COMB(split_load_high_data_comb, uint32_t)
        split_load_high_data_comb = split_load_high_valid_reg ? (uint32_t)split_load_high_reg :
            (split_load_current_high_valid_comb_func() ? dcache_read_data_in() : (uint32_t)0);
        return split_load_high_data_comb;
    }

    _LAZY_COMB(load_ready_comb, bool)
        if (split_load_in()) {
            load_ready_comb = split_load_low_ready_comb_func() && split_load_high_ready_comb_func();
        }
        else {
            load_ready_comb = state_in().valid && state_in().wb_op == Wb::MEM &&
                (load_data_valid_reg ||
                 (dcache_read_valid_in() && dcache_read_addr_in() == alu_result_in()));
        }
        return load_ready_comb;
    }

    _LAZY_COMB(load_raw_comb, uint32_t)
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
        bool allow_store_forward;

        if (split_load_in()) {
            shift = (alu_result_in() & 3u) * 8u;
            raw = (split_load_low_data_comb_func() >> shift) |
                (split_load_high_data_comb_func() << (32u - shift));
        }
        else {
            raw = load_data_valid_reg ? (uint32_t)load_data_reg : dcache_read_data_in();
        }

        result = raw;
        load_addr = load_data_valid_reg ? (uint32_t)load_addr_reg : dcache_read_addr_in();
        allow_store_forward = state_in().amo_op == Amo::AMONONE;

        if (allow_store_forward && store_forward_valid_reg[1]) {
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

        if (allow_store_forward && store_forward_valid_reg[0]) {
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

        load_raw_comb = result;
        return load_raw_comb;
    }

    _LAZY_COMB(wb_mem_data_comb, uint32_t)
        if (split_load_in()) {
            wb_mem_data_comb = split_load_low_data_comb_func();
        }
        else {
            wb_mem_data_comb = load_data_valid_reg ? (uint32_t)load_data_reg :
                ((state_in().valid && state_in().wb_op == Wb::MEM && dcache_read_valid_in()) ?
                    dcache_read_data_in() : (uint32_t)0);
        }
        return wb_mem_data_comb;
    }

    _LAZY_COMB(wb_mem_data_hi_comb, uint32_t)
        wb_mem_data_hi_comb = split_load_in() ? split_load_high_data_comb_func() : (uint32_t)0;
        return wb_mem_data_hi_comb;
    }

    _LAZY_COMB(load_result_comb, uint32_t)
        uint32_t raw;
        raw = load_raw_comb_func();
        load_result_comb = 0;
        switch (state_in().funct3) {
            case 0b000: load_result_comb = uint32_t(int32_t(int8_t(raw)));   break;
            case 0b001: load_result_comb = uint32_t(int32_t(int16_t(raw)));  break;
            case 0b010: load_result_comb = raw;                              break;
            case 0b100: load_result_comb = uint8_t(raw);                     break;
            case 0b101: load_result_comb = uint16_t(raw);                    break;
        }
        return load_result_comb;
    }

public:
    void _work(bool reset)
    {
        if (dcache_write_valid_in() && dcache_write_mask_in()) {
            bool same_head = store_forward_valid_reg[0] &&
                (uint32_t)store_forward_addr_reg[0] == dcache_write_addr_in() &&
                (uint32_t)store_forward_data_reg[0] == dcache_write_data_in() &&
                (uint8_t)store_forward_mask_reg[0] == dcache_write_mask_in();
            if (!same_head) {
                store_forward_addr_reg._next[1] = store_forward_addr_reg[0];
                store_forward_data_reg._next[1] = store_forward_data_reg[0];
                store_forward_mask_reg._next[1] = store_forward_mask_reg[0];
                store_forward_valid_reg._next[1] = store_forward_valid_reg[0];
            }
            store_forward_addr_reg._next[0] = dcache_write_addr_in();
            store_forward_data_reg._next[0] = dcache_write_data_in();
            store_forward_mask_reg._next[0] = dcache_write_mask_in();
            store_forward_valid_reg._next[0] = true;
        }

        if (hold_in()) {
            if (split_load_in()) {
                if (split_load_current_low_valid_comb_func()) {
                    split_load_low_reg._next = dcache_read_data_in();
                    split_load_low_valid_reg._next = true;
                }
                if (split_load_current_high_valid_comb_func()) {
                    split_load_high_reg._next = dcache_read_data_in();
                    split_load_high_valid_reg._next = true;
                }
            }
            else if (state_in().valid && state_in().wb_op == Wb::MEM &&
                     dcache_read_valid_in()) {
                load_data_reg._next = dcache_read_data_in();
                load_addr_reg._next = dcache_read_addr_in();
                load_data_valid_reg._next = true;
            }
        }
        else {
            load_data_valid_reg._next = false;
            split_load_low_valid_reg._next = false;
            split_load_high_valid_reg._next = false;
        }

        if (reset) {
            load_data_reg.clr();
            load_addr_reg.clr();
            load_data_valid_reg.clr();
            split_load_low_reg.clr();
            split_load_high_reg.clr();
            split_load_low_valid_reg.clr();
            split_load_high_valid_reg.clr();
            store_forward_addr_reg.clr();
            store_forward_data_reg.clr();
            store_forward_mask_reg.clr();
            store_forward_valid_reg.clr();
        }
    }

    void _strobe()
    {
        load_data_reg.strobe();
        load_addr_reg.strobe();
        load_data_valid_reg.strobe();
        split_load_low_reg.strobe();
        split_load_high_reg.strobe();
        split_load_low_valid_reg.strobe();
        split_load_high_valid_reg.strobe();
        store_forward_addr_reg.strobe();
        store_forward_data_reg.strobe();
        store_forward_mask_reg.strobe();
        store_forward_valid_reg.strobe();
    }

    void _assign()
    {
    }
};
