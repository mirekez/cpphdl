#pragma once

#include "cpphdl.h"

using namespace cpphdl;

// Re-export geometry constants through inherited helper layers; called by every L2Cache*Ops layer and L2Cache.
#define L2CACHE_GEOMETRY_CONSTANTS(GEOM_TYPE) \
    static constexpr size_t CACHE_SIZE = GEOM_TYPE::CACHE_SIZE; \
    static constexpr size_t PORT_BITWIDTH = GEOM_TYPE::PORT_BITWIDTH; \
    static constexpr size_t CACHE_LINE_SIZE = GEOM_TYPE::CACHE_LINE_SIZE; \
    static constexpr size_t WAYS = GEOM_TYPE::WAYS; \
    static constexpr size_t ADDR_BITS = GEOM_TYPE::ADDR_BITS; \
    static constexpr size_t MEM_ADDR_BITS = GEOM_TYPE::MEM_ADDR_BITS; \
    static constexpr size_t MEM_PORTS = GEOM_TYPE::MEM_PORTS; \
    static constexpr size_t LINE_WORDS = GEOM_TYPE::LINE_WORDS; \
    static constexpr size_t PORT_BYTES = GEOM_TYPE::PORT_BYTES; \
    static constexpr size_t PORT_WORDS = GEOM_TYPE::PORT_WORDS; \
    static constexpr size_t LINE_BEATS = GEOM_TYPE::LINE_BEATS; \
    static constexpr size_t LINE_BEAT_BITS = GEOM_TYPE::LINE_BEAT_BITS; \
    static constexpr size_t SETS = GEOM_TYPE::SETS; \
    static constexpr size_t SET_BITS = GEOM_TYPE::SET_BITS; \
    static constexpr size_t LINE_BITS = GEOM_TYPE::LINE_BITS; \
    static constexpr size_t WORD_BITS = GEOM_TYPE::WORD_BITS; \
    static constexpr size_t WAY_BITS = GEOM_TYPE::WAY_BITS; \
    static constexpr size_t TAG_BITS = GEOM_TYPE::TAG_BITS; \
    static constexpr size_t TAG_RAM_BITS = GEOM_TYPE::TAG_RAM_BITS; \
    static constexpr size_t DATA_BANKS = GEOM_TYPE::DATA_BANKS; \
    static constexpr size_t MEM_PORT_BITS = GEOM_TYPE::MEM_PORT_BITS; \
    static constexpr uint64_t MEM_ADDR_MASK64 = GEOM_TYPE::MEM_ADDR_MASK64

template<size_t CACHE_SIZE_ = 16384, size_t PORT_BITWIDTH_ = 256, size_t CACHE_LINE_SIZE_ = 32,
    size_t WAYS_ = 4, size_t ADDR_BITS_ = 32, size_t MEM_ADDR_BITS_ = 32, size_t MEM_PORTS_ = 1>
class L2CacheGeometry
{
public:
    static_assert(CACHE_LINE_SIZE_ == 32, "L2CacheGeometry currently models 32-byte cache lines");
    static_assert(PORT_BITWIDTH_ >= 32 && PORT_BITWIDTH_ % 32 == 0, "L2Cache port must be whole 32-bit words");
    static_assert((CACHE_LINE_SIZE_ * 8) % PORT_BITWIDTH_ == 0, "L2Cache port must divide a cache line");
    static_assert(WAYS_ > 0, "L2Cache needs at least one way");
    static_assert(CACHE_SIZE_ % (CACHE_LINE_SIZE_ * WAYS_) == 0, "L2Cache geometry must divide evenly");
    static_assert(MEM_PORTS_ >= 1, "L2Cache must have at least one memory port");
    static_assert((MEM_PORTS_ & (MEM_PORTS_ - 1)) == 0, "L2Cache memory port count must be a power of two");

    // Total data capacity; used by SETS/DATA_BANKS and inherited by all cache operation layers.
    static constexpr size_t CACHE_SIZE = CACHE_SIZE_;
    // Width of one L2 data response/request beat; used by beat slicing and cross-beat detection.
    static constexpr size_t PORT_BITWIDTH = PORT_BITWIDTH_;
    // Bytes per cache line; used by address decode, line refill, and writeback helpers.
    static constexpr size_t CACHE_LINE_SIZE = CACHE_LINE_SIZE_;
    // Associativity; used by tag search and victim/state layers.
    static constexpr size_t WAYS = WAYS_;
    // CPU physical address width; used by tag width calculation and address masks.
    static constexpr size_t ADDR_BITS = ADDR_BITS_;
    // External memory address width; used by future AXI master address masking.
    static constexpr size_t MEM_ADDR_BITS = MEM_ADDR_BITS_;
    // Number of external memory regions/ports; used by region routing and arbitration.
    static constexpr size_t MEM_PORTS = MEM_PORTS_;

