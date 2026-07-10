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
    using Base::state_reg;
    using Base::req_reg;
    using Base::active_request_comb_func;

    // Resolve both CPU wait outputs from one ownership snapshot so D-side
    // priority and completion release cannot be evaluated inconsistently.
    _LAZY_COMB(cpu_wait_comb, L2CpuWaitComb)
        bool done_i_read;
        bool done_d_read;
        bool done_d_write;
        done_i_read = state_reg == ST_DONE && !req_reg.from_slave && !req_reg.port && req_reg.read &&
            i_mem_in.read_in() && (uint32_t)i_mem_in.addr_in() == (uint32_t)req_reg.addr;
        done_d_read = state_reg == ST_DONE && !req_reg.from_slave && req_reg.port && req_reg.read &&
            d_mem_in.read_in() && (uint32_t)d_mem_in.addr_in() == (uint32_t)req_reg.addr;
        done_d_write = state_reg == ST_DONE && !req_reg.from_slave && req_reg.port && req_reg.write &&
            d_mem_in.write_in() && (uint32_t)d_mem_in.addr_in() == (uint32_t)req_reg.addr;
        cpu_wait_comb = {};
        if (i_mem_in.read_in()) {
            cpu_wait_comb.instruction = true;
            // Only release the wait for the live I-side request that owns this completed response.
            if (done_i_read) {
                cpu_wait_comb.instruction = false;
            }
        }
        if (state_reg != ST_IDLE &&
            !done_i_read) {
            cpu_wait_comb.instruction = true;
        }
        if (d_mem_in.read_in() || d_mem_in.write_in()) {
            cpu_wait_comb.instruction = true;
        }
        if (active_request_comb_func().request.from_slave &&
            !done_i_read) {
            cpu_wait_comb.instruction = true;
        }
        if (d_mem_in.write_in()) {
            cpu_wait_comb.data = true;
            // Do not let a previous completed D-side transaction acknowledge a new write.
            if (done_d_write) {
                cpu_wait_comb.data = false;
            }
        }
        if (d_mem_in.read_in()) {
            cpu_wait_comb.data = true;
            // Do not let a previous completed D-side transaction acknowledge a new read.
            if (done_d_read) {
                cpu_wait_comb.data = false;
            }
        }
        if (state_reg != ST_IDLE &&
            !(done_d_read || done_d_write)) {
            cpu_wait_comb.data = true;
        }
        if (active_request_comb_func().request.from_slave &&
            !(done_d_read || done_d_write)) {
            cpu_wait_comb.data = true;
        }
        return cpu_wait_comb;
    }

};
