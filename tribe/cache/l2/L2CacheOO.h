#pragma once

#include "../L2Cache.h"
#include "L2CachePortOps.h"

template<size_t CACHE_SIZE_ = 16384, size_t PORT_BITWIDTH_ = 256, size_t CACHE_LINE_SIZE_ = 32,
    size_t WAYS_ = 4, size_t ADDR_BITS_ = 32, size_t MEM_ADDR_BITS_ = 32, size_t MEM_PORTS_ = 1>
// Compose pure helper layers in dependency order; used as the reusable base type for L2CacheOO.
using L2CacheOOBase = L2CachePortOps<
    L2CacheTimeoutOps<
    L2CacheResponseGlue<
    L2CacheTagOps<
    L2CacheRegionRouter<
    L2CacheByteOps<
    L2CacheGeometry<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>>>>>>>;

template<size_t CACHE_SIZE_ = 16384, size_t PORT_BITWIDTH_ = 256, size_t CACHE_LINE_SIZE_ = 32,
    size_t WAYS_ = 4, size_t ADDR_BITS_ = 32, size_t MEM_ADDR_BITS_ = 32, size_t MEM_PORTS_ = 1>
class L2CacheOO : public L2CacheOOBase<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>
    , public L2Cache<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>
{
public:
    // Name the assembled helper stack; used by the geometry re-export and future stateful derived layers.
    using Base = L2CacheOOBase<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>;
    // Name the production-compatible state machine; used by the default Tribe L2 adapter while helper layers are split out.
    using Production = L2Cache<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>;
    // Re-export assembled geometry constants; called by tests and future port-compatible adapter layers.
    L2CACHE_GEOMETRY_CONSTANTS(Base);

    /*
    Assembly plan:

    1. Inherit the production L2Cache so Tribe can use the OOP L2 adapter by
       default while lower layers remain separately tested in tribe/cache/l2/tests.
    2. Add stateful Module-derived layers above Base only after pure request,
       byte-lane, tag, response-glue, routing, and timeout contracts are stable.
    3. Preserve the production L2Cache external ports and checkpoint contract
       only at the final adapter layer, so low-level tests remain small.
    4. Prefer inheritance for real contracts: geometry -> pure operations ->
       state blocks -> AXI/cache-line controller -> final port-compatible top.
    */
};
