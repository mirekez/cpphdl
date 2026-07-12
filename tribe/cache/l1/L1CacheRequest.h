#pragma once

#include "L1CacheGeometry.h"
#include "L1CacheState.h"

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Decodes live and registered CPU requests and builds the grouped backing-memory driver.
class L1CacheRequest : public L1CacheState<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE,
    WAYS, DCACHE, ADDR_BITS, PORT_BITWIDTH>
{
protected:
    using Base = L1CacheState<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS, DCACHE,
        ADDR_BITS, PORT_BITWIDTH>;
    using Base::LINE_WORDS;
    using Base::PORT_BYTES;
    using Base::PORT_WORDS;
    using Base::REFILL_BEAT_BITS;
    using Base::SETS;
    using Base::addr_in;
    using Base::cache_disable_in;
    using Base::mem_out;
    using Base::req_reg;
    using Base::refill_reg;
    using Base::state_reg;
    using Base::write_data_in;
    using Base::write_in;
    using Base::write_mask_in;

    // Decode every property of the registered request once for refill and lookup consumers.
    _LAZY_COMB(request_geometry_comb, L1RequestGeometryComb)
        request_geometry_comb = {};
        request_geometry_comb.set = ((uint32_t)req_reg.addr / CACHE_LINE_SIZE) % SETS;
        request_geometry_comb.tag = (uint32_t)req_reg.addr / (CACHE_LINE_SIZE * SETS);
        request_geometry_comb.word = ((uint32_t)req_reg.addr >> 2) & (LINE_WORDS - 1);
        request_geometry_comb.refill_beat =
            ((uint32_t)req_reg.addr & (CACHE_LINE_SIZE - 1)) / PORT_BYTES;
        request_geometry_comb.direct_cross_beat = !req_reg.cache_disable &&
            (((uint32_t)req_reg.addr & 3u) != 0) &&
            (((uint32_t)req_reg.addr % PORT_BYTES) / 4u) + 1 >= PORT_WORDS;
        return request_geometry_comb;
    }

    // Decode live input without deciding issue timing, which depends on the later lookup layer.
    _LAZY_COMB(input_decode_comb, L1InputRequestComb)
        input_decode_comb = {};
        input_decode_comb.set = (addr_in() / CACHE_LINE_SIZE) % SETS;
        input_decode_comb.cacheable = !cache_disable_in() && !(addr_in() & 1u);
        if (DCACHE != 0 && (addr_in() & 3u) != 0 &&
            (((addr_in() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1)) {
            input_decode_comb.cacheable = false;
        }
        if (DCACHE == 0 && (addr_in() & 2u) != 0 &&
            (((addr_in() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1)) {
            input_decode_comb.cacheable = false;
        }
        return input_decode_comb;
    }

    // Build read/write/address/data/mask together so L1MemIf cannot observe mixed request sources.
    _LAZY_COMB(mem_driver_comb, L1MemDriver)
        mem_driver_comb = {};
        mem_driver_comb.write = write_in();
        mem_driver_comb.write_data = write_data_in();
        mem_driver_comb.write_mask = write_mask_in();
        mem_driver_comb.read = state_reg == L1_ST_REFILL && req_reg.read;
        if (state_reg == L1_ST_REFILL && req_reg.read && req_reg.cacheable) {
            mem_driver_comb.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) +
                (uint32_t)refill_reg.beat * PORT_BYTES;
        }
        else if (state_reg == L1_ST_REFILL && req_reg.read) {
            if (DCACHE != 0 && !req_reg.cache_disable) {
                mem_driver_comb.addr = request_geometry_comb_func().direct_cross_beat ?
                    (uint32_t)req_reg.addr :
                    ((uint32_t)req_reg.addr & ~(uint32_t)(PORT_BYTES - 1));
            }
            else {
                mem_driver_comb.addr = req_reg.addr;
            }
        }
        else {
            mem_driver_comb.addr = addr_in();
        }
        return mem_driver_comb;
    }
};
