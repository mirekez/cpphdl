#pragma once

#include "L2CacheResponseGlue.h"

template<class GEOM>
class L2CacheTimeoutOps : public GEOM
{
public:
    // Re-export geometry constants for timeout-aware inherited layers; called by the final L2CacheOO stack.
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Advance or clear an operation-age counter; called by L2CacheOOPrimitives and future deadlock watchdog logic.
    static uint32_t next_age(bool active, bool progress, uint32_t age)
    {
        if (!active || progress) {
            return 0;
        }
        return age + 1;
    }

    // Detect a stalled operation exceeding its limit; called by L2CacheOOPrimitives and future timeout trap/debug logic.
    static bool expired(bool active, bool progress, uint32_t age, uint32_t limit)
    {
        return active && !progress && limit != 0 && age >= limit;
    }
};
