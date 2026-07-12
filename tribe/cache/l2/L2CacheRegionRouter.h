#pragma once

#include "L2CacheByteOps.h"

template<size_t MEM_PORT_BITS>
struct L2CacheRegionResult
{
    u<MEM_PORT_BITS> port;
    uint32_t region_base;
    uint32_t local_addr;
    bool uncached;
    bool hit;
};

template<class GEOM>
class L2CacheRegionRouter : public GEOM
{
public:
    // Re-export geometry constants for memory-region routing; called by route() and inherited layers.
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Map an absolute address to memory port/local address/uncached flag; called by L2CachePrimitives and future miss routing.
    static L2CacheRegionResult<GEOM::MEM_PORT_BITS> route(uint32_t addr, uint32_t memory_base,
        const uint32_t (&region_size)[GEOM::MEM_PORTS], const bool (&region_uncached)[GEOM::MEM_PORTS])
    {
        uint32_t i;
        uint64_t base;
        uint64_t local;
        L2CacheRegionResult<GEOM::MEM_PORT_BITS> result;

        local = addr - memory_base;
        base = 0;
        result.port = GEOM::MEM_PORTS - 1;
        result.region_base = 0;
        result.local_addr = (uint32_t)local;
        result.uncached = false;
        result.hit = false;
        for (i = 0; i < GEOM::MEM_PORTS; ++i) {
            if (local >= base && local < base + region_size[i]) {
                result.port = (u<GEOM::MEM_PORT_BITS>)i;
                result.region_base = (uint32_t)base;
                result.local_addr = (uint32_t)(local - base);
                result.uncached = region_uncached[i];
                result.hit = true;
            }
            base += region_size[i];
        }
        return result;
    }
};
