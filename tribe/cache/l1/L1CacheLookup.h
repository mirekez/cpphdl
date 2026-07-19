#pragma once

#include "L1CacheRefill.h"

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Performs one associative lookup and derives RAM issue/refill-tag controls from that result.
class L1CacheLookup : public L1CacheRefill<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE,
    WAYS, DCACHE, ADDR_BITS, PORT_BITWIDTH>
{
protected:
    using Base = L1CacheRefill<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS, DCACHE,
        ADDR_BITS, PORT_BITWIDTH>;
    using Base::TAG_BITS;
    using Base::addr_in;
    using Base::assemble_line_word;
    using Base::even_ram;
    using Base::flush_in;
    using Base::input_decode_comb_func;
    using Base::response_reg;
    using Base::odd_ram;
    using Base::read_in;
    using Base::req_reg;
    using Base::request_geometry_comb_func;
    using Base::stall_in;
    using Base::state_reg;
    using Base::tag_epoch_reg;
    using Base::tag_set_epoch_reg;
    using Base::tag_ram;

    // Check valid, epoch, and tag together in C++ tests without exporting a specialization-bound SV helper.
#ifndef SYNTHESIS
    static bool tag_matches(const logic<256>& entry, uint32_t tag, bool epoch,
        uint8_t set_epoch)
    {
        return (bool)entry[TAG_BITS + 9] && (bool)entry[TAG_BITS + 8] == epoch &&
            entry.bits(TAG_BITS + 7, TAG_BITS) == set_epoch &&
            entry.bits(TAG_BITS - 1, 0) == tag;
    }
#endif

    // Compare tags once and return the matching way and assembled data as one coherent result.
    _LAZY_COMB(lookup_comb, L1LookupComb)
        size_t i;
        uint32_t word;
        uint32_t byte;
        logic<128> even_line;
        logic<128> odd_line;
        logic<256> tag_entry;
        lookup_comb = {};
        word = (uint32_t)request_geometry_comb_func().word;
        byte = (uint32_t)req_reg.addr & 3u;
        even_line = 0;
        odd_line = 0;
        tag_entry = 0;
        if (state_reg == L1_ST_LOOKUP && req_reg.read && req_reg.cacheable) {
            for (i = 0; i < WAYS; ++i) {
                tag_entry = tag_ram[i].q_out();
                // Keep the parameter-dependent slices in this specialization's module body.
                if ((bool)tag_entry[TAG_BITS + 9] &&
                    (bool)tag_entry[TAG_BITS + 8] == (bool)tag_epoch_reg &&
                    tag_entry.bits(TAG_BITS + 7, TAG_BITS) ==
                        tag_set_epoch_reg[(uint32_t)request_geometry_comb_func().set] &&
                    tag_entry.bits(TAG_BITS - 1, 0) ==
                        (uint32_t)request_geometry_comb_func().tag) {
                    lookup_comb.hit = true;
                    lookup_comb.way = i;
                    even_line = even_ram[i].q_out();
                    odd_line = odd_ram[i].q_out();
                }
            }
        }
        if (lookup_comb.hit) {
            lookup_comb.data = assemble_line_word(even_line, odd_line, word, byte);
        }
        return lookup_comb;
    }

    // Decide acceptance and RAM-read issue together because hit chaining depends on the current lookup result.
    _LAZY_COMB(input_request_comb, L1InputRequestComb)
        input_request_comb = input_decode_comb_func();
        input_request_comb.start = false;
        if (read_in() && !stall_in()) {
            if (state_reg == L1_ST_IDLE) input_request_comb.start = true;
            if (state_reg == L1_ST_DONE && req_reg.cacheable &&
                addr_in() != (uint32_t)response_reg.addr) input_request_comb.start = true;
            if (state_reg == L1_ST_LOOKUP && req_reg.read && lookup_comb_func().hit &&
                addr_in() != (uint32_t)req_reg.addr) input_request_comb.start = true;
        }
        input_request_comb.issue = (flush_in() && read_in()) || input_request_comb.start;
        return input_request_comb;
    }

    // Build the valid/epoch/tag payload installed after the final refill beat.
    _LAZY_COMB(refill_tag_comb, logic<ADDR_BITS - clog2(TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 10>)
        refill_tag_comb = request_geometry_comb_func().tag;
        refill_tag_comb.bits(TAG_BITS + 7, TAG_BITS) =
            tag_set_epoch_reg[(uint32_t)request_geometry_comb_func().set];
        refill_tag_comb[TAG_BITS + 8] = (bool)tag_epoch_reg;
        refill_tag_comb[TAG_BITS + 9] = true;
        return refill_tag_comb;
    }
};
