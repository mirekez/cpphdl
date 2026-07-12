#pragma once

#include "L1CacheRequest.h"

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Accumulates split-bank refill data and assembles cached or direct 32-bit response words.
class L1CacheRefill : public L1CacheRequest<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE,
    WAYS, DCACHE, ADDR_BITS, PORT_BITWIDTH>
{
protected:
    using Base = L1CacheRequest<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS, DCACHE,
        ADDR_BITS, PORT_BITWIDTH>;
    using Base::HALF_LINE_BITS;
    using Base::LINE_WORDS;
    using Base::PORT_BYTES;
    using Base::PORT_WORDS;
    using Base::mem_out;
    using Base::refill_reg;
    using Base::req_reg;
    using Base::request_geometry_comb_func;

    // Update even and odd split-bank images from the same accepted memory beat.
    _LAZY_COMB(refill_lines_comb, L1RefillLinesComb)
        size_t i;
        uint32_t word;
        refill_lines_comb = {};
        refill_lines_comb.even = refill_reg.even_line;
        refill_lines_comb.odd = refill_reg.odd_line;
        for (i = 0; i < PORT_WORDS; ++i) {
            word = (uint32_t)refill_reg.beat * PORT_WORDS + i;
            refill_lines_comb.even.bits(word * 16 + 15, word * 16) =
                (uint32_t)mem_out.read_data_out().bits(i * 32 + 15, i * 32);
            refill_lines_comb.odd.bits(word * 16 + 15, word * 16) =
                (uint32_t)mem_out.read_data_out().bits(i * 32 + 31, i * 32 + 16);
        }
        return refill_lines_comb;
    }

    // Assemble one possibly unaligned word from a completed pair of split-bank line images.
    uint32_t assemble_line_word(const logic<128>& even_line, const logic<128>& odd_line,
        uint32_t word, uint32_t byte_offset) const
    {
        uint32_t word_data;
        uint32_t next_word_data;
        uint32_t even_half;
        uint32_t odd_half;
        even_half = (uint32_t)even_line.bits(word * 16 + 15, word * 16);
        odd_half = (uint32_t)odd_line.bits(word * 16 + 15, word * 16);
        word_data = even_half | (odd_half << 16);
        next_word_data = 0;
        if (byte_offset != 0 && word + 1 < LINE_WORDS) {
            even_half = (uint32_t)even_line.bits((word + 1) * 16 + 15, (word + 1) * 16);
            odd_half = (uint32_t)odd_line.bits((word + 1) * 16 + 15, (word + 1) * 16);
            next_word_data = even_half | (odd_half << 16);
        }
        return byte_offset == 0 ? word_data :
            (word_data >> (byte_offset * 8u)) |
            (next_word_data << (32u - byte_offset * 8u));
    }

    // Return the requested word from the refill image including the current memory beat.
    _LAZY_COMB(refill_data_comb, uint32_t)
        return refill_data_comb = assemble_line_word(
            refill_lines_comb_func().even, refill_lines_comb_func().odd,
            (uint32_t)request_geometry_comb_func().word,
            (uint32_t)req_reg.addr & 3u);
    }

    // Assemble one direct memory response, including intra-beat and cross-beat unaligned reads.
    _LAZY_COMB(direct_data_comb, uint32_t)
        uint32_t byte;
        uint32_t word;
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr % PORT_BYTES) / 4u;
        if (!req_reg.cacheable && request_geometry_comb_func().direct_cross_beat) {
            direct_data_comb = (uint32_t)mem_out.read_data_out().bits(31, 0);
        }
        else {
            direct_data_comb = (uint32_t)mem_out.read_data_out().bits(word * 32 + 31, word * 32) >> (byte * 8u);
            if (byte != 0 && word + 1 < PORT_WORDS) {
                direct_data_comb |= (uint32_t)mem_out.read_data_out().bits(
                    (word + 1) * 32 + 31, (word + 1) * 32) << (32u - byte * 8u);
            }
            else if (byte != 0) {
                direct_data_comb |= (uint32_t)mem_out.read_data_out().bits(31, 0) << (32u - byte * 8u);
            }
        }
        return direct_data_comb;
    }
};
