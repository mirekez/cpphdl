#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<size_t TOTAL_CACHE_SIZE_ = 1024, size_t CACHE_LINE_SIZE_ = 32,
    size_t WAYS_ = 2, int DCACHE_ = 0, size_t ADDR_BITS_ = 32,
    size_t PORT_BITWIDTH_ = 32>
// Centralizes L1 geometry and address predicates so every stateful layer uses identical decoding.
class L1CacheGeometry
{
public:
    // Total L1 capacity; used to derive SETS and tag width in state and lookup layers.
    static constexpr size_t TOTAL_CACHE_SIZE = TOTAL_CACHE_SIZE_;
    // Bytes in one line; used by all address decomposition and refill helpers.
    static constexpr size_t CACHE_LINE_SIZE = CACHE_LINE_SIZE_;
    // Associativity; used by set count, lookup iteration, and victim selection.
    static constexpr size_t WAYS = WAYS_;
    // Selects D-cache boundary rules; used by cacheable().
    static constexpr int DCACHE = DCACHE_;
    // Physical address width; used to derive stored tag width.
    static constexpr size_t ADDR_BITS = ADDR_BITS_;
    // L1-to-L2 beat width; used by refill and direct-read decomposition.
    static constexpr size_t PORT_BITWIDTH = PORT_BITWIDTH_;
    // Number of 32-bit words in a line; used by word_index() and line-boundary checks.
    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    // Number of sets; used by set_index(), tag_value(), and RAM depth.
    static constexpr size_t SETS = TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    // Width of a RAM set address; used by the state storage layer.
    static constexpr size_t SET_BITS = clog2(SETS);
    // Width of a word index inside a line; retained for cache datapath selectors.
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    // Width of the line byte offset; used to derive tag width.
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    // Width of each split 16-bit data bank; used by refill and lookup storage.
    static constexpr size_t HALF_LINE_BITS = CACHE_LINE_SIZE * 4;
    // Bytes in one L1-to-L2 beat; used by beat address and refill calculations.
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH / 8;
    // Words in one L1-to-L2 beat; used by refill splitting and direct read glue.
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH / 32;
    // Beats needed to fill a line; used by controller refill sequencing.
    static constexpr size_t REFILL_BEATS = CACHE_LINE_SIZE / PORT_BYTES;
    // Width of the refill counter; used by the state register layer.
    static constexpr size_t REFILL_BEAT_BITS = REFILL_BEATS <= 1 ? 1 : clog2(REFILL_BEATS);
    // Stored address tag width; used by tag RAM entries and lookup comparison.
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    // Width of the replacement-way register; used by refill installation.
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);

    static_assert(CACHE_LINE_SIZE == 32, "L1Cache uses 32-byte cache lines");
    static_assert(PORT_BITWIDTH >= 32 && PORT_BITWIDTH % 32 == 0,
        "L1Cache refill port must be a whole number of 32-bit words");
    static_assert((CACHE_LINE_SIZE * 8) % PORT_BITWIDTH == 0,
        "L1Cache refill port must divide a cache line");
    static_assert(WAYS > 0, "L1Cache needs at least one way");
    static_assert(TOTAL_CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0,
        "L1Cache geometry must divide evenly");
    static_assert(ADDR_BITS > SET_BITS + LINE_BITS,
        "L1Cache address width must include tag bits");

    // Return the containing cache-line address; called by refill memory-address generation.
    static uint32_t line_base(uint32_t addr) { return addr & ~(uint32_t)(CACHE_LINE_SIZE - 1); }
    // Return the containing refill-beat address; called by direct D-cache bypass reads.
    static uint32_t beat_base(uint32_t addr) { return addr & ~(uint32_t)(PORT_BYTES - 1); }
    // Return the set selected by an address; called by request and RAM-control layers.
    static uint32_t set_index(uint32_t addr) { return (addr / CACHE_LINE_SIZE) % SETS; }
    // Return the tag selected by an address; called by lookup and refill-tag construction.
    static uint32_t tag_value(uint32_t addr) { return addr / (CACHE_LINE_SIZE * SETS); }
    // Return the 32-bit word index inside a line; called by lookup/refill data assembly.
    static uint32_t word_index(uint32_t addr) { return (addr >> 2) & (LINE_WORDS - 1); }
    // Return the refill beat containing an address; called by refill sequencing.
    static uint32_t refill_beat(uint32_t addr) { return (addr & (CACHE_LINE_SIZE - 1)) / PORT_BYTES; }
    // Detect a direct unaligned read spanning refill beats; called by memory routing and data assembly.
    static bool direct_cross_beat(uint32_t addr, bool cache_disabled)
    {
        uint32_t byte = addr & 3u;
        uint32_t word = (addr % PORT_BYTES) / 4u;
        return !cache_disabled && byte != 0 && word + 1 >= PORT_WORDS;
    }
    // Decide whether the split 16-bit L1 banks can represent this read; called when accepting CPU input.
    static bool cacheable(uint32_t addr, bool cache_disabled)
    {
        bool result = !cache_disabled && !(addr & 1u);
        if (DCACHE != 0 && (addr & 3u) != 0 && word_index(addr) == LINE_WORDS - 1) result = false;
        if (DCACHE == 0 && (addr & 2u) != 0 && word_index(addr) == LINE_WORDS - 1) result = false;
        return result;
    }
};
