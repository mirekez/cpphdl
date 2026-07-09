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
    using Base::last_data_reg;
    using Base::cross_low_reg;
    using Base::cross_high_reg;
    using Base::req_word_comb_func;
    using Base::req_beat_comb_func;
    using Base::req_tag_comb_func;
    using Base::axi_out_selected_resp_comb_func;

    _LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        for (i = 0; i < WAYS; ++i) {
            if (tag_q_reg[i][TAG_BITS + 1] &&
                tag_q_reg[i].bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_comb = true;
            }
        }
        return hit_comb;
    }

    // Way index selected by the associative tag compare.
    _LAZY_COMB(hit_way_comb, u<(WAYS <= 1 ? 1 : clog2(WAYS))>)
        size_t i;
        hit_way_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_q_reg[i][TAG_BITS + 1] &&
                tag_q_reg[i].bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_way_comb = i;
            }
        }
        return hit_way_comb;
    }

    // Aligned 32-bit word from the hit way at req_word_comb.
    _LAZY_COMB(hit_aligned_word_comb, uint32_t)
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
                ret = (uint32_t)data_q_reg[i];
            }
        }
        return hit_aligned_word_comb = ret;
    }

    // Aligned next 32-bit word from the hit way for unaligned write merging.
    _LAZY_COMB(hit_aligned_next_word_comb, uint32_t)
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
                ret = (uint32_t)data_q_reg[i];
            }
        }
        return hit_aligned_next_word_comb = ret;
    }

    // Assemble an unaligned 32-bit read from one or two cached words in the hit way.
    _LAZY_COMB(hit_word_comb, uint32_t)
        size_t i;
        size_t way;
        size_t word_index;
        uint32_t byte;
        uint32_t word;
        way = 0;
        word_index = 0;
        word = 0;
        hit_word_comb = 0;
        byte = (uint32_t)req_reg.addr & 3u;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word_index = i % LINE_WORDS;
            if (hit_way_comb_func() == way && req_word_comb_func() == word_index) {
                word = (uint32_t)data_q_reg[i];
                // Low part comes from the addressed word shifted down by byte offset.
                hit_word_comb |= word >> (byte * 8);
            }
            if (byte != 0 && hit_way_comb_func() == way && req_word_comb_func() + 1 == word_index) {
                word = (uint32_t)data_q_reg[i];
                // High part comes from the next word shifted into the upper result bytes.
                hit_word_comb |= word << (32 - byte * 8);
            }
        }
        return hit_word_comb;
    }

    // Merge write bytes into the addressed cached word, preserving unmasked bytes.
    _LAZY_COMB(write_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_reg.addr & 3u;
        old_data = hit_aligned_word_comb_func();
        new_data = (uint32_t)req_reg.write_data << (byte * 8);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) && i + byte < 4) {
                // Keep only byte lanes that still land in the addressed word.
                mask |= 0xffu << ((i + byte) * 8);
            }
        }
        return write_word_comb = (old_data & ~mask) | (new_data & mask);
    }

    // Merge spillover write bytes into the following cached word.
    _LAZY_COMB(write_next_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_reg.addr & 3u;
        old_data = hit_aligned_next_word_comb_func();
        new_data = byte == 0 ? (uint32_t)0 : (uint32_t)req_reg.write_data >> (32 - byte * 8);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                // Remap lanes that crossed the word boundary down to byte lanes [0..3].
                mask |= 0xffu << ((i + byte - 4) * 8);
            }
        }
        return write_next_word_comb = (old_data & ~mask) | (new_data & mask);
    }

    // Aligned 32-bit request word extracted from the current AXI read beat.
    _LAZY_COMB(axi_aligned_word_comb, uint32_t)
        uint32_t word;
        word = (uint32_t)req_word_comb_func() % PORT_WORDS;
        return axi_aligned_word_comb = (uint32_t)(axi_out_selected_resp_comb_func().r.data >> (word * 32u));
    }

    // Merge a pending write into the addressed word while filling a cache line from AXI.
    _LAZY_COMB(fill_write_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        byte = (uint32_t)req_reg.addr & 3u;
        old_data = axi_aligned_word_comb_func();
        new_data = (uint32_t)req_reg.write_data << (byte * 8);
        mask = 0;
        if (req_reg.write) {
            for (i = 0; i < 4; ++i) {
                if ((req_reg.write_mask & (1u << i)) && i + byte < 4) {
                    // Byte lanes before the word boundary update the addressed fill word.
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

    // Merge spillover write bytes into the following word while filling from AXI.
    _LAZY_COMB(fill_write_next_word_comb, uint32_t)
        size_t i;
        uint32_t old_data;
        uint32_t new_data;
        uint32_t mask;
        uint32_t byte;
        uint32_t word;
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_word_comb_func() + 1) % PORT_WORDS;
        old_data = 0;
        if ((uint32_t)req_word_comb_func() + 1 < LINE_WORDS) {
            old_data = (uint32_t)(axi_out_selected_resp_comb_func().r.data >> (word * 32u));
        }
        new_data = byte == 0 ? (uint32_t)0 : (uint32_t)req_reg.write_data >> (32 - byte * 8);
        mask = 0;
        if (req_reg.write) {
            for (i = 0; i < 4; ++i) {
                if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                    // Byte lanes beyond the word boundary update the next fill word.
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

    // Pack the hit way words for the requested PORT_BITWIDTH beat returned to L1.
    _LAZY_COMB(hit_beat_comb, logic<PORT_BITWIDTH>)
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
                // Preserve AXI/L1 beat order: word zero occupies bits [31:0], and so on.
                hit_beat_comb.bits(beat_word * 32 + 31, beat_word * 32) = data_q_reg[i];
            }
        }
        return hit_beat_comb;
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
                ((uint64_t)req_reg.write << TAG_BITS) | (uint64_t)req_tag_comb_func();
        }
        return tag_write_data_comb;
    }

    // Read data mux for held responses, cross-line reads, cache hits, or live AXI fill data.
    _LAZY_COMB(read_data_comb, logic<PORT_BITWIDTH>)
        if (state_reg != ST_IDLE && req_reg.from_slave) {
            // External AXI-master completions are returned through axi_in[*];
            // never expose their live memory beat on the CPU/L1 read-data port.
            read_data_comb = 0;
        }
        else if (state_reg == ST_DONE) {
            read_data_comb = last_data_reg;
        }
        else if (state_reg == ST_CROSS_DONE) {
            read_data_comb = cross_read_data_comb_func();
        }
        else if (state_reg == ST_LOOKUP && hit_comb_func()) {
            read_data_comb = hit_beat_comb_func();
        }
        else if (state_reg == ST_AXI_R || state_reg == ST_IO_R || state_reg == ST_CROSS_R0 || state_reg == ST_CROSS_R1) {
            read_data_comb = axi_out_selected_resp_comb_func().r.data;
        }
        else {
            read_data_comb = 0;
        }
        return read_data_comb;
    }

    // Instruction-side wait/ready generation with data-side priority.
};
