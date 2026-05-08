#pragma once

#include "cpphdl.h"
#include "RAM1PORT.h"

using namespace cpphdl;

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
class L2Cache : public Module
{
    static_assert(CACHE_LINE_SIZE == 32, "L2Cache uses 32-byte cache lines");
    static_assert(PORT_BITWIDTH >= 32 && PORT_BITWIDTH % 32 == 0, "L2Cache port must be a whole number of 32-bit words");
    static_assert((CACHE_LINE_SIZE * 8) % PORT_BITWIDTH == 0, "L2Cache port must divide a cache line");
    static_assert(WAYS == 4, "L2Cache is intended to be 4-way set associative");
    static_assert(CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0, "L2Cache geometry must divide evenly");
    static_assert(MEM_PORTS >= 1, "L2Cache must have at least one memory port");
    static_assert((MEM_PORTS & (MEM_PORTS - 1)) == 0, "L2Cache memory port count must be a power of two");

    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH / 8;
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH / 32;
    static constexpr size_t LINE_BEATS = CACHE_LINE_SIZE / PORT_BYTES;
    static constexpr size_t LINE_BEAT_BITS = LINE_BEATS <= 1 ? 1 : clog2(LINE_BEATS);
    static constexpr size_t SETS = CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    static constexpr size_t SET_BITS = clog2(SETS);
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    static constexpr size_t WAY_BITS = clog2(WAYS);
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    static constexpr size_t DATA_BANKS = WAYS * LINE_WORDS;
    static constexpr size_t MEM_PORT_BITS = clog2(MEM_PORTS);
    static constexpr size_t MEM_PORT_ADDR_BITS = MEM_ADDR_BITS - MEM_PORT_BITS;
    static constexpr uint64_t MEM_ADDR_MASK64 = (MEM_ADDR_BITS >= 64) ? ~0ull : ((1ull << MEM_ADDR_BITS) - 1ull);
    static constexpr uint64_t MEM_PORT_ADDR_MASK64 = (MEM_PORT_ADDR_BITS >= 64) ? ~0ull : ((1ull << MEM_PORT_ADDR_BITS) - 1ull);

    static constexpr uint64_t ST_IDLE = 0;
    static constexpr uint64_t ST_INIT = 1;
    static constexpr uint64_t ST_LOOKUP = 2;
    static constexpr uint64_t ST_AXI_AR = 3;
    static constexpr uint64_t ST_AXI_R = 4;
    static constexpr uint64_t ST_DONE = 5;
    static constexpr uint64_t ST_CROSS_AR0 = 6;
    static constexpr uint64_t ST_CROSS_R0 = 7;
    static constexpr uint64_t ST_CROSS_AR1 = 8;
    static constexpr uint64_t ST_CROSS_R1 = 9;
    static constexpr uint64_t ST_EVICT_AW = 10;
    static constexpr uint64_t ST_EVICT_W = 11;
    static constexpr uint64_t ST_EVICT_B = 12;
    static constexpr uint64_t ST_CROSS_WRITE_LOOKUP = 13;
    static constexpr uint64_t ST_CROSS_DONE = 14;

public:
    __PORT(bool) i_read_in;
    __PORT(bool) i_write_in;
    __PORT(uint32_t) i_addr_in;
    __PORT(uint32_t) i_write_data_in;
    __PORT(uint8_t) i_write_mask_in;
    __PORT(logic<PORT_BITWIDTH>) i_read_data_out = __VAR(read_data_comb_func());
    __PORT(bool) i_wait_out = __VAR(i_wait_comb_func());

    __PORT(bool) d_read_in;
    __PORT(bool) d_write_in;
    __PORT(uint32_t) d_addr_in;
    __PORT(uint32_t) d_write_data_in;
    __PORT(uint8_t) d_write_mask_in;
    __PORT(logic<PORT_BITWIDTH>) d_read_data_out = __VAR(read_data_comb_func());
    __PORT(bool) d_wait_out = __VAR(d_wait_comb_func());

    __PORT(uint32_t) memory_base_in;
    __PORT(uint32_t) memory_size_in;

    __PORT(bool) axi_awvalid_out[MEM_PORTS];
    __PORT(u<MEM_ADDR_BITS>) axi_awaddr_out[MEM_PORTS];
    __PORT(u<4>) axi_awid_out[MEM_PORTS];
    __PORT(bool) axi_awready_in[MEM_PORTS];

    __PORT(bool) axi_wvalid_out[MEM_PORTS];
    __PORT(logic<PORT_BITWIDTH>) axi_wdata_out[MEM_PORTS];
    __PORT(bool) axi_wlast_out[MEM_PORTS];
    __PORT(bool) axi_wready_in[MEM_PORTS];

