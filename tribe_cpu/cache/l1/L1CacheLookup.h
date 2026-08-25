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
    using Base::LINE_WORDS;
    using Base::addr_in;
    using Base::assemble_line_word;
    using Base::even_ram;
    using Base::flush_in;
    using Base::input_decode_comb_func;
    using Base::lookup_reg;
    using Base::response_reg;
    using Base::odd_ram;
    using Base::read_in;
    using Base::req_reg;
    using Base::request_geometry_comb_func;
    using Base::selected_line_reg;
    using Base::stall_in;
    using Base::state_reg;
    using Base::tag_epoch_reg;
    using Base::tag_entries_reg;
    using Base::tag_set_epoch_reg;
    using Base::tag_ram;
    using Base::write_in;

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
        if (state_reg == L1_ST_COMPARE && req_reg.read && req_reg.cacheable) {
            for (i = 0; i < WAYS; ++i) {
                tag_entry = tag_entries_reg[i];
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

    // Select only the matching way here. Word/byte extraction occurs after this
    // wide value is registered, keeping the BRAM-to-register path shallow.
    _LAZY_COMB(selected_line_comb, L1SelectedLineState)
        size_t i;
        selected_line_comb = {};
        if (state_reg == L1_ST_SELECT && lookup_reg.hit) {
            for (i = 0; i < WAYS; ++i) {
                if (lookup_reg.way == i) {
                    selected_line_comb.even = even_ram[i].q_out();
                    selected_line_comb.odd = odd_ram[i].q_out();
                    selected_line_comb.addr = (uint32_t)req_reg.addr &
                        ~(uint32_t)(CACHE_LINE_SIZE - 1);
                    selected_line_comb.valid = true;
                }
            }
        }
        return selected_line_comb;
    }

    // A recently selected line is already beyond the tag/data BRAM timing
    // boundaries. Sequential instruction fetches and nearby data reads can
    // therefore use it at one request per cycle without reopening the lookup
    // FSM or recreating a BRAM-to-core combinational path.
    _LAZY_COMB(selected_line_hit_comb, bool)
        return selected_line_hit_comb = state_reg == L1_ST_IDLE && read_in() &&
            !write_in() &&
            !flush_in() && selected_line_reg.valid &&
            input_decode_comb_func().cacheable &&
            ((addr_in() & ~(uint32_t)(CACHE_LINE_SIZE - 1)) ==
                (uint32_t)selected_line_reg.addr);
    }

    _LAZY_COMB(selected_line_data_comb, u32)
        return selected_line_data_comb = assemble_line_word(
            selected_line_reg.even, selected_line_reg.odd,
            (addr_in() >> 2) & (LINE_WORDS - 1), addr_in() & 3u);
    }

    // Extract and align one CPU word from the registered selected line.
    _LAZY_COMB(lookup_data_comb, u32)
        lookup_data_comb = assemble_line_word(selected_line_reg.even,
            selected_line_reg.odd,
            (uint32_t)request_geometry_comb_func().word,
            (uint32_t)req_reg.addr & 3u);
        return lookup_data_comb;
    }

    // Decide acceptance and RAM-read issue together because hit chaining depends on the current lookup result.
    _LAZY_COMB(input_request_comb, L1InputRequestComb)
        input_request_comb = input_decode_comb_func();
        input_request_comb.start = false;
        if (read_in() && !stall_in() && !selected_line_hit_comb_func()) {
            if (state_reg == L1_ST_IDLE) input_request_comb.start = true;
            if (state_reg == L1_ST_DONE && req_reg.cacheable &&
                addr_in() != (uint32_t)response_reg.addr) input_request_comb.start = true;
        }
        // A branch redirect updates the core's registered PC at this edge, so
        // addr_in still contains the discarded PC while flush_in is asserted.
        // Reading the RAM for that address cannot satisfy the redirected fetch
        // and puts Execute's complete branch/target cone on every BRAM enable.
        // The flush handler returns to IDLE; the corrected registered PC issues
        // normally on the following cycle.
        input_request_comb.issue = input_request_comb.start;
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
