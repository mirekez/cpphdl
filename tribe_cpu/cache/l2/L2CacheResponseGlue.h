#pragma once

#include "L2CacheTagOps.h"

template<class GEOM>
class L2CacheResponseGlue : public GEOM
{
public:
    // Re-export geometry constants for response slicing; called by beat_from_line() and cross_line_read_data().
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Slice one port-width beat from a full cache line; called by future read-hit and refill response datapaths.
    static logic<GEOM::PORT_BITWIDTH> beat_from_line(logic<GEOM::CACHE_LINE_SIZE * 8> line, uint32_t beat)
    {
        uint32_t word;
        size_t beat_word;
        logic<GEOM::PORT_BITWIDTH> ret;

        ret = 0;
        for (word = beat * GEOM::PORT_WORDS;
             word < (beat + 1u) * GEOM::PORT_WORDS && word < GEOM::LINE_WORDS;
             ++word) {
            beat_word = word - beat * GEOM::PORT_WORDS;
            ret.bits(beat_word * 32 + 31, beat_word * 32) = line.bits(word * 32 + 31, word * 32);
        }
        return ret;
    }

    // Build an unaligned read response crossing into the next line; called by L2CachePrimitives and future miss glue.
    static logic<GEOM::PORT_BITWIDTH> cross_line_read_data(logic<GEOM::PORT_BITWIDTH> low_beat,
        logic<GEOM::PORT_BITWIDTH> high_beat, uint32_t addr)
    {
        uint32_t low_word;
        uint32_t byte;
        uint32_t low;
        uint32_t high;
        uint32_t data;
        logic<GEOM::PORT_BITWIDTH> ret;

        byte = GEOM::byte_offset(addr);
        low_word = (addr % GEOM::PORT_BYTES) / 4u;
        low = (uint32_t)low_beat.bits(low_word * 32 + 31, low_word * 32);
        high = (uint32_t)high_beat.bits(31, 0);
        data = L2CacheByteOps<GEOM>::unaligned_read_word(low, high, byte);
        ret = 0;
        ret.bits(31, 0) = data;
        return ret;
    }
};
