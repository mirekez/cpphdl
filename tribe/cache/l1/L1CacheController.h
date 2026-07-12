#pragma once

#include "L1CacheResponse.h"

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Wires L1 ports/RAMs and advances the FSM while datapath decisions remain in lower layers.
class L1Cache : public L1CacheResponse<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS,
    DCACHE, ADDR_BITS, PORT_BITWIDTH>
{
protected:
    using Base = L1CacheResponse<TOTAL_CACHE_SIZE, CACHE_LINE_SIZE, WAYS, DCACHE,
        ADDR_BITS, PORT_BITWIDTH>;
public:
    using Base::addr_in;
    using Base::busy_out;
    using Base::cache_disable_in;
    using Base::debugen_in;
    using Base::flush_in;
    using Base::invalidate_in;
    using Base::mem_out;
    using Base::perf_out;
    using Base::read_addr_out;
    using Base::read_data_out;
    using Base::read_in;
    using Base::read_valid_out;
    using Base::stall_in;
    using Base::write_data_in;
    using Base::write_in;
    using Base::write_mask_in;

private:
    using Base::REFILL_BEATS;
    using Base::SETS;
    using Base::TAG_BITS;
    using Base::cpu_response_comb_func;
    using Base::direct_data_comb_func;
    using Base::even_ram;
    using Base::input_request_comb_func;
    using Base::init_set_reg;
    using Base::response_reg;
    using Base::lookup_comb_func;
    using Base::mem_driver_comb_func;
    using Base::odd_ram;
    using Base::perf_comb_func;
    using Base::refill_reg;
    using Base::refill_data_comb_func;
    using Base::refill_lines_comb_func;
    using Base::refill_tag_comb_func;
    using Base::req_reg;
    using Base::request_geometry_comb_func;
    using Base::state_reg;
    using Base::tag_epoch_reg;
    using Base::tag_ram;
    using Base::victim_reg;

public:
    // Bind grouped combinational decisions to public ports and physical RAM interfaces once.
    void _assign()
    {
        size_t i;
        read_data_out = _ASSIGN_COMB(cpu_response_comb_func().data);
        read_addr_out = _ASSIGN_COMB(cpu_response_comb_func().addr);
        read_valid_out = _ASSIGN_COMB(cpu_response_comb_func().valid);
        busy_out = _ASSIGN_COMB(cpu_response_comb_func().busy);
        perf_out = _ASSIGN_COMB(perf_comb_func());

        mem_out.read_in = _ASSIGN_COMB(mem_driver_comb_func().read);
        mem_out.write_in = _ASSIGN_COMB(mem_driver_comb_func().write);
        mem_out.addr_in = _ASSIGN_COMB(mem_driver_comb_func().addr);
        mem_out.write_data_in = _ASSIGN_COMB(mem_driver_comb_func().write_data);
        mem_out.write_mask_in = _ASSIGN_COMB(mem_driver_comb_func().write_mask);

        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = _ASSIGN((state_reg == L1_ST_REFILL ||
                (state_reg == L1_ST_LOOKUP && !input_request_comb_func().issue)) ?
                (uint32_t)request_geometry_comb_func().set :
                (uint32_t)input_request_comb_func().set);
            even_ram[i].data_in = _ASSIGN(refill_lines_comb_func().even);
            even_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_REFILL && req_reg.read &&
                req_reg.cacheable && refill_reg.beat == REFILL_BEATS - 1 && victim_reg == i);
            even_ram[i].rd_in = _ASSIGN(input_request_comb_func().issue &&
                input_request_comb_func().cacheable);
            even_ram[i].id_in = DCACHE * 100 + i * 3;

            odd_ram[i].addr_in = _ASSIGN((state_reg == L1_ST_REFILL ||
                (state_reg == L1_ST_LOOKUP && !input_request_comb_func().issue)) ?
                (uint32_t)request_geometry_comb_func().set :
                (uint32_t)input_request_comb_func().set);
            odd_ram[i].data_in = _ASSIGN(refill_lines_comb_func().odd);
            odd_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_REFILL && req_reg.read &&
                req_reg.cacheable && refill_reg.beat == REFILL_BEATS - 1 && victim_reg == i);
            odd_ram[i].rd_in = _ASSIGN(input_request_comb_func().issue &&
                input_request_comb_func().cacheable);
            odd_ram[i].id_in = DCACHE * 100 + i * 3 + 1;

            tag_ram[i].addr_in = _ASSIGN(state_reg == L1_ST_INIT ? (uint32_t)init_set_reg :
                (write_in() ? (uint32_t)input_request_comb_func().set :
                ((state_reg == L1_ST_REFILL ||
                (state_reg == L1_ST_LOOKUP && !input_request_comb_func().issue)) ?
                (uint32_t)request_geometry_comb_func().set :
                (uint32_t)input_request_comb_func().set)));
            tag_ram[i].data_in = _ASSIGN(state_reg == L1_ST_REFILL ?
                refill_tag_comb_func() : logic<TAG_BITS + 2>(0));
            tag_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_INIT ||
                (state_reg == L1_ST_REFILL && req_reg.read && req_reg.cacheable &&
                refill_reg.beat == REFILL_BEATS - 1 && victim_reg == i) || write_in());
            tag_ram[i].rd_in = _ASSIGN(input_request_comb_func().issue &&
                input_request_comb_func().cacheable);
            tag_ram[i].id_in = DCACHE * 100 + i * 3 + 2;
        }
    }

    // Advance request, lookup, refill, held-response, invalidate, and flush sequencing.
    void _work(bool reset)
    {
        size_t i;
        L1InputRequestComb input_request;
        L1LookupComb lookup;
        L1RefillLinesComb refill_lines;
        input_request = input_request_comb_func();
        lookup = lookup_comb_func();
        refill_lines = refill_lines_comb_func();

        if (invalidate_in()) {
            req_reg._next.read = false;
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            tag_epoch_reg._next = !tag_epoch_reg;
            state_reg._next = L1_ST_IDLE;
        }
        else if (flush_in()) {
            req_reg._next.addr = addr_in();
            req_reg._next.read = read_in();
            req_reg._next.cacheable = input_request.cacheable;
            req_reg._next.cache_disable = cache_disable_in();
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            state_reg._next = read_in() ? L1_ST_LOOKUP : L1_ST_IDLE;
        }
        else if (state_reg == L1_ST_INIT) {
            req_reg._next.read = false;
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            if (init_set_reg == SETS - 1) state_reg._next = L1_ST_IDLE;
            else init_set_reg._next = init_set_reg + 1;
        }
        else if (state_reg == L1_ST_IDLE) {
            response_reg._next.valid = false;
            if (read_in() && !stall_in()) {
                req_reg._next.addr = addr_in();
                req_reg._next.read = true;
                req_reg._next.cacheable = input_request.cacheable;
                req_reg._next.cache_disable = cache_disable_in();
                state_reg._next = L1_ST_LOOKUP;
            }
        }
        else if (state_reg == L1_ST_LOOKUP && req_reg.read) {
            if (lookup.hit) {
                if (stall_in()) {
                    response_reg._next.addr = req_reg.addr;
                    response_reg._next.data = lookup.data;
                    response_reg._next.valid = true;
                    state_reg._next = L1_ST_DONE;
                }
                else if (input_request.start) {
                    req_reg._next.addr = addr_in();
                    req_reg._next.cacheable = input_request.cacheable;
                    req_reg._next.cache_disable = cache_disable_in();
                    response_reg._next.valid = false;
                    state_reg._next = L1_ST_LOOKUP;
                }
                else {
                    req_reg._next.read = false;
                    req_reg._next.cacheable = false;
                    response_reg._next.valid = false;
                    state_reg._next = L1_ST_IDLE;
                }
            }
            else {
                refill_reg._next.beat = 0;
                refill_reg._next.even_line = 0;
                refill_reg._next.odd_line = 0;
                refill_reg._next.req_data_valid = false;
                state_reg._next = L1_ST_REFILL;
            }
        }
        else if (state_reg == L1_ST_REFILL && req_reg.read) {
            if (!mem_out.wait_out()) {
                if (req_reg.cacheable) {
                    refill_reg._next.even_line = refill_lines.even;
                    refill_reg._next.odd_line = refill_lines.odd;
                    if (refill_reg.beat == request_geometry_comb_func().refill_beat &&
                        (((uint32_t)req_reg.addr & 3u) == 0)) {
                        refill_reg._next.req_data = direct_data_comb_func();
                        refill_reg._next.req_data_valid = true;
                    }
                    if (refill_reg.beat == REFILL_BEATS - 1) {
                        response_reg._next.addr = req_reg.addr;
                        response_reg._next.data =
                            (refill_reg.beat == request_geometry_comb_func().refill_beat) ?
                            direct_data_comb_func() :
                            (refill_reg.req_data_valid ? (uint32_t)refill_reg.req_data :
                            refill_data_comb_func());
                        response_reg._next.valid = true;
                        refill_reg._next.req_data_valid = false;
                        victim_reg._next = victim_reg == WAYS - 1 ? 0 : victim_reg + 1;
                        state_reg._next = L1_ST_DONE;
                    }
                    else refill_reg._next.beat = refill_reg.beat + 1;
                }
                else {
                    response_reg._next.addr = req_reg.addr;
                    response_reg._next.data = direct_data_comb_func();
                    response_reg._next.valid = true;
                    state_reg._next = L1_ST_DONE;
                }
            }
        }
        else if (state_reg == L1_ST_DONE && !stall_in()) {
            response_reg._next.valid = false;
            if (input_request.start) {
                req_reg._next.addr = addr_in();
                req_reg._next.read = true;
                req_reg._next.cacheable = input_request.cacheable;
                req_reg._next.cache_disable = cache_disable_in();
                state_reg._next = L1_ST_LOOKUP;
            }
            else {
                req_reg._next.read = false;
                req_reg._next.cacheable = false;
                state_reg._next = L1_ST_IDLE;
            }
        }

        if (write_in()) response_reg._next.valid = false;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._work(reset);
            odd_ram[i]._work(reset);
            tag_ram[i]._work(reset);
        }
        if (reset) {
            state_reg.clr();
            req_reg.clr();
            tag_epoch_reg.clr();
            refill_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            response_reg.clr();
            state_reg._next = L1_ST_INIT;
        }
    }

    // Commit checkpointed architectural cache state and transient refill response state.
    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        size_t i;
        state_reg.strobe(checkpoint_fd);
        req_reg.strobe(checkpoint_fd);
        tag_epoch_reg.strobe(checkpoint_fd);
        refill_reg.strobe(checkpoint_fd);
        victim_reg.strobe(checkpoint_fd);
        init_set_reg.strobe(checkpoint_fd);
        response_reg.strobe(checkpoint_fd);
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe(checkpoint_fd);
            odd_ram[i]._strobe(checkpoint_fd);
            tag_ram[i]._strobe(checkpoint_fd);
        }
    }
};