    __PORT(bool) axi_bready_out[MEM_PORTS];
    __PORT(bool) axi_bvalid_in[MEM_PORTS];
    __PORT(u<4>) axi_bid_in[MEM_PORTS];

    __PORT(bool) axi_arvalid_out[MEM_PORTS];
    __PORT(u<MEM_ADDR_BITS>) axi_araddr_out[MEM_PORTS];
    __PORT(u<4>) axi_arid_out[MEM_PORTS];
    __PORT(bool) axi_arready_in[MEM_PORTS];

    __PORT(bool) axi_rready_out[MEM_PORTS];
    __PORT(bool) axi_rvalid_in[MEM_PORTS];
    __PORT(logic<PORT_BITWIDTH>) axi_rdata_in[MEM_PORTS];
    __PORT(bool) axi_rlast_in[MEM_PORTS];
    __PORT(u<4>) axi_rid_in[MEM_PORTS];

    bool debugen_in;

private:
    RAM1PORT<32, SETS> data_ram[DATA_BANKS];
    RAM1PORT<TAG_BITS + 2, SETS> tag_ram[WAYS]; // {valid, dirty, tag}

    reg<u<4>> state_reg;
    reg<u32> req_addr_reg;
    reg<u32> req_write_data_reg;
    reg<u8> req_write_mask_reg;
    reg<u1> req_read_reg;
    reg<u1> req_write_reg;
    reg<u1> req_port_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<WAY_BITS>> fill_way_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<logic<PORT_BITWIDTH>> last_data_reg;
    reg<logic<PORT_BITWIDTH>> cross_low_reg;
    reg<logic<PORT_BITWIDTH>> cross_high_reg;
    reg<u<LINE_BEAT_BITS>> fill_beat_reg;
    reg<u<LINE_BEAT_BITS>> evict_beat_reg;

