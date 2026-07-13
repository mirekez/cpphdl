#pragma once

#include "L2CacheMemory.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Implements tag lookup, word merging, fill merging, hit-beat packing, and two-beat/cross-line response glue.
class L2CacheTagData : public L2CacheMemory<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>
{
protected:
    using Base = L2CacheMemory<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>;
public:

protected:
    using Base::LINE_WORDS;
    using Base::PORT_BYTES;
    using Base::PORT_WORDS;
    using Base::TAG_BITS;
    using Base::DATA_BANKS;
    using Base::data_q_reg;
    using Base::tag_q_reg;
    using Base::state_reg;
    using Base::req_reg;
    using Base::CPU_RESPONSE_INDEX;
    using Base::response_reg;
    using Base::cross_low_reg;
    using Base::cross_high_reg;
    using Base::request_geometry_comb_func;
    using Base::axi_out_selected_resp_comb_func;

    // Compare tags once, select one way, and derive all hit data from that same
    // selection so hit control, read data, and store merging cannot disagree.
    _LAZY_COMB(hit_lookup_comb, L2HitLookupComb)
        uint32_t i;
        uint32_t way;
        size_t word_index;
        size_t beat_word;
        uint32_t byte;
        uint32_t word_data;
        hit_lookup_comb = {};
        way = 0;
        word_index = 0;
        beat_word = 0;
        byte = (uint32_t)req_reg.addr & 3u;
        word_data = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_q_reg[i][TAG_BITS + 1] &&
                tag_q_reg[i].bits(TAG_BITS - 1, 0) == request_geometry_comb_func().tag) {
                hit_lookup_comb.hit = true;
                hit_lookup_comb.way = i;
            }
        }
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word_index = i % LINE_WORDS;
            if (hit_lookup_comb.hit && (uint32_t)hit_lookup_comb.way == way) {
                word_data = (uint32_t)data_q_reg[i];
                if (request_geometry_comb_func().word == word_index) {
                    hit_lookup_comb.aligned_word = word_data;
                    hit_lookup_comb.read_word |= word_data >> (byte * 8u);
                }
                if ((uint32_t)request_geometry_comb_func().word + 1u == word_index) {
                    hit_lookup_comb.aligned_next_word = word_data;
                    if (byte != 0) {
                        hit_lookup_comb.read_word |= word_data << (32u - byte * 8u);
                    }
                }
                if (word_index >= (uint32_t)request_geometry_comb_func().beat * PORT_WORDS &&
                    word_index < ((uint32_t)request_geometry_comb_func().beat + 1u) * PORT_WORDS) {
                    beat_word = word_index - (uint32_t)request_geometry_comb_func().beat * PORT_WORDS;
                    // Preserve AXI/L1 beat order: word zero occupies bits [31:0], and so on.
                    hit_lookup_comb.beat.bits(beat_word * 32 + 31, beat_word * 32) = data_q_reg[i];
                }
            }
        }
        return hit_lookup_comb;
    }

    // Merge both words of a cache-hit store together from one byte offset and
    // one selected-way snapshot.
    _LAZY_COMB(hit_write_pair_comb, L2WordPairComb)
        uint32_t i;
        uint32_t byte;
        uint32_t word_mask;
        uint32_t next_word_mask;
        uint32_t word_data;
        uint32_t next_word_data;
        byte = (uint32_t)req_reg.addr & 3u;
        word_mask = 0;
        next_word_mask = 0;
        word_data = (uint32_t)req_reg.write_data << (byte * 8u);
        next_word_data = byte == 0 ? 0 : (uint32_t)req_reg.write_data >> (32u - byte * 8u);
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) && i + byte < 4) {
                word_mask |= 0xffu << ((i + byte) * 8u);
            }
            if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                next_word_mask |= 0xffu << ((i + byte - 4u) * 8u);
            }
        }
        hit_write_pair_comb.word = ((uint32_t)hit_lookup_comb_func().aligned_word & ~word_mask) |
            (word_data & word_mask);
        hit_write_pair_comb.next_word = ((uint32_t)hit_lookup_comb_func().aligned_next_word & ~next_word_mask) |
            (next_word_data & next_word_mask);
        return hit_write_pair_comb;
    }

    // Merge both words of a store-miss fill together from one AXI beat and one
    // byte-offset decode.
    _LAZY_COMB(fill_write_pair_comb, L2WordPairComb)
        uint32_t i;
        uint32_t byte;
        uint32_t word;
        uint32_t next_word;
        uint32_t old_word;
        uint32_t old_next_word;
        uint32_t word_mask;
        uint32_t next_word_mask;
        uint32_t word_data;
        uint32_t next_word_data;
        byte = (uint32_t)req_reg.addr & 3u;
        word = (uint32_t)request_geometry_comb_func().word % PORT_WORDS;
        next_word = ((uint32_t)request_geometry_comb_func().word + 1u) % PORT_WORDS;
        old_word = (uint32_t)(axi_out_selected_resp_comb_func().r.data >> (word * 32u));
        old_next_word = 0;
        if ((uint32_t)request_geometry_comb_func().word + 1u < LINE_WORDS) {
            old_next_word = (uint32_t)(axi_out_selected_resp_comb_func().r.data >> (next_word * 32u));
        }
        word_mask = 0;
        next_word_mask = 0;
        word_data = (uint32_t)req_reg.write_data << (byte * 8u);
        next_word_data = byte == 0 ? 0 : (uint32_t)req_reg.write_data >> (32u - byte * 8u);
        if (req_reg.write) {
            for (i = 0; i < 4; ++i) {
                if ((req_reg.write_mask & (1u << i)) && i + byte < 4) {
                    word_mask |= 0xffu << ((i + byte) * 8u);
                }
                if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                    next_word_mask |= 0xffu << ((i + byte - 4u) * 8u);
                }
            }
        }
        fill_write_pair_comb.word = (old_word & ~word_mask) | (word_data & word_mask);
        fill_write_pair_comb.next_word = (old_next_word & ~next_word_mask) |
            (next_word_data & next_word_mask);
        return fill_write_pair_comb;
    }

    // Assemble a cross-beat unaligned read from the saved low and high AXI beats.
    _LAZY_COMB(cross_read_data_comb, logic<PORT_BITWIDTH>)
        uint32_t low_word;
        uint32_t byte;
        uint32_t low;
        uint32_t high;
        uint32_t data;
        cross_read_data_comb = cross_low_reg;
        byte = (uint32_t)req_reg.addr & 3u;
        low_word = (uint32_t)req_reg.addr % PORT_BYTES / 4u;
        low = (uint32_t)cross_low_reg.bits(low_word * 32 + 31, low_word * 32);
        high = (uint32_t)cross_high_reg.bits(31, 0);
        // Low bytes come from the end of the first line; high bytes come from word zero of the next line.
        data = (low >> (byte * 8u)) | (high << (32u - byte * 8u));
        cross_read_data_comb = 0;
        // L1 expects cross-line direct data in the low 32 bits of the returned beat.
        cross_read_data_comb.bits(31, 0) = data;
        return cross_read_data_comb;
    }

    // Tag RAM payload: valid, dirty, and tag bits for init/fill/write updates.
    _LAZY_COMB(tag_write_data_comb, logic<(((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8)>)
        if (state_reg == ST_INIT) {
            tag_write_data_comb = 0;
        }
        else {
            tag_write_data_comb = ((uint64_t)1 << (TAG_BITS + 1)) |
                ((uint64_t)req_reg.write << TAG_BITS) | (uint64_t)request_geometry_comb_func().tag;
        }
        return tag_write_data_comb;
    }

    // Return only the registered CPU response beat so no live RAM, FSM, or AXI
    // path reaches either L1 read-data output.
    _LAZY_COMB(read_data_comb, logic<PORT_BITWIDTH>)
        read_data_comb = response_reg[CPU_RESPONSE_INDEX].valid ?
            (logic<PORT_BITWIDTH>)response_reg[CPU_RESPONSE_INDEX].data : logic<PORT_BITWIDTH>(0);
        return read_data_comb;
    }

    // Instruction-side wait/ready generation with data-side priority.
};
