#pragma once

#include "L2CacheTagData.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Generates CPU-side wait signals from the active state and completed response ownership.
class L2CacheWait : public L2CacheTagData<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>
{
protected:
    using Base = L2CacheTagData<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>;

public:
    using Base::i_mem_in;
    using Base::d_mem_in;

protected:
    using Base::CPU_RESPONSE_INDEX;
    using Base::response_reg;
    using Base::state_reg;

    // Resolve both CPU waits only from the registered response identity. This
    // keeps the cache lookup and memory-return paths out of the L1 timing path.
    _LAZY_COMB(cpu_wait_comb, L2CpuWaitComb)
        bool done_i_read;
        bool done_d_read;
        bool done_d_write;
        done_i_read = response_reg[CPU_RESPONSE_INDEX].valid && !response_reg[CPU_RESPONSE_INDEX].data_port &&
            response_reg[CPU_RESPONSE_INDEX].read && i_mem_in.read_in() &&
            (uint32_t)i_mem_in.addr_in() == (uint32_t)response_reg[CPU_RESPONSE_INDEX].addr;
        done_d_read = response_reg[CPU_RESPONSE_INDEX].valid && response_reg[CPU_RESPONSE_INDEX].data_port &&
            response_reg[CPU_RESPONSE_INDEX].read && d_mem_in.read_in() &&
            (uint32_t)d_mem_in.addr_in() == (uint32_t)response_reg[CPU_RESPONSE_INDEX].addr;
        done_d_write = response_reg[CPU_RESPONSE_INDEX].valid && response_reg[CPU_RESPONSE_INDEX].data_port &&
            response_reg[CPU_RESPONSE_INDEX].write && d_mem_in.write_in() &&
            (uint32_t)d_mem_in.addr_in() == (uint32_t)response_reg[CPU_RESPONSE_INDEX].addr;
        cpu_wait_comb = {};
        if (i_mem_in.read_in()) {
            cpu_wait_comb.instruction = !done_i_read;
        }
        if (d_mem_in.write_in()) {
            cpu_wait_comb.data = !done_d_write;
        }
        if (d_mem_in.read_in()) {
            cpu_wait_comb.data = !done_d_read;
        }
        // Advertise initialization and an occupied request pipeline even when
        // the corresponding L1 valid is low; L1 and testbench clients use this
        // registered busy indication before presenting their first request.
        if (state_reg != ST_IDLE && !done_i_read) {
            cpu_wait_comb.instruction = true;
        }
        if (state_reg != ST_IDLE && !(done_d_read || done_d_write)) {
            cpu_wait_comb.data = true;
        }
        return cpu_wait_comb;
    }

};
