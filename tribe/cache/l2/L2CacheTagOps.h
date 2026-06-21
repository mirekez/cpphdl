#pragma once

#include "L2CacheRegionRouter.h"

template<size_t WAY_BITS>
struct L2CacheHitResult
{
    bool hit;
    u<WAY_BITS> way;
};

template<class GEOM>
class L2CacheTagOps : public GEOM
{
public:
    // Re-export geometry constants for tag packing/search; called by tag helpers and inherited layers.
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Pack valid/dirty/tag into one tag RAM entry; called by L2CacheOOPrimitives and future refill/writeback state.
    static logic<GEOM::TAG_RAM_BITS> make_tag(bool valid, bool dirty, u<GEOM::TAG_BITS> tag)
    {
        logic<GEOM::TAG_RAM_BITS> ret;

        ret = 0;
        ret.bits(GEOM::TAG_BITS - 1, 0) = tag;
        ret[GEOM::TAG_BITS] = dirty;
        ret[GEOM::TAG_BITS + 1] = valid;
        return ret;
    }

    // Read the valid bit from a tag entry; called by find_hit() and future replacement logic.
    static bool tag_valid(logic<GEOM::TAG_RAM_BITS> entry)
    {
        return entry[GEOM::TAG_BITS + 1];
    }

    // Read the dirty bit from a tag entry; called by future victim/writeback logic.
    static bool tag_dirty(logic<GEOM::TAG_RAM_BITS> entry)
    {
        return entry[GEOM::TAG_BITS];
    }

    // Extract the tag payload from a tag entry; called by find_hit() and future diagnostics.
    static u<GEOM::TAG_BITS> tag_value(logic<GEOM::TAG_RAM_BITS> entry)
    {
        return (uint64_t)entry.bits(GEOM::TAG_BITS - 1, 0);
    }

    // Search all ways for a valid matching tag; called by L2CacheOOPrimitives and future lookup control.
    static L2CacheHitResult<GEOM::WAY_BITS> find_hit(const array<logic<GEOM::TAG_RAM_BITS>, GEOM::WAYS, true>& tags,
        u<GEOM::TAG_BITS> request_tag)
    {
        size_t i;
        L2CacheHitResult<GEOM::WAY_BITS> result;

        result.hit = false;
        result.way = 0;
        for (i = 0; i < GEOM::WAYS; ++i) {
            if (tag_valid(tags[i]) && tag_value(tags[i]) == request_tag) {
                result.hit = true;
                result.way = (u<GEOM::WAY_BITS>)i;
            }
        }
        return result;
    }
};
