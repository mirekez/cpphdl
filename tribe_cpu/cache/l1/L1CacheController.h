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
    using Base::invalidate_line_in;
    using Base::invalidate_addr_in;
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

protected:
    // Exposed to derived layer tests that verify peer-invalidation generations.
    using Base::tag_epoch_reg;
    using Base::tag_set_epoch_reg;

private:
    using Base::REFILL_BEATS;
    using Base::SETS;
    using Base::TAG_BITS;
    using Base::cpu_response_comb_func;
    using Base::direct_data_comb_func;
    using Base::even_ram;
    using Base::epoch_wrap_pending_reg;
    using Base::input_request_comb_func;
    using Base::init_set_reg;
    using Base::response_reg;
    using Base::lookup_comb_func;
    using Base::lookup_data_comb_func;
    using Base::lookup_reg;
    using Base::selected_line_comb_func;
    using Base::selected_line_reg;
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
    using Base::tag_ram;
    using Base::tag_entries_reg;
    using Base::victim_reg;

public:
    // Bind grouped combinational decisions to public ports and physical RAM interfaces once.
    void _assign()
    {
        uint32_t i;
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
        mem_out.cache_disable_in = _ASSIGN_COMB(mem_driver_comb_func().cache_disable);

        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = _ASSIGN((state_reg == L1_ST_REFILL ||
                (state_reg == L1_ST_LOOKUP && !input_request_comb_func().issue)) ?
                (uint32_t)request_geometry_comb_func().set :
                (uint32_t)input_request_comb_func().set);
            even_ram[i].data_in = _ASSIGN(refill_lines_comb_func().even);
            even_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_REFILL && req_reg.read &&
                req_reg.cacheable && !mem_out.wait_out() &&
                refill_reg.beat == REFILL_BEATS - 1 && victim_reg == i);
            even_ram[i].rd_in = _ASSIGN(input_request_comb_func().issue &&
                input_request_comb_func().cacheable);
            even_ram[i].id_in = DCACHE * 100 + i * 3;

            odd_ram[i].addr_in = _ASSIGN((state_reg == L1_ST_REFILL ||
                (state_reg == L1_ST_LOOKUP && !input_request_comb_func().issue)) ?
                (uint32_t)request_geometry_comb_func().set :
                (uint32_t)input_request_comb_func().set);
            odd_ram[i].data_in = _ASSIGN(refill_lines_comb_func().odd);
            odd_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_REFILL && req_reg.read &&
                req_reg.cacheable && !mem_out.wait_out() &&
                refill_reg.beat == REFILL_BEATS - 1 && victim_reg == i);
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
                refill_tag_comb_func() : logic<TAG_BITS + 10>(0));
            tag_ram[i].wr_in = _ASSIGN_I(state_reg == L1_ST_INIT ||
                (state_reg == L1_ST_REFILL && req_reg.read && req_reg.cacheable &&
                !mem_out.wait_out() && refill_reg.beat == REFILL_BEATS - 1 &&
                victim_reg == i) || write_in());
            tag_ram[i].rd_in = _ASSIGN(input_request_comb_func().issue &&
                input_request_comb_func().cacheable);
            tag_ram[i].id_in = DCACHE * 100 + i * 3 + 2;
        }
    }

    // Advance request, lookup, refill, held-response, invalidate, and flush sequencing.
    void _work(bool reset)
    {
        uint32_t i;
        L1InputRequestComb input_request;
        L1LookupComb lookup;
        L1RefillLinesComb refill_lines;
        uint32_t invalidate_set;
        bool invalidate_conflict;
        bool invalidate_epoch_wrap;
        input_request = input_request_comb_func();
        lookup = lookup_comb_func();
        refill_lines = refill_lines_comb_func();
        invalidate_set = ((uint32_t)invalidate_addr_in() / CACHE_LINE_SIZE) % SETS;
        invalidate_conflict = invalidate_line_in() && req_reg.read &&
            ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) ==
                ((uint32_t)invalidate_addr_in() & ~(uint32_t)(CACHE_LINE_SIZE - 1));
        invalidate_epoch_wrap = invalidate_line_in() &&
            tag_set_epoch_reg[invalidate_set] == 0xffu;

        // Initialize refill bookkeeping solely from registered cache state.
        // It is harmless when a higher-priority invalidate or branch flush
        // aborts this lookup, and keeping it outside that priority mux prevents
        // live branch resolution from driving the refill counters' reset pins.
        if (state_reg == L1_ST_LOOKUP && req_reg.read) {
            refill_reg._next.beat = 0;
            refill_reg._next.req_data_valid = false;
        }

        if (invalidate_line_in()) {
            // A per-set generation counter invalidates peer data without taking the
            // single-port tag RAM away from an unrelated local lookup/refill.
            // On wrap the initialization walk physically invalidates every tag
            // before lookups resume, so only the addressed set counter needs to
            // advance. Avoiding a whole-array clear also removes one epoch bit's
            // high-fanout path to every epoch register reset pin.
            // Decode the target explicitly so synthesis gives each epoch byte
            // a local enable/add path. A dynamic packed-array update otherwise
            // becomes a cross-coupled mux between every set and dominates the
            // routed cache timing even though only one byte changes.
            for (i = 0; i < SETS; ++i) {
                if (invalidate_set == i) {
                    tag_set_epoch_reg._next[i] = tag_set_epoch_reg[i] + 1;
                }
            }
            if (invalidate_epoch_wrap) {
                // Toggle the global epoch at the wrap edge so every old tag is
                // invalid immediately. The following registered pending bit
                // starts the physical clear without putting the indexed epoch
                // lookup on every controller register's enable/reset cone.
                tag_epoch_reg._next = !tag_epoch_reg;
                epoch_wrap_pending_reg._next = true;
            }
        }

        if (epoch_wrap_pending_reg) {
            // Reuse the initialization walk to physically clear every tag before
            // the wrapped generation value can match an old resident entry.
            req_reg._next.read = false;
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            init_set_reg._next = 0;
            state_reg._next = L1_ST_INIT;
            epoch_wrap_pending_reg._next = false;
        }
        else if (invalidate_conflict) {
            // Only abort a request for the snooped line. The set generation also
            // invalidates unrelated resident lines, but aborting unrelated
            // in-flight refills lets a store stream starve another core forever.
            req_reg._next.read = false;
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            state_reg._next = L1_ST_IDLE;
        }
        else if (invalidate_in()) {
            req_reg._next.read = false;
            response_reg._next.valid = false;
            refill_reg._next.req_data_valid = false;
            init_set_reg._next = 0;
            state_reg._next = L1_ST_INIT;
        }
        else if (flush_in()) {
            // addr_in is the old PC until the redirecting clock edge.  Drop
            // the wrong-path request and let IDLE accept the new registered PC
            // on the next cycle instead of entering LOOKUP with no matching
            // synchronous RAM read result.
            req_reg._next.read = false;
            req_reg._next.cacheable = false;
            response_reg._next.valid = false;
            // Refill bookkeeping is ignored in IDLE and is initialized before
            // the next refill.  Leaving it untouched keeps the live branch
            // decision off the refill counter/reset cone.
            state_reg._next = L1_ST_IDLE;
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
            if (input_request.start) {
                req_reg._next.addr = addr_in();
                req_reg._next.read = true;
                req_reg._next.cacheable = input_request.cacheable;
                req_reg._next.cache_disable = cache_disable_in();
                state_reg._next = L1_ST_LOOKUP;
            }
        }
        else if (state_reg == L1_ST_LOOKUP && req_reg.read) {
            for (i = 0; i < WAYS; ++i) {
                tag_entries_reg._next[i] = tag_ram[i].q_out();
            }
            state_reg._next = L1_ST_COMPARE;
        }
        else if (state_reg == L1_ST_COMPARE && req_reg.read) {
            lookup_reg._next.hit = lookup.hit;
            lookup_reg._next.way = lookup.way;
            state_reg._next = L1_ST_SELECT;
        }
        else if (state_reg == L1_ST_SELECT && req_reg.read) {
            if (lookup_reg.hit) {
                selected_line_reg._next = selected_line_comb_func();
                state_reg._next = L1_ST_ASSEMBLE;
            }
            else {
                // Each word in both split images is overwritten by accepted
                // refill beats before installation.  Avoid clearing the wide
                // accumulators from the tag-hit/miss decision in this cycle.
                state_reg._next = L1_ST_REFILL;
            }
        }
        else if (state_reg == L1_ST_ASSEMBLE && req_reg.read) {
            response_reg._next.addr = req_reg.addr;
            response_reg._next.data = lookup_data_comb_func();
            response_reg._next.valid = true;
            state_reg._next = L1_ST_DONE;
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
                        selected_line_reg._next.even = refill_lines.even;
                        selected_line_reg._next.odd = refill_lines.odd;
                        selected_line_reg._next.addr = (uint32_t)req_reg.addr &
                            ~(uint32_t)(CACHE_LINE_SIZE - 1);
                        selected_line_reg._next.valid = true;
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

        if (write_in()) {
            response_reg._next.valid = false;
            selected_line_reg._next.valid = false;
        }
        if (invalidate_in() || epoch_wrap_pending_reg) {
            selected_line_reg._next.valid = false;
        }
        if (invalidate_line_in() && selected_line_reg.valid &&
            (((uint32_t)selected_line_reg.addr / CACHE_LINE_SIZE) % SETS) == invalidate_set) {
            selected_line_reg._next.valid = false;
        }
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._work(reset);
            odd_ram[i]._work(reset);
            tag_ram[i]._work(reset);
        }
        if (reset) {
            state_reg.clr();
            req_reg.clr();
            tag_epoch_reg.clr();
            epoch_wrap_pending_reg.clr();
            tag_set_epoch_reg.clr();
            refill_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            response_reg.clr();
            lookup_reg.clr();
            selected_line_reg.clr();
            tag_entries_reg.clr();
            state_reg._next = L1_ST_INIT;
        }
    }

    // Commit checkpointed architectural cache state and transient refill response state.
    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        uint32_t i;
        state_reg.strobe(checkpoint_fd);
        req_reg.strobe(checkpoint_fd);
        tag_epoch_reg.strobe(checkpoint_fd);
        epoch_wrap_pending_reg.strobe(checkpoint_fd);
        tag_set_epoch_reg.strobe(checkpoint_fd);
        refill_reg.strobe(checkpoint_fd);
        victim_reg.strobe(checkpoint_fd);
        init_set_reg.strobe(checkpoint_fd);
        response_reg.strobe(checkpoint_fd);
        lookup_reg.strobe(checkpoint_fd);
        selected_line_reg.strobe(checkpoint_fd);
        tag_entries_reg.strobe(checkpoint_fd);
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe(checkpoint_fd);
            odd_ram[i]._strobe(checkpoint_fd);
            tag_ram[i]._strobe(checkpoint_fd);
        }
    }
};