    __LAZY_COMB(active_is_d_comb, bool)
        return active_is_d_comb = d_write_in() || d_read_in();
    }

    __LAZY_COMB(active_read_comb, bool)
        return active_read_comb = d_read_in() || (!d_write_in() && i_read_in());
    }

    __LAZY_COMB(active_write_comb, bool)
        return active_write_comb = d_write_in() || (!d_read_in() && !d_write_in() && i_write_in());
    }

    __LAZY_COMB(active_addr_comb, uint32_t)
        return active_addr_comb = active_is_d_comb_func() ? d_addr_in() : i_addr_in();
    }

    __LAZY_COMB(active_write_data_comb, uint32_t)
        return active_write_data_comb = active_is_d_comb_func() ? d_write_data_in() : i_write_data_in();
    }

    __LAZY_COMB(active_write_mask_comb, uint8_t)
        return active_write_mask_comb = active_is_d_comb_func() ? d_write_mask_in() : i_write_mask_in();
    }

    __LAZY_COMB(req_set_comb, u<SET_BITS>)
        return req_set_comb = (u<SET_BITS>)((uint32_t)req_addr_reg >> LINE_BITS);
    }

    __LAZY_COMB(active_set_comb, u<SET_BITS>)
        return active_set_comb = (u<SET_BITS>)(active_addr_comb_func() >> LINE_BITS);
    }

    __LAZY_COMB(req_word_comb, u<WORD_BITS>)
        return req_word_comb = (u<WORD_BITS>)(((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1));
    }

    __LAZY_COMB(req_beat_comb, u<LINE_BEAT_BITS>)
        return req_beat_comb = (u<LINE_BEAT_BITS>)(((uint32_t)req_addr_reg & (CACHE_LINE_SIZE - 1)) / PORT_BYTES);
    }

    __LAZY_COMB(req_tag_comb, u<TAG_BITS>)
        return req_tag_comb = (u<TAG_BITS>)((uint32_t)req_addr_reg >> (LINE_BITS + SET_BITS));
    }

    __LAZY_COMB(active_cross_line_read_comb, bool)
        active_cross_line_read_comb = active_read_comb_func() &&
            ((active_addr_comb_func() & 3u) != 0) &&
            (((active_addr_comb_func() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1);
        return active_cross_line_read_comb;
    }

    __LAZY_COMB(req_cross_line_write_comb, bool)
        uint32_t byte;
        uint32_t word;
        uint32_t i;
        byte = (uint32_t)req_addr_reg & 3u;
        word = ((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1);
        req_cross_line_write_comb = false;
        if (req_write_reg && byte != 0 && word == LINE_WORDS - 1) {
            for (i = 0; i < 4; ++i) {
                if ((req_write_mask_reg & (1u << i)) && i + byte >= 4) {
                    req_cross_line_write_comb = true;
                }
            }
        }
        return req_cross_line_write_comb;
    }

    __LAZY_COMB(cross_write_data_comb, uint32_t)
        uint32_t byte;
        byte = (uint32_t)req_addr_reg & 3u;
        return cross_write_data_comb = byte == 0 ? (uint32_t)0 : (uint32_t)req_write_data_reg >> (32 - byte * 8);
    }

    __LAZY_COMB(cross_write_mask_comb, uint8_t)
        uint32_t byte;
        uint32_t i;
        byte = (uint32_t)req_addr_reg & 3u;
        cross_write_mask_comb = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_write_mask_reg & (1u << i)) && i + byte >= 4) {
                cross_write_mask_comb |= 1u << (i + byte - 4);
            }
        }
        return cross_write_mask_comb;
    }

    __LAZY_COMB(addr_in_memory_comb, bool)
        uint32_t addr;
        uint32_t local;
        uint32_t size;
        addr = active_addr_comb_func();
        local = addr - memory_base_in();
        size = memory_size_in();
        addr_in_memory_comb = addr >= memory_base_in() && size != 0 && local < size;
        return addr_in_memory_comb;
    }

    __LAZY_COMB(req_addr_in_memory_comb, bool)
        uint32_t addr;
        uint32_t local;
        uint32_t size;
        addr = req_addr_reg;
        local = addr - memory_base_in();
        size = memory_size_in();
        req_addr_in_memory_comb = addr >= memory_base_in() && size != 0 && local < size;
        return req_addr_in_memory_comb;
    }

    __LAZY_COMB(axi_araddr_full_comb, uint32_t)
        uint32_t line_addr;
        line_addr = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)fill_beat_reg * PORT_BYTES);
        if (state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_R0) {
            line_addr = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)req_beat_comb_func() * PORT_BYTES);
        }
        if (state_reg == ST_CROSS_AR1 || state_reg == ST_CROSS_R1) {
            line_addr = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
        }
        return axi_araddr_full_comb = line_addr;
    }

    __LAZY_COMB(axi_araddr_total_local_comb, uint32_t)
        return axi_araddr_total_local_comb = axi_araddr_full_comb_func() - memory_base_in();
    }

    __LAZY_COMB(axi_araddr_local_comb, u<MEM_ADDR_BITS>)
        return axi_araddr_local_comb = (u<MEM_ADDR_BITS>)((uint64_t)axi_araddr_total_local_comb_func() & MEM_PORT_ADDR_MASK64);
    }

    __LAZY_COMB(axi_ar_sel_comb, uint32_t)
        if constexpr (MEM_PORTS == 1) {
            axi_ar_sel_comb = 0;
        }
        else {
            axi_ar_sel_comb = (axi_araddr_total_local_comb_func() >> MEM_PORT_ADDR_BITS) & (MEM_PORTS - 1);
        }
        return axi_ar_sel_comb;
    }

    __LAZY_COMB(axi_arvalid_comb, bool)
        return axi_arvalid_comb = req_addr_in_memory_comb_func() &&
            (state_reg == ST_AXI_AR || state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_AR1);
    }

    __LAZY_COMB(axi_rready_comb, bool)
        return axi_rready_comb = state_reg == ST_AXI_R || state_reg == ST_CROSS_R0 || state_reg == ST_CROSS_R1;
    }

    __LAZY_COMB(axi_arready_selected_comb, bool)
        size_t i;
        axi_arready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_arready_selected_comb = axi_arready_in[i]();
            }
        }
        return axi_arready_selected_comb;
    }

    __LAZY_COMB(axi_rvalid_selected_comb, bool)
        size_t i;
        axi_rvalid_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_rvalid_selected_comb = axi_rvalid_in[i]();
            }
        }
        return axi_rvalid_selected_comb;
    }

    __LAZY_COMB(axi_rdata_selected_comb, logic<PORT_BITWIDTH>)
        size_t i;
        axi_rdata_selected_comb = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_rdata_selected_comb = axi_rdata_in[i]();
            }
        }
        return axi_rdata_selected_comb;
    }

    __LAZY_COMB(evict_way_comb, u<WAY_BITS>)
        return evict_way_comb = (state_reg == ST_LOOKUP) ? victim_reg : fill_way_reg;
    }

    __LAZY_COMB(evict_valid_comb, bool)
        bool valid;
        size_t i;
        valid = false;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                valid = (bool)tag_ram[i].q_out()[TAG_BITS + 1];
            }
        }
        return evict_valid_comb = valid;
    }

    __LAZY_COMB(evict_dirty_comb, bool)
        bool dirty;
        size_t i;
        dirty = false;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                dirty = (bool)tag_ram[i].q_out()[TAG_BITS];
            }
        }
        return evict_dirty_comb = dirty;
    }

    __LAZY_COMB(evict_tag_comb, u<TAG_BITS>)
        size_t i;
        evict_tag_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                evict_tag_comb = (uint64_t)tag_ram[i].q_out().bits(TAG_BITS - 1, 0);
            }
        }
        return evict_tag_comb;
    }

    __LAZY_COMB(axi_awaddr_full_comb, uint32_t)
        uint32_t addr;
        addr = (((uint32_t)evict_tag_comb_func() << (SET_BITS + LINE_BITS)) |
            ((uint32_t)req_set_comb_func() << LINE_BITS)) + ((uint32_t)evict_beat_reg * PORT_BYTES);
        return axi_awaddr_full_comb = addr;
    }

    __LAZY_COMB(axi_awaddr_total_local_comb, uint32_t)
        return axi_awaddr_total_local_comb = axi_awaddr_full_comb_func() - memory_base_in();
    }

    __LAZY_COMB(axi_awaddr_local_comb, u<MEM_ADDR_BITS>)
        return axi_awaddr_local_comb = (u<MEM_ADDR_BITS>)((uint64_t)axi_awaddr_total_local_comb_func() & MEM_PORT_ADDR_MASK64);
    }

    __LAZY_COMB(axi_aw_sel_comb, uint32_t)
        if constexpr (MEM_PORTS == 1) {
            axi_aw_sel_comb = 0;
        }
        else {
            axi_aw_sel_comb = (axi_awaddr_total_local_comb_func() >> MEM_PORT_ADDR_BITS) & (MEM_PORTS - 1);
        }
        return axi_aw_sel_comb;
    }

    __LAZY_COMB(axi_awvalid_comb, bool)
        return axi_awvalid_comb = req_addr_in_memory_comb_func() && state_reg == ST_EVICT_AW;
    }

    __LAZY_COMB(axi_wvalid_comb, bool)
        return axi_wvalid_comb = req_addr_in_memory_comb_func() && state_reg == ST_EVICT_W;
    }

    __LAZY_COMB(axi_awready_selected_comb, bool)
        size_t i;
        axi_awready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_awready_selected_comb = axi_awready_in[i]();
            }
        }
        return axi_awready_selected_comb;
    }

    __LAZY_COMB(axi_wready_selected_comb, bool)
        size_t i;
        axi_wready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_wready_selected_comb = axi_wready_in[i]();
            }
        }
        return axi_wready_selected_comb;
    }

    __LAZY_COMB(axi_bvalid_selected_comb, bool)
        size_t i;
        axi_bvalid_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_bvalid_selected_comb = axi_bvalid_in[i]();
            }
        }
        return axi_bvalid_selected_comb;
    }

    __LAZY_COMB(evict_line_comb, logic<PORT_BITWIDTH>)
        size_t i;
        size_t way;
        size_t word;
        size_t beat_word;
        way = 0;
        word = 0;
        beat_word = 0;
        evict_line_comb = 0;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word = i % LINE_WORDS;
            if (evict_way_comb_func() == way &&
                word >= (uint32_t)evict_beat_reg * PORT_WORDS &&
                word < ((uint32_t)evict_beat_reg + 1u) * PORT_WORDS) {
                beat_word = word - (uint32_t)evict_beat_reg * PORT_WORDS;
                evict_line_comb.bits(beat_word * 32 + 31, beat_word * 32) = data_ram[i].q_out();
            }
        }
        return evict_line_comb;
    }

    __LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS + 1] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_comb = true;
            }
        }
        return hit_comb;
    }

    __LAZY_COMB(hit_way_comb, u<WAY_BITS>)
        size_t i;
        hit_way_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS + 1] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_way_comb = (u<WAY_BITS>)i;
            }
        }
        return hit_way_comb;
    }

    __LAZY_COMB(hit_aligned_word_comb, uint32_t)
        size_t i;
        size_t way;
        size_t word;
        uint32_t ret;
        way = 0;
        word = 0;
        ret = 0;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word = i % LINE_WORDS;
            if (hit_way_comb_func() == way && req_word_comb_func() == word) {
                ret = (uint32_t)data_ram[i].q_out();
            }
        }
        return hit_aligned_word_comb = ret;
    }

    __LAZY_COMB(hit_aligned_next_word_comb, uint32_t)
        size_t i;
        size_t way;
        size_t word;
        uint32_t ret;
        way = 0;
        word = 0;
        ret = 0;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word = i % LINE_WORDS;
            if (hit_way_comb_func() == way && req_word_comb_func() + 1 == word) {
                ret = (uint32_t)data_ram[i].q_out();
            }
        }
        return hit_aligned_next_word_comb = ret;
    }

    __LAZY_COMB(hit_word_comb, uint32_t)
        size_t i;
        size_t way;
        size_t word_index;
        uint32_t byte;
        uint32_t word;
        way = 0;
        word_index = 0;
        word = 0;
        hit_word_comb = 0;
        byte = (uint32_t)req_addr_reg & 3u;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word_index = i % LINE_WORDS;
            if (hit_way_comb_func() == way && req_word_comb_func() == word_index) {
                word = (uint32_t)data_ram[i].q_out();
                hit_word_comb |= word >> (byte * 8);
            }
            if (byte != 0 && hit_way_comb_func() == way && req_word_comb_func() + 1 == word_index) {
                word = (uint32_t)data_ram[i].q_out();
                hit_word_comb |= word << (32 - byte * 8);
            }
        }
        return hit_word_comb;
    }

    __LAZY_COMB(write_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_addr_reg & 3u;
        old_data = hit_aligned_word_comb_func();
        new_data = (uint32_t)req_write_data_reg << (byte * 8);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_write_mask_reg & (1u << i)) && i + byte < 4) {
                mask |= 0xffu << ((i + byte) * 8);
            }
        }
        return write_word_comb = (old_data & ~mask) | (new_data & mask);
    }

    __LAZY_COMB(write_next_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_addr_reg & 3u;
        old_data = hit_aligned_next_word_comb_func();
        new_data = byte == 0 ? (uint32_t)0 : (uint32_t)req_write_data_reg >> (32 - byte * 8);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_write_mask_reg & (1u << i)) && i + byte >= 4) {
                mask |= 0xffu << ((i + byte - 4) * 8);
            }
        }
        return write_next_word_comb = (old_data & ~mask) | (new_data & mask);
    }

    __LAZY_COMB(axi_aligned_word_comb, uint32_t)
        uint32_t word;
        word = (uint32_t)req_word_comb_func() % PORT_WORDS;
        return axi_aligned_word_comb = (uint32_t)axi_rdata_selected_comb_func().bits(word * 32 + 31, word * 32);
    }

    __LAZY_COMB(fill_write_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_addr_reg & 3u;
        old_data = axi_aligned_word_comb_func();
        new_data = (uint32_t)req_write_data_reg << (byte * 8);
        mask = 0;
        if (req_write_reg) {
            for (i = 0; i < 4; ++i) {
                if ((req_write_mask_reg & (1u << i)) && i + byte < 4) {
                    mask |= 0xffu << ((i + byte) * 8);
                }
            }
            fill_write_word_comb = (old_data & ~mask) | (new_data & mask);
        }
        else {
            fill_write_word_comb = old_data;
        }
        return fill_write_word_comb;
    }

    __LAZY_COMB(fill_write_next_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        uint32_t word;
        byte = (uint32_t)req_addr_reg & 3u;
        word = ((uint32_t)req_word_comb_func() + 1) % PORT_WORDS;
        old_data = 0;
        if ((uint32_t)req_word_comb_func() + 1 < LINE_WORDS) {
            old_data = (uint32_t)axi_rdata_selected_comb_func().bits(word * 32 + 31, word * 32);
        }
        new_data = byte == 0 ? (uint32_t)0 : (uint32_t)req_write_data_reg >> (32 - byte * 8);
        mask = 0;
        if (req_write_reg) {
            for (i = 0; i < 4; ++i) {
                if ((req_write_mask_reg & (1u << i)) && i + byte >= 4) {
                    mask |= 0xffu << ((i + byte - 4) * 8);
                }
            }
            fill_write_next_word_comb = (old_data & ~mask) | (new_data & mask);
        }
        else {
            fill_write_next_word_comb = old_data;
        }
        return fill_write_next_word_comb;
    }

    __LAZY_COMB(hit_beat_comb, logic<PORT_BITWIDTH>)
        size_t i;
        size_t way;
        size_t word_index;
        size_t beat_word;
        way = 0;
        word_index = 0;
        beat_word = 0;
        hit_beat_comb = 0;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word_index = i % LINE_WORDS;
            if (hit_way_comb_func() == way &&
                word_index >= (uint32_t)req_beat_comb_func() * PORT_WORDS &&
                word_index < ((uint32_t)req_beat_comb_func() + 1u) * PORT_WORDS) {
                beat_word = word_index - (uint32_t)req_beat_comb_func() * PORT_WORDS;
                hit_beat_comb.bits(beat_word * 32 + 31, beat_word * 32) = data_ram[i].q_out();
            }
        }
        return hit_beat_comb;
    }

    __LAZY_COMB(cross_read_data_comb, logic<PORT_BITWIDTH>)
        uint32_t low_word;
        uint32_t byte;
        uint32_t low;
        uint32_t high;
        uint32_t data;
        cross_read_data_comb = cross_low_reg;
        byte = (uint32_t)req_addr_reg & 3u;
        low_word = (uint32_t)req_addr_reg % PORT_BYTES / 4u;
        low = (uint32_t)cross_low_reg.bits(low_word * 32 + 31, low_word * 32);
        high = (uint32_t)cross_high_reg.bits(31, 0);
        data = (low >> (byte * 8u)) | (high << (32u - byte * 8u));
        cross_read_data_comb = 0;
        cross_read_data_comb.bits(31, 0) = data;
        return cross_read_data_comb;
    }

    __LAZY_COMB(tag_write_data_comb, logic<TAG_BITS + 2>)
        if (state_reg == ST_INIT) {
            tag_write_data_comb = 0;
        }
        else {
            tag_write_data_comb = (logic<TAG_BITS + 2>)(((uint64_t)1 << (TAG_BITS + 1)) |
                ((uint64_t)req_write_reg << TAG_BITS) | (uint64_t)req_tag_comb_func());
        }
        return tag_write_data_comb;
    }

    __LAZY_COMB(read_data_comb, logic<PORT_BITWIDTH>)
        if (state_reg == ST_DONE) {
            read_data_comb = last_data_reg;
        }
        else if (state_reg == ST_CROSS_DONE) {
            read_data_comb = cross_read_data_comb_func();
        }
        else if (state_reg == ST_LOOKUP && hit_comb_func()) {
            read_data_comb = hit_beat_comb_func();
        }
        else {
            read_data_comb = axi_rdata_selected_comb_func();
        }
        return read_data_comb;
    }

    __LAZY_COMB(i_wait_comb, bool)
        i_wait_comb = false;
        if (i_read_in()) {
            i_wait_comb = true;
            if (state_reg == ST_LOOKUP && !req_port_reg && req_read_reg && hit_comb_func()) {
                i_wait_comb = false;
            }
            if (state_reg == ST_DONE && !req_port_reg && req_read_reg) {
                i_wait_comb = false;
            }
        }
        if (state_reg != ST_IDLE && !(state_reg == ST_LOOKUP && !req_port_reg && req_read_reg && hit_comb_func()) &&
            !(state_reg == ST_DONE && !req_port_reg && req_read_reg)) {
            i_wait_comb = true;
        }
        if (d_read_in() || d_write_in()) {
            i_wait_comb = true;
        }
        return i_wait_comb;
    }

    __LAZY_COMB(d_wait_comb, bool)
        d_wait_comb = false;
        if (d_write_in()) {
            d_wait_comb = true;
            if (state_reg == ST_DONE && req_port_reg && req_write_reg) {
                d_wait_comb = false;
            }
        }
        if (d_read_in()) {
            d_wait_comb = true;
            if (state_reg == ST_LOOKUP && req_port_reg && req_read_reg && hit_comb_func()) {
                d_wait_comb = false;
            }
            if (state_reg == ST_DONE && req_port_reg && req_read_reg) {
                d_wait_comb = false;
            }
        }
        if (state_reg != ST_IDLE && !(state_reg == ST_LOOKUP && req_port_reg && req_read_reg && hit_comb_func()) &&
            !(state_reg == ST_DONE && req_port_reg && (req_read_reg || req_write_reg))) {
            d_wait_comb = true;
        }
        return d_wait_comb;
    }

