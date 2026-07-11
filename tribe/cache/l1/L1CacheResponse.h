#pragma once

#include "L1CacheLookup.h"

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Presents CPU response, busy, and performance outputs from one grouped state decision.
class L1CacheResponse : public L1CacheLookup<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE,
    WAYS, DCACHE, ADDR_BITS, PORT_BITWIDTH>
{
protected:
    using Base = L1CacheLookup<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS, DCACHE,
        ADDR_BITS, PORT_BITWIDTH>;
    using Base::direct_data_comb_func;
    using Base::response_reg;
    using Base::lookup_comb_func;
    using Base::read_in;
    using Base::req_reg;
    using Base::state_reg;

    // Select live-hit, held, or bypass data and derive valid/busy from the same state snapshot.
    _LAZY_COMB(cpu_response_comb, L1CpuResponseComb)
        cpu_response_comb = {};
        if (state_reg == L1_ST_LOOKUP && req_reg.read && lookup_comb_func().hit) {
            cpu_response_comb.data = lookup_comb_func().data;
        }
        else if (response_reg.valid) {
            cpu_response_comb.data = response_reg.data;
        }
        else {
            cpu_response_comb.data = direct_data_comb_func();
        }
        cpu_response_comb.addr = response_reg.valid ? (uint32_t)response_reg.addr : (uint32_t)req_reg.addr;
        cpu_response_comb.valid = response_reg.valid ||
            (state_reg == L1_ST_LOOKUP && req_reg.read && lookup_comb_func().hit);
        cpu_response_comb.busy = state_reg == L1_ST_INIT || state_reg == L1_ST_REFILL ||
            (state_reg == L1_ST_LOOKUP && req_reg.read && !lookup_comb_func().hit);
        return cpu_response_comb;
    }

    // Publish all performance classifications together so percentages use mutually consistent state/hit values.
    _LAZY_COMB(perf_comb, L1CachePerf)
        perf_comb = {};
        perf_comb.state = state_reg;
        perf_comb.hit = lookup_comb_func().hit;
        perf_comb.lookup_wait = cpu_response_comb_func().busy && state_reg == L1_ST_LOOKUP;
        perf_comb.refill_wait = cpu_response_comb_func().busy && state_reg == L1_ST_REFILL;
        perf_comb.init_wait = cpu_response_comb_func().busy && state_reg == L1_ST_INIT;
        perf_comb.issue_wait = read_in() && state_reg == L1_ST_IDLE;
        return perf_comb;
    }
};
