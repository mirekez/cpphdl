#pragma once

#include "L2CacheGeometry.h"

template<class GEOM>
class L2CacheByteOps : public GEOM
{
public:
    // Re-export geometry constants for byte-lane helpers; called by this layer and inherited response glue.
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Merge a masked unaligned store into the addressed word; called by L2CachePrimitives and future write-hit control.
    static uint32_t store_word(uint32_t old_data, uint32_t write_data, uint8_t write_mask, uint32_t byte_offset)
    {
        size_t i;
        uint32_t new_data;
        uint32_t mask;

        new_data = write_data << (byte_offset * 8u);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((write_mask & (1u << i)) && i + byte_offset < 4) {
                mask |= 0xffu << ((i + byte_offset) * 8u);
            }
        }
        return (old_data & ~mask) | (new_data & mask);
    }

    // Merge the spill bytes of an unaligned store into the next word; called by cross-line/cross-word write handling.
    static uint32_t store_next_word(uint32_t old_data, uint32_t write_data, uint8_t write_mask, uint32_t byte_offset)
    {
        size_t i;
        uint32_t new_data;
        uint32_t mask;

        new_data = byte_offset == 0 ? (uint32_t)0 : write_data >> (32u - byte_offset * 8u);
        mask = 0;
        for (i = 0; i < 4; ++i) {
            if ((write_mask & (1u << i)) && i + byte_offset >= 4) {
                mask |= 0xffu << ((i + byte_offset - 4u) * 8u);
            }
        }
        return (old_data & ~mask) | (new_data & mask);
    }

    // Extract spill data for the next word of an unaligned store; called by future write-split datapath.
    static uint32_t cross_write_data(uint32_t write_data, uint32_t byte_offset)
    {
        return byte_offset == 0 ? (uint32_t)0 : write_data >> (32u - byte_offset * 8u);
    }

    // Extract spill byte mask for the next word of an unaligned store; called by L2CachePrimitives and future write-split control.
    static uint8_t cross_write_mask(uint8_t write_mask, uint32_t byte_offset)
    {
        size_t i;
        uint8_t ret;

        ret = 0;
        for (i = 0; i < 4; ++i) {
            if ((write_mask & (1u << i)) && i + byte_offset >= 4) {
                ret |= 1u << (i + byte_offset - 4u);
            }
        }
        return ret;
    }

    // Join two adjacent words for an unaligned read result; called by ResponseGlue::cross_line_read_data().
    static uint32_t unaligned_read_word(uint32_t low_word, uint32_t high_word, uint32_t byte_offset)
    {
        if (byte_offset == 0) {
            return low_word;
        }
        return (low_word >> (byte_offset * 8u)) | (high_word << (32u - byte_offset * 8u));
    }
};
