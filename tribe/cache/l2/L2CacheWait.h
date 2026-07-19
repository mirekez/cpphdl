#pragma once

#include "L2CacheTagData.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1, size_t CPU_PORTS = 1>
// Generates CPU-side wait signals from the active state and completed response ownership.
class L2CacheWait : public L2CacheTagData<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>
{
protected:
    using Base = L2CacheTagData<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>;

public:
    using Base::i_mem_in;
    using Base::d_mem_in;

protected:
    using Base::CPU_RESPONSE_BASE;
    using Base::response_reg;
    using Base::state_reg;

    // Stores each CPU pair's instruction/data wait result for the final port bindings.
    L2CpuWaitComb cpu_wait_comb[CPU_PORTS];

    // Rebuild every CPU pair's waits from its registered response identity for _assign().
    L2CpuWaitComb (&cpu_wait_comb_func())[CPU_PORTS]
    {
        uint32_t index;
        bool done_i_read;
        bool done_d_read;
        bool done_d_write;
        for (index = 0; index < CPU_PORTS; ++index) {
            done_i_read = response_reg[CPU_RESPONSE_BASE + index].valid &&
                !response_reg[CPU_RESPONSE_BASE + index].data_port &&
                response_reg[CPU_RESPONSE_BASE + index].read && i_mem_in[index].read_in() &&
                (uint32_t)i_mem_in[index].addr_in() == (uint32_t)response_reg[CPU_RESPONSE_BASE + index].addr;
            done_d_read = response_reg[CPU_RESPONSE_BASE + index].valid &&
                response_reg[CPU_RESPONSE_BASE + index].data_port &&
                response_reg[CPU_RESPONSE_BASE + index].read && d_mem_in[index].read_in() &&
                (uint32_t)d_mem_in[index].addr_in() == (uint32_t)response_reg[CPU_RESPONSE_BASE + index].addr;
            done_d_write = response_reg[CPU_RESPONSE_BASE + index].valid &&
                response_reg[CPU_RESPONSE_BASE + index].data_port &&
                response_reg[CPU_RESPONSE_BASE + index].write && d_mem_in[index].write_in() &&
                (uint32_t)d_mem_in[index].addr_in() == (uint32_t)response_reg[CPU_RESPONSE_BASE + index].addr;
            cpu_wait_comb[index] = {};
            if (i_mem_in[index].read_in()) {
                cpu_wait_comb[index].instruction = !done_i_read;
            }
            if (d_mem_in[index].write_in()) {
                cpu_wait_comb[index].data = !done_d_write;
            }
            if (d_mem_in[index].read_in()) {
                cpu_wait_comb[index].data = !done_d_read;
            }
            if (state_reg != ST_IDLE && !done_i_read) {
                cpu_wait_comb[index].instruction = true;
            }
            if (state_reg != ST_IDLE && !(done_d_read || done_d_write)) {
                cpu_wait_comb[index].data = true;
            }
        }
        return cpu_wait_comb;
    }

};