public:
    void _assign()
    {
        size_t i;
        for (i = 0; i < DATA_BANKS; ++i) {
            data_ram[i].addr_in = __EXPR((state_reg == ST_IDLE) ? active_set_comb_func() : req_set_comb_func());
            data_ram[i].rd_in = __EXPR((state_reg == ST_IDLE && (active_read_comb_func() || active_write_comb_func())) ||
                state_reg == ST_CROSS_WRITE_LOOKUP);
            data_ram[i].wr_in = __EXPR_I(
                (state_reg == ST_AXI_R && axi_rvalid_selected_comb_func() && axi_rready_comb_func() && fill_way_reg == (i / LINE_WORDS) &&
                    (i % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ||
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_write_reg && hit_comb_func() &&
                    hit_way_comb_func() == (i / LINE_WORDS) &&
                    (req_word_comb_func() == (i % LINE_WORDS) ||
                     (((uint32_t)req_addr_reg & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS)))));
            data_ram[i].data_in = __EXPR_I((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) ?
                ((((uint32_t)req_addr_reg & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS)) ?
                    write_next_word_comb_func() : write_word_comb_func()) :
                ((req_write_reg && req_word_comb_func() == (i % LINE_WORDS)) ? fill_write_word_comb_func() :
                 (req_write_reg && ((uint32_t)req_addr_reg & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS)) ? fill_write_next_word_comb_func() :
                    (uint32_t)axi_rdata_selected_comb_func().bits(((i % LINE_WORDS) % PORT_WORDS) * 32 + 31, ((i % LINE_WORDS) % PORT_WORDS) * 32)));
            data_ram[i].id_in = 2000 + i;
        }

        for (i = 0; i < WAYS; ++i) {
            tag_ram[i].addr_in = __EXPR((state_reg == ST_INIT) ? init_set_reg :
                ((state_reg == ST_IDLE) ? active_set_comb_func() : req_set_comb_func()));
            tag_ram[i].rd_in = __EXPR((state_reg == ST_IDLE && (active_read_comb_func() || active_write_comb_func())) ||
                state_reg == ST_CROSS_WRITE_LOOKUP);
            tag_ram[i].wr_in = __EXPR_I((state_reg == ST_INIT) ||
                (state_reg == ST_AXI_R && axi_rvalid_selected_comb_func() && axi_rready_comb_func() && fill_beat_reg == LINE_BEATS - 1 && fill_way_reg == i) ||
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_write_reg && hit_comb_func() && hit_way_comb_func() == i));
            tag_ram[i].data_in = __VAR(tag_write_data_comb_func());
            tag_ram[i].id_in = 2100 + i;
        }

        for (i = 0; i < MEM_PORTS; ++i) {
            axi_awvalid_out[i] = __EXPR_I(axi_awvalid_comb_func() && axi_aw_sel_comb_func() == i);
            axi_awaddr_out[i] = __VAR(axi_awaddr_local_comb_func());
            axi_awid_out[i] = __EXPR((u<4>)0);
            axi_wvalid_out[i] = __EXPR_I(axi_wvalid_comb_func() && axi_aw_sel_comb_func() == i);
            axi_wdata_out[i] = __VAR(evict_line_comb_func());
            axi_wlast_out[i] = __EXPR_I(axi_wvalid_comb_func() && axi_aw_sel_comb_func() == i);
            axi_bready_out[i] = __EXPR_I(axi_aw_sel_comb_func() == i);
            axi_arvalid_out[i] = __EXPR_I(axi_arvalid_comb_func() && axi_ar_sel_comb_func() == i);
            axi_araddr_out[i] = __VAR(axi_araddr_local_comb_func());
            axi_arid_out[i] = __EXPR((u<4>)0);
            axi_rready_out[i] = __EXPR_I(axi_rready_comb_func() && axi_ar_sel_comb_func() == i);
        }
    }

    void _work(bool reset)
    {
        size_t i;
        size_t way;
        for (i = 0; i < DATA_BANKS; ++i) {
            data_ram[i]._work(reset);
        }
        for (way = 0; way < WAYS; ++way) {
            tag_ram[way]._work(reset);
        }

        if (state_reg == ST_INIT) {
            if (init_set_reg == SETS - 1) {
                state_reg._next = ST_IDLE;
            }
            else {
                init_set_reg._next = init_set_reg + 1;
            }
        }
        else if (state_reg == ST_IDLE) {
            if (active_read_comb_func() || active_write_comb_func()) {
                req_addr_reg._next = active_addr_comb_func();
                req_write_data_reg._next = active_write_data_comb_func();
                req_write_mask_reg._next = active_write_mask_comb_func();
                req_read_reg._next = active_read_comb_func();
                req_write_reg._next = active_write_comb_func();
                req_port_reg._next = active_is_d_comb_func();
                state_reg._next = active_cross_line_read_comb_func() ? ST_CROSS_AR0 : ST_LOOKUP;
            }
        }
        else if (state_reg == ST_LOOKUP) {
            if (!req_addr_in_memory_comb_func()) {
                if (req_read_reg) {
                    last_data_reg._next = 0;
                }
                state_reg._next = ST_DONE;
            }
            else if (hit_comb_func()) {
                if (req_read_reg) {
                    last_data_reg._next = hit_beat_comb_func();
                }
                if (req_cross_line_write_comb_func()) {
                    req_addr_reg._next = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                    req_write_data_reg._next = cross_write_data_comb_func();
                    req_write_mask_reg._next = cross_write_mask_comb_func();
                    state_reg._next = ST_CROSS_WRITE_LOOKUP;
                }
                else {
                    state_reg._next = req_write_reg ? ST_DONE : ST_IDLE;
                }
            }
            else {
                fill_way_reg._next = victim_reg;
                fill_beat_reg._next = 0;
                evict_beat_reg._next = 0;
                state_reg._next = (evict_valid_comb_func() && evict_dirty_comb_func()) ? ST_EVICT_AW : ST_AXI_AR;
            }
        }
        else if (state_reg == ST_CROSS_WRITE_LOOKUP) {
            if (!req_addr_in_memory_comb_func()) {
                state_reg._next = ST_DONE;
            }
            else if (hit_comb_func()) {
                state_reg._next = ST_DONE;
            }
            else {
                fill_way_reg._next = victim_reg;
                fill_beat_reg._next = 0;
                evict_beat_reg._next = 0;
                state_reg._next = (evict_valid_comb_func() && evict_dirty_comb_func()) ? ST_EVICT_AW : ST_AXI_AR;
            }
        }
        else if (state_reg == ST_EVICT_AW) {
            if (axi_awvalid_comb_func() && axi_awready_selected_comb_func()) {
                state_reg._next = ST_EVICT_W;
            }
        }
        else if (state_reg == ST_EVICT_W) {
            if (axi_wvalid_comb_func() && axi_wready_selected_comb_func()) {
                state_reg._next = ST_EVICT_B;
            }
        }
        else if (state_reg == ST_EVICT_B) {
            if (axi_bvalid_selected_comb_func()) {
                if (evict_beat_reg == LINE_BEATS - 1) {
                    fill_beat_reg._next = 0;
                    state_reg._next = ST_AXI_AR;
                }
                else {
                    evict_beat_reg._next = evict_beat_reg + 1;
                    state_reg._next = ST_EVICT_AW;
                }
            }
        }
        else if (state_reg == ST_AXI_AR) {
            if (axi_arvalid_comb_func() && axi_arready_selected_comb_func()) {
                state_reg._next = ST_AXI_R;
            }
        }
        else if (state_reg == ST_AXI_R) {
            if (axi_rvalid_selected_comb_func() && axi_rready_comb_func()) {
                if (req_read_reg && fill_beat_reg == req_beat_comb_func()) {
                    last_data_reg._next = axi_rdata_selected_comb_func();
                }
                if (fill_beat_reg == LINE_BEATS - 1) {
                    victim_reg._next = victim_reg + 1;
                    if (req_cross_line_write_comb_func()) {
                        req_addr_reg._next = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                        req_write_data_reg._next = cross_write_data_comb_func();
                        req_write_mask_reg._next = cross_write_mask_comb_func();
                        state_reg._next = ST_CROSS_WRITE_LOOKUP;
                    }
                    else {
                        state_reg._next = ST_DONE;
                    }
                }
                else {
                    fill_beat_reg._next = fill_beat_reg + 1;
                    state_reg._next = ST_AXI_AR;
                }
            }
        }
        else if (state_reg == ST_CROSS_AR0) {
            if (axi_arvalid_comb_func() && axi_arready_selected_comb_func()) {
                state_reg._next = ST_CROSS_R0;
            }
        }
        else if (state_reg == ST_CROSS_R0) {
            if (axi_rvalid_selected_comb_func() && axi_rready_comb_func()) {
                cross_low_reg._next = axi_rdata_selected_comb_func();
                state_reg._next = ST_CROSS_AR1;
            }
        }
        else if (state_reg == ST_CROSS_AR1) {
            if (axi_arvalid_comb_func() && axi_arready_selected_comb_func()) {
                state_reg._next = ST_CROSS_R1;
            }
        }
        else if (state_reg == ST_CROSS_R1) {
            if (axi_rvalid_selected_comb_func() && axi_rready_comb_func()) {
                cross_high_reg._next = axi_rdata_selected_comb_func();
                state_reg._next = ST_CROSS_DONE;
            }
        }
        else if (state_reg == ST_CROSS_DONE) {
            last_data_reg._next = cross_read_data_comb_func();
            state_reg._next = ST_DONE;
        }
        else if (state_reg == ST_DONE) {
            state_reg._next = ST_IDLE;
        }

        if (reset) {
            state_reg.clr();
            req_addr_reg.clr();
            req_write_data_reg.clr();
            req_write_mask_reg.clr();
            req_read_reg.clr();
            req_write_reg.clr();
            req_port_reg.clr();
            victim_reg.clr();
            fill_way_reg.clr();
            init_set_reg.clr();
            last_data_reg.clr();
            cross_low_reg.clr();
            cross_high_reg.clr();
            fill_beat_reg.clr();
            evict_beat_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe()
    {
        size_t i;
        size_t way;
        for (i = 0; i < DATA_BANKS; ++i) {
            data_ram[i]._strobe();
        }
        for (way = 0; way < WAYS; ++way) {
            tag_ram[way]._strobe();
        }
        state_reg.strobe();
        req_addr_reg.strobe();
        req_write_data_reg.strobe();
        req_write_mask_reg.strobe();
        req_read_reg.strobe();
        req_write_reg.strobe();
        req_port_reg.strobe();
        victim_reg.strobe();
        fill_way_reg.strobe();
        init_set_reg.strobe();
        last_data_reg.strobe();
        cross_low_reg.strobe();
        cross_high_reg.strobe();
        fill_beat_reg.strobe();
        evict_beat_reg.strobe();
    }
};