    // 32-bit words per line; used by word-index decode and data bank addressing.
    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE_ / 4;
    // Bytes per port beat; used by beat-base and beat-word decode.
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH_ / 8;
    // 32-bit words per port beat; used by response glue and cross-beat read detection.
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH_ / 32;
    // Beats required to transfer one line; used by refill/writeback state sequencing.
    static constexpr size_t LINE_BEATS = (CACHE_LINE_SIZE_ * 8) / PORT_BITWIDTH_;
    // Bits needed to count beats inside a line; used by future refill/writeback counters.
    static constexpr size_t LINE_BEAT_BITS = LINE_BEATS <= 1 ? 1 : clog2((CACHE_LINE_SIZE_ * 8) / PORT_BITWIDTH_);
    // Number of indexable sets; used by tag/data RAM addressing.
    static constexpr size_t SETS = CACHE_SIZE_ / CACHE_LINE_SIZE_ / WAYS_;
    // Bits in the set index; used by set_index() and tag width calculation.
    static constexpr size_t SET_BITS = clog2(CACHE_SIZE_ / CACHE_LINE_SIZE_ / WAYS_);
    // Bits in byte offset inside a cache line; used by line_base(), set_index(), and tag_value().
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE_);
    // Bits in word offset inside a cache line; used by future data-bank selectors.
    static constexpr size_t WORD_BITS = clog2(CACHE_LINE_SIZE_ / 4);
    // Bits needed to select a way; used by tag-hit result and victim/state layers.
    static constexpr size_t WAY_BITS = WAYS_ <= 1 ? 1 : clog2(WAYS_);
    // Tag width after line and set bits; used by make_tag(), tag_value(), and find_hit().
    static constexpr size_t TAG_BITS = ADDR_BITS_ - clog2(CACHE_SIZE_ / CACHE_LINE_SIZE_ / WAYS_) - clog2(CACHE_LINE_SIZE_);
    // Physical tag RAM entry width rounded to bytes; used by packed tag arrays.
    static constexpr size_t TAG_RAM_BITS = ((TAG_BITS + 2 + 7) / 8) * 8;
    // Number of word banks across all ways; used by future line data storage.
    static constexpr size_t DATA_BANKS = WAYS_ * (CACHE_LINE_SIZE_ / 4);
    // Bits needed to select an external memory port; used by route() result type.
    static constexpr size_t MEM_PORT_BITS = clog2(MEM_PORTS_);
    // Mask for external memory addresses; used by future AXI master address generation.
    static constexpr uint64_t MEM_ADDR_MASK64 = (MEM_ADDR_BITS_ >= 64) ? ~0ull : ((1ull << MEM_ADDR_BITS_) - 1ull);

    // Return the aligned cache-line address; called by future miss/refill and writeback controllers.
    static uint32_t line_base(uint32_t addr)
    {
        return addr & ~(uint32_t)(CACHE_LINE_SIZE - 1);
    }

    // Return the aligned port-beat address; called by future memory beat request generation.
    static uint32_t beat_base(uint32_t addr)
    {
        return addr & ~(uint32_t)(PORT_BYTES - 1);
    }

    // Return byte offset inside a 32-bit word; called by byte store and unaligned read helpers.
    static uint32_t byte_offset(uint32_t addr)
    {
        return addr & 3u;
    }

    // Return 32-bit word index inside a cache line; called by cross-line checks and data RAM selectors.
    static uint32_t word_index(uint32_t addr)
    {
        return (addr >> 2) & (LINE_WORDS - 1);
    }

    // Return 32-bit word index inside the current port beat; called by cross-beat read detection.
    static uint32_t beat_word_index(uint32_t addr)
    {
        return (addr % PORT_BYTES) / 4u;
    }

    // Return beat index inside a cache line; called by future refill/writeback sequencing.
    static uint32_t beat_index(uint32_t addr)
    {
        return (addr & (CACHE_LINE_SIZE - 1)) / PORT_BYTES;
    }

    // Return cache set index; called by future tag/data RAM lookup.
    static uint32_t set_index(uint32_t addr)
    {
        return (addr >> LINE_BITS) & (SETS - 1);
    }

    // Return cache tag bits from an address; called by future lookup and fill tag creation.
    static uint32_t tag_value(uint32_t addr)
    {
        return addr >> (LINE_BITS + SET_BITS);
    }

    // Detect an unaligned word read spanning two port beats; called by response-glue selection.
    static bool cross_beat_read(uint32_t addr)
    {
        return byte_offset(addr) != 0 && beat_word_index(addr) + 1 >= PORT_WORDS;
    }

    // Detect an unaligned word read spanning two cache lines; called by response-glue selection.
    static bool cross_line_read(uint32_t addr)
    {
        return byte_offset(addr) != 0 && word_index(addr) == LINE_WORDS - 1;
    }

    // Detect a masked store that spills into the next line; called by write-split control logic.
    static bool cross_line_write(uint32_t addr, uint8_t mask)
    {
        uint32_t i;
        uint32_t byte;

        byte = byte_offset(addr);
        if (byte == 0 || word_index(addr) != LINE_WORDS - 1) {
            return false;
        }
        for (i = 0; i < 4; ++i) {
            if ((mask & (1u << i)) && i + byte >= 4) {
                return true;
            }
        }
        return false;
    }
};
