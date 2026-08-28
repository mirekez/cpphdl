#pragma once

#include "L2CacheWait.h"

#define L2_FOR_EACH_DATA_BANK(M) \
    M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) \
    M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15) \
    M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23) \
    M(24) M(25) M(26) M(27) M(28) M(29) M(30) M(31)

#define L2_FOR_EACH_TAG_BANK(M) M(0) M(1) M(2) M(3)

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1, size_t CPU_PORTS = 1>
// Final port-compatible L2 cache controller: wires RAM/AXI ports, advances FSM state, and checkpoints registers.
class L2Cache : public L2CacheWait<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>
{
protected:
    using Base = L2CacheWait<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>;
public:
    using Base::i_mem_in;
    using Base::d_mem_in;
    using Base::axi_in;
    using Base::axi_out;
    using Base::dma_line_valid_in;
    using Base::dma_line_addr_in;
    using Base::dma_line_data_in;
    using Base::dma_line_keep_in;
    using Base::dma_line_ready_out;
    using Base::debugen_in;

private:
    using Base::LINE_WORDS;
    using Base::PORT_WORDS;
    using Base::LINE_BEATS;
    using Base::SETS;
    using Base::DATA_BANKS;
    using Base::CPU_RESPONSE_BASE;
    using Base::RESPONSE_SLOTS;
    using Base::data_ram;
    using Base::tag_ram;
    using Base::lookup_data_reg;
    using Base::lookup_tag_reg;
    using Base::lookup_hit_reg;
    using Base::lookup_evict_reg;
    using Base::lookup_write_pair_reg;
    using Base::state_reg;
    using Base::req_reg;
    using Base::request_pipe_reg;
    using Base::request_pipe_valid_reg;
    using Base::cpu_rr_reg;
    using Base::victim_reg;
    using Base::fill_way_reg;
    using Base::init_set_reg;
    using Base::response_reg;
    using Base::cross_low_reg;
    using Base::cross_high_reg;
    using Base::refill_data_reg;
    using Base::fill_beat_reg;
    using Base::evict_beat_reg;
    using Base::evict_tag_reg;
    using Base::evict_line_reg;
    using Base::slave_aw_reg;
    using Base::slave_aw_seen_reg;
    using Base::slave_ar_seen_reg;
    using Base::slave_aw_novelty_reg;
    using Base::slave_ar_novelty_reg;
    using Base::slave_request_novelty_comb_func;
    using Base::active_request_comb_func;
    using Base::request_geometry_comb_func;
    using Base::axi_route_comb_func;
    using Base::axi_out_driver_comb_func;
    using Base::axi_out_selected_resp_comb_func;
    using Base::evict_candidate_comb_func;
    using Base::evict_line_comb_func;
    using Base::req_uncached_region_comb_func;
    using Base::hit_lookup_comb_func;
    using Base::hit_write_pair_comb_func;
    using Base::fill_write_pair_comb_func;
    using Base::cross_read_data_comb_func;
    using Base::tag_write_data_comb_func;
    using Base::read_data_comb_func;
    using Base::cpu_wait_comb_func;

    Axi4Responder<4,256> axi_in_comb[MEM_PORTS];
    Axi4Driver<32,4,256> axi_out_comb[MEM_PORTS];
#ifndef SYNTHESIS
    long prev_axi_in_comb_clock = -1;
    long prev_axi_out_comb_clock = -1;
#endif

    // Build all AXI slave-side responder bundles and return the array so comb users depend on the driven values.
    Axi4Responder<4,256> (&axi_in_comb_func())[MEM_PORTS]
    {
        uint32_t index;
        L2ActiveRequestComb active_request;

#ifndef SYNTHESIS
        if (prev_axi_in_comb_clock == _system_clock) {
            return axi_in_comb;
        }
        prev_axi_in_comb_clock = _system_clock;
#endif
        active_request = active_request_comb_func();
        for (index = 0; index < MEM_PORTS; ++index) {
            axi_in_comb[index].aw.ready = state_reg == ST_IDLE && !request_pipe_valid_reg &&
                !slave_aw_reg[index].valid &&
                slave_request_novelty_comb_func().aw[index] &&
                (!response_reg[index].b.valid || axi_in[index].bready_in()) && axi_in[index].awvalid_in();
            axi_in_comb[index].w.ready = state_reg == ST_IDLE && !request_pipe_valid_reg &&
                active_request.request.from_slave && active_request.request.write &&
                active_request.request.slave_index == index;
            axi_in_comb[index].b = response_reg[index].b;
            axi_in_comb[index].ar.ready = state_reg == ST_IDLE && !request_pipe_valid_reg &&
                slave_request_novelty_comb_func().ar[index] &&
                active_request.request.from_slave && active_request.request.read &&
                active_request.request.slave_index == index;
            axi_in_comb[index].r = response_reg[index].r;
        }
        return axi_in_comb;
    }

    // Build all AXI master-side driver bundles and return the array so comb users depend on the driven values.
    Axi4Driver<32,4,256> (&axi_out_comb_func())[MEM_PORTS]
    {
        uint32_t index;

#ifndef SYNTHESIS
        if (prev_axi_out_comb_clock == _system_clock) {
            return axi_out_comb;
        }
        prev_axi_out_comb_clock = _system_clock;
#endif
        for (index = 0; index < MEM_PORTS; ++index) {
            axi_out_comb[index].aw.valid = axi_out_driver_comb_func().aw.valid && (uint32_t)axi_route_comb_func().aw_sel == index;
            axi_out_comb[index].aw.addr = axi_out_driver_comb_func().aw.addr;
            axi_out_comb[index].aw.id = axi_out_driver_comb_func().aw.id;
            axi_out_comb[index].w.valid = axi_out_driver_comb_func().w.valid && (uint32_t)axi_route_comb_func().aw_sel == index;
            axi_out_comb[index].w.data = axi_out_driver_comb_func().w.data;
            axi_out_comb[index].w.strb = axi_out_driver_comb_func().w.strb;
            axi_out_comb[index].w.last = axi_out_driver_comb_func().w.last && (uint32_t)axi_route_comb_func().aw_sel == index;
            axi_out_comb[index].b.ready = axi_out_driver_comb_func().b.ready && (uint32_t)axi_route_comb_func().aw_sel == index;
            axi_out_comb[index].ar.valid = axi_out_driver_comb_func().ar.valid && (uint32_t)axi_route_comb_func().ar_sel == index;
            axi_out_comb[index].ar.addr = axi_out_driver_comb_func().ar.addr;
            axi_out_comb[index].ar.id = axi_out_driver_comb_func().ar.id;
            axi_out_comb[index].r.ready = axi_out_driver_comb_func().r.ready && (uint32_t)axi_route_comb_func().ar_sel == index;
        }
        return axi_out_comb;
    }

    // AXI slave read responses use the exact 3-bit bookkeeping index so generated SV does not widen an eight-slot array selector.
    void send_slave_read_response(u<3> index, u<4> id, logic<256> data)
    {
        response_reg._next[index].r.valid = true;
        response_reg._next[index].r.id = id;
        response_reg._next[index].r.data = data;
        response_reg._next[index].r.last = true;
    }

    // AXI slave write responses use the same exact-width index after the accepted write is committed or forwarded.
    void send_slave_write_response(u<3> index, u<4> id)
    {
        response_reg._next[index].b.valid = true;
        response_reg._next[index].b.id = id;
    }

    // CPU/L1 completions capture request identity and data together so wait is
    // released only for the request that produced this registered response.
    void send_cpu_response(logic<256> data)
    {
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].valid = true;
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].read = req_reg.read;
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].write = req_reg.write;
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].data_port = req_reg.port;
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].addr = req_reg.addr;
        response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].r.data = data;
    }

    // Produce conventional synchronous-RAM controls outside the clocked FSM
    // task.  The generated controller drives small bank modules, while the
    // bank replacement supplies only the physical inferred-BRAM template.
    _LAZY_COMB(l2_ram_controls_comb, L2RamControlsComb)
        uint32_t bank;
        uint32_t way;
        uint32_t dma_set;
        uint32_t dma_tag;
        uint32_t dma_way;
        uint32_t dma_word;
        uint32_t dma_byte;
        bool dma_line_fire;
        bool bank_write;
        L2RequestGeometryComb request_geometry;
        L2HitLookupComb hit_lookup;
        L2WordPairComb hit_write_pair;
        L2WordPairComb fill_write_pair;
        logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8> tag_data;

        // This fixed-size zero initializes and ties off every literal RAM leaf;
        // the loops below overwrite only this configuration's active banks.
        l2_ram_controls_comb = {};
        dma_word = 0;
        tag_data = 0;
        request_geometry = request_geometry_comb_func();
        hit_lookup = hit_lookup_comb_func();
        if (state_reg == ST_LOOKUP_RESULT || state_reg == ST_CROSS_WRITE_RESULT) {
            hit_lookup = lookup_hit_reg;
        }
        hit_write_pair = hit_write_pair_comb_func();
        fill_write_pair = fill_write_pair_comb_func();
        dma_line_fire = dma_line_valid_in() && dma_line_ready_out();
        dma_set = ((uint32_t)dma_line_addr_in() >> Base::LINE_BITS)
            & (Base::SETS - 1);
        dma_tag = (uint32_t)dma_line_addr_in()
            >> (Base::LINE_BITS + Base::SET_BITS);
        dma_way = WAYS <= 1 ? 0 : dma_tag % WAYS;

        l2_ram_controls_comb.addr = state_reg == ST_INIT ?
            (uint32_t)init_set_reg :
            (dma_line_fire ? dma_set :
                (state_reg == ST_IDLE && request_pipe_valid_reg ?
                    (uint32_t)request_pipe_reg.cache_set :
                    (uint32_t)request_geometry.set));
        l2_ram_controls_comb.read =
            ((state_reg == ST_IDLE && request_pipe_valid_reg &&
                !request_pipe_reg.cross_line_read) ||
             state_reg == ST_READ || state_reg == ST_CROSS_WRITE_READ)
            && !dma_line_fire;

        for (bank = 0; bank < DATA_BANKS; ++bank) {
            bank_write =
                (dma_line_fire && dma_way == (bank / LINE_WORDS)) ||
                (state_reg == ST_AXI_R_WRITE && fill_way_reg == (bank / LINE_WORDS) &&
                    (bank % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                    (bank % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ||
                (state_reg == ST_LOOKUP_RESULT && req_reg.from_slave && req_reg.write && lookup_hit_reg.hit &&
                    lookup_hit_reg.way == (bank / LINE_WORDS) &&
                    (bank % LINE_WORDS) >= (uint32_t)request_geometry.beat * PORT_WORDS &&
                    (bank % LINE_WORDS) < ((uint32_t)request_geometry.beat + 1u) * PORT_WORDS &&
                    req_reg.write_word_mask[(bank % LINE_WORDS) % PORT_WORDS]) ||
                ((state_reg == ST_LOOKUP_RESULT || state_reg == ST_CROSS_WRITE_RESULT) &&
                    req_reg.write && lookup_hit_reg.hit && !req_reg.from_slave &&
                    lookup_hit_reg.way == (bank / LINE_WORDS) &&
                    (request_geometry.word == (bank % LINE_WORDS) ||
                     (((uint32_t)req_reg.addr & 3u) != 0 &&
                        (uint32_t)request_geometry.word + 1 == (bank % LINE_WORDS))));
            l2_ram_controls_comb.data_write[bank] = bank_write;

            if (dma_line_fire) {
                dma_word = (uint32_t)(dma_line_data_in() >>
                    ((bank % LINE_WORDS) * 32));
                for (dma_byte = 0; dma_byte < 4; ++dma_byte) {
                    if (!dma_line_keep_in()[(bank % LINE_WORDS) * 4 + dma_byte]) {
                        dma_word &= ~(0xffu << (dma_byte * 8));
                    }
                }
                l2_ram_controls_comb.data[bank] = dma_word;
            }
            else {
                l2_ram_controls_comb.data[bank] =
                    (state_reg == ST_LOOKUP_RESULT || state_reg == ST_CROSS_WRITE_RESULT) ?
                    (req_reg.from_slave ?
                        (uint32_t)(req_reg.write_beat >> (((bank % PORT_WORDS) * 32))) :
                        ((((uint32_t)req_reg.addr & 3u) != 0 &&
                            (uint32_t)request_geometry.word + 1 == (bank % LINE_WORDS)) ?
                            (uint32_t)lookup_write_pair_reg.next_word :
                            (uint32_t)lookup_write_pair_reg.word)) :
                    ((req_reg.from_slave && req_reg.write &&
                        request_geometry.beat == fill_beat_reg &&
                        (bank % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                        (bank % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ?
                        (req_reg.write_word_mask[(bank % LINE_WORDS) % PORT_WORDS] ?
                            (uint32_t)(req_reg.write_beat >> (((bank % PORT_WORDS) * 32))) :
                            (uint32_t)(refill_data_reg >> ((((bank % LINE_WORDS) % PORT_WORDS) * 32)))) :
                     (req_reg.write && request_geometry.word == (bank % LINE_WORDS)) ?
                        (uint32_t)fill_write_pair.word :
                     (req_reg.write && ((uint32_t)req_reg.addr & 3u) != 0 &&
                        (uint32_t)request_geometry.word + 1 == (bank % LINE_WORDS)) ?
                        (uint32_t)fill_write_pair.next_word :
                        (uint32_t)(refill_data_reg >> ((((bank % LINE_WORDS) % PORT_WORDS) * 32))));
            }
        }

        tag_data = tag_write_data_comb_func();
        if (dma_line_fire) {
            tag_data = ((uint64_t)1 << (Base::TAG_BITS + 1)) |
                ((uint64_t)1 << Base::TAG_BITS) | dma_tag;
        }
        l2_ram_controls_comb.tag_data = (uint32_t)tag_data;
        for (way = 0; way < WAYS; ++way) {
            l2_ram_controls_comb.tag_write[way] =
                (dma_line_fire && dma_way == way) ||
                (state_reg == ST_INIT) ||
                (state_reg == ST_AXI_R_WRITE && fill_beat_reg == LINE_BEATS - 1 &&
                    fill_way_reg == way) ||
                ((state_reg == ST_LOOKUP_RESULT || state_reg == ST_CROSS_WRITE_RESULT) &&
                    req_reg.write && lookup_hit_reg.hit && lookup_hit_reg.way == way);
        }
        return l2_ram_controls_comb;
    }

    u<clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS)> l2_ram_addr_comb;
    u<clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS)>& l2_ram_addr_comb_func()
    {
        l2_ram_addr_comb = l2_ram_controls_comb_func().addr;
        return l2_ram_addr_comb;
    }

    bool l2_ram_read_comb;
    bool& l2_ram_read_comb_func()
    {
        l2_ram_read_comb = l2_ram_controls_comb_func().read;
        return l2_ram_read_comb;
    }

    logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8> l2_tag_data_comb;
    logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8>& l2_tag_data_comb_func()
    {
        l2_tag_data_comb = l2_ram_controls_comb_func().tag_data;
        return l2_tag_data_comb;
    }

public:
    void _assign()
    {
        uint32_t i;

        for (i = 0; i < CPU_PORTS; ++i) {
            this->i_mem_in[i].read_data_out = _ASSIGN_COMB_I(read_data_comb_func()[i]);
            this->i_mem_in[i].wait_out = _ASSIGN_COMB_I(cpu_wait_comb_func()[i].instruction);
            d_mem_in[i].read_data_out = _ASSIGN_COMB_I(read_data_comb_func()[i]);
            d_mem_in[i].wait_out = _ASSIGN_COMB_I(cpu_wait_comb_func()[i].data);
        }

        for (i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM_COMB_INDEXED(axi_in[i], axi_in_comb_func(), i);
            AXI4_DRIVER_FROM_COMB_INDEXED(axi_out[i], axi_out_comb_func(), i);
        }
#define L2_BIND_DATA_BANK(number) \
        data_ram[number].addr_in = _ASSIGN_COMB(l2_ram_addr_comb_func()); \
        data_ram[number].write_in = _ASSIGN(l2_ram_controls_comb_func().data_write[number]); \
        data_ram[number].read_in = _ASSIGN_COMB(l2_ram_read_comb_func()); \
        data_ram[number].write_data_in = _ASSIGN(l2_ram_controls_comb_func().data[number]); \
        data_ram[number].__inst_name = this->__inst_name + "/data_bank" + std::to_string(number); \
        data_ram[number]._assign();
        L2_FOR_EACH_DATA_BANK(L2_BIND_DATA_BANK)
#undef L2_BIND_DATA_BANK

#define L2_BIND_TAG_BANK(number) \
        tag_ram[number].addr_in = _ASSIGN_COMB(l2_ram_addr_comb_func()); \
        tag_ram[number].write_in = _ASSIGN(l2_ram_controls_comb_func().tag_write[number]); \
        tag_ram[number].read_in = _ASSIGN_COMB(l2_ram_read_comb_func()); \
        tag_ram[number].write_data_in = _ASSIGN_COMB(l2_tag_data_comb_func()); \
        tag_ram[number].__inst_name = this->__inst_name + "/tag_bank" + std::to_string(number); \
        tag_ram[number]._assign();
        L2_FOR_EACH_TAG_BANK(L2_BIND_TAG_BANK)
#undef L2_BIND_TAG_BANK
        // Uncached I/O forwarding does not touch the tag/data RAMs.  Permit a
        // line allocation in parallel with those states; MMIO polling would
        // otherwise reduce SmartNIC allocation bandwidth below wire rate.
        dma_line_ready_out = _ASSIGN((uint32_t)state_reg == ST_IDLE
            || (uint32_t)state_reg == ST_READ
            || ((uint32_t)state_reg == ST_LOOKUP
                && (!req_reg.write || req_uncached_region_comb_func()))
            || (uint32_t)state_reg == ST_LOOKUP_CAPTURE
            || (uint32_t)state_reg == ST_LOOKUP_RESULT
            || (uint32_t)state_reg == ST_CROSS_WRITE_CAPTURE
            || (uint32_t)state_reg == ST_CROSS_WRITE_RESULT
            || (uint32_t)state_reg == ST_AXI_AR
            || (uint32_t)state_reg == ST_EVICT_AW
            || (uint32_t)state_reg == ST_EVICT_W
            || (uint32_t)state_reg == ST_EVICT_B
            || (uint32_t)state_reg == ST_IO_AW
            || (uint32_t)state_reg == ST_IO_W
            || (uint32_t)state_reg == ST_IO_B
            || (uint32_t)state_reg == ST_IO_AR
            || (uint32_t)state_reg == ST_IO_R);
    }

    void _work_l2_clock(bool reset)
    {
        uint32_t i;
        uint32_t way;
        bool dma_line_fire;
        uint32_t trace_line;
        bool trace_line_enabled;
        bool trace_req_line;
        bool trace_active_line;
        uint32_t trace_word0;
        uint32_t trace_word1;
        bool active_request_completed;
        // Keep cpphdl local logic declarations after scalar locals: the SV
        // backend emits default construction as an assignment, and Verilator
        // requires all task declarations before the first assignment.
        L2ActiveRequestComb active_request;
        L2RequestGeometryComb request_geometry;
        L2EvictCandidateComb evict_candidate;
        L2HitLookupComb hit_lookup;
        L2WordPairComb hit_write_pair;
        L2WordPairComb fill_write_pair;
        logic<256> completion_data;
        active_request = active_request_comb_func();
        active_request_completed = !active_request.request.from_slave &&
            response_reg[CPU_RESPONSE_BASE + active_request.request.cpu_index].valid &&
            response_reg[CPU_RESPONSE_BASE + active_request.request.cpu_index].data_port == active_request.request.port &&
            response_reg[CPU_RESPONSE_BASE + active_request.request.cpu_index].read == active_request.request.read &&
            response_reg[CPU_RESPONSE_BASE + active_request.request.cpu_index].write == active_request.request.write &&
            response_reg[CPU_RESPONSE_BASE + active_request.request.cpu_index].addr == active_request.request.addr;
        request_geometry = request_geometry_comb_func();
        evict_candidate = evict_candidate_comb_func();
        hit_lookup = hit_lookup_comb_func();
        if (state_reg == ST_LOOKUP_RESULT || state_reg == ST_CROSS_WRITE_RESULT) {
            evict_candidate = lookup_evict_reg;
            hit_lookup = lookup_hit_reg;
        }
        hit_write_pair = hit_write_pair_comb_func();
        fill_write_pair = fill_write_pair_comb_func();
        completion_data = 0;
        trace_line = 0;
        trace_line_enabled = false;
        trace_req_line = false;
        trace_active_line = false;
        if (debugen_in) {
            trace_line_enabled = true;
            trace_line = 0x400u;
            trace_req_line = (((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
            trace_active_line = (((uint32_t)active_request.request.addr &
                ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
        }
#ifndef SYNTHESIS
        static const char* trace_line_env = std::getenv("TRIBE_TRACE_L2_LINE");
        trace_line_enabled = trace_line_env != nullptr;
        trace_line = trace_line_env ? (uint32_t)std::strtoul(trace_line_env, nullptr, 0) & ~(uint32_t)(CACHE_LINE_SIZE - 1) : 0;
        trace_req_line = trace_line_env && (((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
        trace_active_line = trace_line_env && (((uint32_t)active_request.request.addr &
            ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
#endif
        trace_word0 = 0;
        trace_word1 = 0;

        dma_line_fire = dma_line_valid_in() && dma_line_ready_out();
        for (i = 0; i < DATA_BANKS; ++i) {
            data_ram[i]._work_l2_clock(reset);
        }
        for (way = 0; way < WAYS; ++way) {
            tag_ram[way]._work_l2_clock(reset);
        }

        // CPU/L1 has no response-ready input: expose its registered response
        // for exactly this clock, then free the slot for the next completion.
        for (i = 0; i < CPU_PORTS; ++i) {
            response_reg._next[CPU_RESPONSE_BASE + i].valid = false;
        }

        if (state_reg == ST_LOOKUP_CAPTURE || state_reg == ST_CROSS_WRITE_CAPTURE) {
            for (i = 0; i < DATA_BANKS; ++i) {
                lookup_data_reg._next[i] = data_ram[i].read_data_out();
            }
            for (way = 0; way < WAYS; ++way) {
                lookup_tag_reg._next[way] = tag_ram[way].read_data_out();
            }
        }

        for (i = 0; i < MEM_PORTS; ++i) {
            slave_aw_novelty_reg._next[i] = !slave_aw_seen_reg[i].valid ||
                slave_aw_seen_reg[i].addr != axi_in[i].awaddr_in() ||
                slave_aw_seen_reg[i].id != axi_in[i].awid_in();
            slave_ar_novelty_reg._next[i] = !slave_ar_seen_reg[i].valid ||
                slave_ar_seen_reg[i].addr != axi_in[i].araddr_in() ||
                slave_ar_seen_reg[i].id != axi_in[i].arid_in();
            if (!axi_in[i].awvalid_in()) {
                slave_aw_seen_reg._next[i].valid = false;
            }
            if (!axi_in[i].arvalid_in()) {
                slave_ar_seen_reg._next[i].valid = false;
            }
            if (response_reg[i].b.valid && axi_in[i].bready_in()) {
                response_reg._next[i].b.valid = false;
            }
            if (response_reg[i].r.valid && axi_in[i].rready_in()) {
                response_reg._next[i].r.valid = false;
                response_reg._next[i].r.last = false;
            }
            if (state_reg == ST_IDLE && axi_in[i].awvalid_in() && axi_in[i].awready_out()) {
                slave_aw_reg._next[i].valid = true;
                slave_aw_reg._next[i].addr = axi_in[i].awaddr_in();
                slave_aw_reg._next[i].id = axi_in[i].awid_in();
                slave_aw_seen_reg._next[i].valid = true;
                slave_aw_seen_reg._next[i].addr = axi_in[i].awaddr_in();
                slave_aw_seen_reg._next[i].id = axi_in[i].awid_in();
            }
            if (state_reg == ST_IDLE && axi_in[i].arvalid_in() && axi_in[i].arready_out()) {
                slave_ar_seen_reg._next[i].valid = true;
                slave_ar_seen_reg._next[i].addr = axi_in[i].araddr_in();
                slave_ar_seen_reg._next[i].id = axi_in[i].arid_in();
            }
        }

        if (state_reg == ST_INIT) {
            if (init_set_reg == SETS - 1) {
                state_reg._next = ST_IDLE;
            }
            else {
                init_set_reg._next = init_set_reg + 1;
            }
        }
        else if (state_reg == ST_IDLE) {
            if (request_pipe_valid_reg) {
                request_pipe_valid_reg._next = false;
                if (!(response_reg[CPU_RESPONSE_BASE + request_pipe_reg.request.cpu_index].valid &&
                      !request_pipe_reg.request.from_slave &&
                      response_reg[CPU_RESPONSE_BASE + request_pipe_reg.request.cpu_index].data_port == request_pipe_reg.request.port &&
                      response_reg[CPU_RESPONSE_BASE + request_pipe_reg.request.cpu_index].read == request_pipe_reg.request.read &&
                      response_reg[CPU_RESPONSE_BASE + request_pipe_reg.request.cpu_index].write == request_pipe_reg.request.write &&
                      response_reg[CPU_RESPONSE_BASE + request_pipe_reg.request.cpu_index].addr == request_pipe_reg.request.addr)) {
                if (trace_active_line) {
                    std::print("trace-l2 cycle={} cpu={} accept addr={:08x} rd={} wr={} wdata={:08x} mask={:02x} slave={} dport={} victim={}\n",
                        _system_clock, (uint32_t)request_pipe_reg.request.cpu_index,
                        (uint32_t)request_pipe_reg.request.addr,
                        (bool)request_pipe_reg.request.read, (bool)request_pipe_reg.request.write,
                        (uint32_t)request_pipe_reg.request.write_data,
                        (uint32_t)request_pipe_reg.request.write_mask,
                        (bool)request_pipe_reg.request.from_slave,
                        (bool)request_pipe_reg.request.port, (uint32_t)victim_reg);
                }
                req_reg._next = request_pipe_reg.request;
                if (!request_pipe_reg.request.from_slave) {
                    cpu_rr_reg._next = request_pipe_reg.request.cpu_index == CPU_PORTS - 1 ?
                        (uint32_t)0 : (uint32_t)request_pipe_reg.request.cpu_index + 1u;
                }
                // Read the synchronous arrays on this same consume edge. DMA
                // has port priority, so only that collision needs ST_READ as a
                // retry; the normal path keeps its pre-pipeline cycle count.
                state_reg._next = request_pipe_reg.cross_line_read ? ST_CROSS_AR0 :
                    (dma_line_fire ? ST_READ : ST_LOOKUP_CAPTURE);
                }
            }
            // A CPU request is level-held until its completion crosses back to
            // the faster CPU clock.  Do not recapture it on the L2 edge that
            // retires the matching response; otherwise the pipeline consumes
            // the duplicate after the one-cycle response record is gone.
            else if (active_request.valid && !active_request_completed) {
                request_pipe_reg._next = active_request;
                request_pipe_valid_reg._next = true;
                // A slave W handshake has completed in this cycle.  Retire
                // its saved AW immediately; the complete request payload is
                // now retained in request_pipe_reg.
                for (i = 0; i < MEM_PORTS; ++i) {
                    if (active_request.request.from_slave && active_request.request.write &&
                        active_request.request.slave_index == i) {
                        slave_aw_reg._next[i].valid = false;
                    }
                }
            }
        }
        else if (state_reg == ST_READ) {
            // Packet DMA owns the single RAM port on an allocation cycle.
            // Retry the registered lookup read on the next free L2 clock.
            if (!dma_line_fire) state_reg._next = ST_LOOKUP_CAPTURE;
        }
        else if (state_reg == ST_LOOKUP_CAPTURE) {
            state_reg._next = ST_LOOKUP;
        }
        else if (state_reg == ST_LOOKUP) {
            lookup_hit_reg._next = hit_lookup;
            lookup_evict_reg._next = evict_candidate;
            lookup_write_pair_reg._next = hit_write_pair;
            state_reg._next = ST_LOOKUP_RESULT;
        }
        else if (state_reg == ST_LOOKUP_RESULT) {
            if (!request_geometry.addr_in_memory) {
                if (trace_req_line) {
                    std::print("trace-l2 cycle={} lookup-outside addr={:08x} rd={} wr={}\n",
                        _system_clock, (uint32_t)req_reg.addr, (bool)req_reg.read, (bool)req_reg.write);
                }
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            if (req_reg.read) {
                                send_slave_read_response(i, req_reg.slave_id, 0);
                            }
                            if (req_reg.write) {
                                send_slave_write_response(i, req_reg.slave_id);
                            }
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    send_cpu_response(0);
                    state_reg._next = ST_IDLE;
                }
            }
            else if (req_uncached_region_comb_func()) {
                if (trace_req_line) {
                    std::print("trace-l2 cycle={} lookup-uncached addr={:08x} rd={} wr={}\n",
                        _system_clock, (uint32_t)req_reg.addr, (bool)req_reg.read, (bool)req_reg.write);
                }
                state_reg._next = req_reg.read ? ST_IO_AR : ST_IO_AW;
            }
            else if (hit_lookup.hit) {
                if (trace_req_line) {
                    trace_word0 = (uint32_t)hit_lookup.beat;
                    trace_word1 = PORT_WORDS > 1 ? (uint32_t)(hit_lookup.beat >> 32) : 0;
                    std::print("trace-l2 cycle={} lookup-hit addr={:08x} rd={} wr={} way={} word={} hit_word={:08x} beat0={:08x} beat1={:08x} wdata={:08x} mask={:02x}\n",
                        _system_clock, (uint32_t)req_reg.addr, (bool)req_reg.read, (bool)req_reg.write,
                        (uint32_t)hit_lookup.way, (uint32_t)request_geometry.word,
                        (uint32_t)hit_lookup.read_word,
                        trace_word0, trace_word1, (uint32_t)req_reg.write_data, (uint32_t)req_reg.write_mask);
                }
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            if (req_reg.read) {
                                send_slave_read_response(i, req_reg.slave_id, hit_lookup.beat);
                            }
                            if (req_reg.write && !request_geometry.cross_line_write) {
                                send_slave_write_response(i, req_reg.slave_id);
                            }
                        }
                    }
                }
                if (request_geometry.cross_line_write) {
                    // Finish the part of an unaligned store that spills into the first word of the next line.
                    req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                    req_reg._next.write_data = request_geometry.cross_write_data;
                    req_reg._next.write_mask = request_geometry.cross_write_mask;
                    req_reg._next.write_strobe = active_request.request.write_strobe;
                    state_reg._next = ST_CROSS_WRITE_READ;
                }
                else {
                    if (!req_reg.from_slave) {
                        completion_data = 0;
                        if (req_reg.read && request_geometry.cross_beat_read) {
                            completion_data.bits(31, 0) = hit_lookup.read_word;
                        }
                        else if (req_reg.read) {
                            completion_data = hit_lookup.beat;
                        }
                        send_cpu_response(completion_data);
                    }
                    state_reg._next = ST_IDLE;
                }
            }
            else {
                if (trace_req_line) {
                    std::print("trace-l2 cycle={} lookup-miss addr={:08x} rd={} wr={} victim={} evict_valid={} evict_dirty={} evict_tag={:08x}\n",
                        _system_clock, (uint32_t)req_reg.addr, (bool)req_reg.read, (bool)req_reg.write,
                        (uint32_t)victim_reg, (bool)evict_candidate.valid,
                        (bool)evict_candidate.dirty, (uint32_t)evict_candidate.tag);
                }
                fill_way_reg._next = victim_reg;
                fill_beat_reg._next = 0;
                evict_beat_reg._next = 0;
                evict_tag_reg._next = evict_candidate.tag;
                evict_line_reg._next = evict_candidate.line;
                state_reg._next = (!req_reg.from_slave && request_geometry.cross_beat_read) ? ST_CROSS_AR0 :
                    ((evict_candidate.valid && evict_candidate.dirty) ? ST_EVICT_AW : ST_AXI_AR);
            }
        }
        else if (state_reg == ST_CROSS_WRITE_READ) {
            if (!dma_line_fire) state_reg._next = ST_CROSS_WRITE_CAPTURE;
        }
        else if (state_reg == ST_CROSS_WRITE_CAPTURE) {
            state_reg._next = ST_CROSS_WRITE_LOOKUP;
        }
        else if (state_reg == ST_CROSS_WRITE_LOOKUP) {
            lookup_hit_reg._next = hit_lookup;
            lookup_evict_reg._next = evict_candidate;
            lookup_write_pair_reg._next = hit_write_pair;
            state_reg._next = ST_CROSS_WRITE_RESULT;
        }
        else if (state_reg == ST_CROSS_WRITE_RESULT) {
            if (!request_geometry.addr_in_memory) {
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            send_slave_write_response(i, req_reg.slave_id);
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    send_cpu_response(0);
                    state_reg._next = ST_IDLE;
                }
            }
            else if (hit_lookup.hit) {
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            send_slave_write_response(i, req_reg.slave_id);
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    send_cpu_response(0);
                    state_reg._next = ST_IDLE;
                }
            }
            else {
                fill_way_reg._next = victim_reg;
                fill_beat_reg._next = 0;
                evict_beat_reg._next = 0;
                evict_tag_reg._next = evict_candidate.tag;
                evict_line_reg._next = evict_candidate.line;
                state_reg._next = (evict_candidate.valid && evict_candidate.dirty) ? ST_EVICT_AW : ST_AXI_AR;
            }
        }
        else if (state_reg == ST_EVICT_AW) {
            if (axi_out_driver_comb_func().aw.valid && axi_out_selected_resp_comb_func().aw.ready) {
                state_reg._next = ST_EVICT_W;
            }
        }
        else if (state_reg == ST_EVICT_W) {
            if (axi_out_driver_comb_func().w.valid && axi_out_selected_resp_comb_func().w.ready) {
                if (trace_line_enabled && ((axi_route_comb_func().aw_full_addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line)) {
                    trace_word0 = (uint32_t)evict_line_comb_func();
                    trace_word1 = PORT_WORDS > 1 ? (uint32_t)(evict_line_comb_func() >> 32) : 0;
                    std::print("trace-l2 cycle={} evict addr={:08x} beat={} data0={:08x} data1={:08x} way={}\n",
                        _system_clock, (uint32_t)axi_route_comb_func().aw_full_addr, (uint32_t)evict_beat_reg,
                        trace_word0, trace_word1, (uint32_t)evict_candidate.way);
                }
                state_reg._next = ST_EVICT_B;
            }
        }
        else if (state_reg == ST_EVICT_B) {
            if (axi_out_selected_resp_comb_func().b.valid) {
                if (evict_beat_reg == LINE_BEATS - 1) {
                    fill_beat_reg._next = 0;
                    state_reg._next = ST_AXI_AR;
                }
                else {
                    evict_beat_reg._next = evict_beat_reg + 1;
                    state_reg._next = ST_EVICT_AW;
                }
            }
        }
        else if (state_reg == ST_AXI_AR) {
            if (axi_out_driver_comb_func().ar.valid && axi_out_selected_resp_comb_func().ar.ready) {
                state_reg._next = ST_AXI_R;
            }
        }
        else if (state_reg == ST_AXI_R) {
            if (axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready) {
                refill_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                state_reg._next = ST_AXI_R_WRITE;
            }
        }
        else if (state_reg == ST_AXI_R_WRITE) {
                if (trace_req_line) {
                    trace_word0 = (uint32_t)refill_data_reg;
                    trace_word1 = PORT_WORDS > 1 ? (uint32_t)(refill_data_reg >> 32) : 0;
                    std::print("trace-l2 cycle={} fill addr={:08x} beat={} data0={:08x} data1={:08x} req_word={} req_beat={}\n",
                        _system_clock, (uint32_t)axi_route_comb_func().ar_full_addr, (uint32_t)fill_beat_reg,
                        trace_word0, trace_word1, (uint32_t)request_geometry.word,
                        (uint32_t)request_geometry.beat);
                }
                if (req_reg.read && fill_beat_reg == request_geometry.beat) {
                    // Preserve an early requested beat in the unified response
                    // stage until the complete cache line has been installed.
                    response_reg._next[CPU_RESPONSE_BASE + req_reg.cpu_index].r.data = refill_data_reg;
                }
                if (fill_beat_reg == LINE_BEATS - 1) {
                    // Final fill beat commits the line; a spillover store then re-enters lookup for the next line.
                    victim_reg._next = (victim_reg == WAYS - 1) ? 0 : victim_reg + 1;
                    if (request_geometry.cross_line_write) {
                        req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                        req_reg._next.write_data = request_geometry.cross_write_data;
                        req_reg._next.write_mask = request_geometry.cross_write_mask;
                        req_reg._next.write_strobe = active_request.request.write_strobe;
                        state_reg._next = ST_CROSS_WRITE_READ;
                    }
                    else {
                        if (req_reg.from_slave) {
                            for (i = 0; i < MEM_PORTS; ++i) {
                                if (req_reg.slave_index == i) {
                                    if (req_reg.read) {
                                        // If the requested beat arrived before the final
                                        // fill beat, return data retained in the response stage.
                                        send_slave_read_response(i, req_reg.slave_id,
                                            (fill_beat_reg == request_geometry.beat) ?
                                                refill_data_reg :
                                                response_reg[CPU_RESPONSE_BASE + req_reg.cpu_index].r.data);
                                    }
                                    if (req_reg.write) {
                                        send_slave_write_response(i, req_reg.slave_id);
                                    }
                                }
                            }
                            state_reg._next = ST_IDLE;
                        }
                        else {
                            completion_data = req_reg.read ?
                                ((fill_beat_reg == request_geometry.beat) ?
                                    refill_data_reg :
                                    response_reg[CPU_RESPONSE_BASE + req_reg.cpu_index].r.data) : logic<256>(0);
                            send_cpu_response(completion_data);
                            state_reg._next = ST_IDLE;
                        }
                    }
                }
                else {
                    fill_beat_reg._next = fill_beat_reg + 1;
                    state_reg._next = ST_AXI_AR;
                }
        }
        else if (state_reg == ST_CROSS_AR0) {
            if (axi_out_driver_comb_func().ar.valid && axi_out_selected_resp_comb_func().ar.ready) {
                state_reg._next = ST_CROSS_R0;
            }
        }
        else if (state_reg == ST_CROSS_R0) {
            if (axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready) {
                // Save the beat containing the tail bytes before requesting the next line.
                cross_low_reg._next = axi_out_selected_resp_comb_func().r.data;
                state_reg._next = ST_CROSS_AR1;
            }
        }
        else if (state_reg == ST_CROSS_AR1) {
            if (axi_out_driver_comb_func().ar.valid && axi_out_selected_resp_comb_func().ar.ready) {
                state_reg._next = ST_CROSS_R1;
            }
        }
        else if (state_reg == ST_CROSS_R1) {
            if (axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready) {
                // Save the next-line beat containing the high bytes of the unaligned word.
                cross_high_reg._next = axi_out_selected_resp_comb_func().r.data;
                state_reg._next = ST_CROSS_DONE;
            }
        }
        else if (state_reg == ST_CROSS_DONE) {
            // Hold the assembled cross-line word for one cycle like other completed responses.
            if (req_reg.from_slave) {
                for (i = 0; i < MEM_PORTS; ++i) {
                    if (req_reg.slave_index == i) {
                        send_slave_read_response(i, req_reg.slave_id, cross_read_data_comb_func());
                    }
                }
                state_reg._next = ST_IDLE;
            }
            else {
                send_cpu_response(cross_read_data_comb_func());
                state_reg._next = ST_IDLE;
            }
        }
        else if (state_reg == ST_IO_AW) {
            if (axi_out_driver_comb_func().aw.valid && axi_out_selected_resp_comb_func().aw.ready) {
                state_reg._next = ST_IO_W;
            }
        }
        else if (state_reg == ST_IO_W) {
            if (axi_out_driver_comb_func().w.valid && axi_out_selected_resp_comb_func().w.ready) {
                state_reg._next = ST_IO_B;
            }
        }
        else if (state_reg == ST_IO_B) {
            if (axi_out_selected_resp_comb_func().b.valid) {
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            send_slave_write_response(i, req_reg.slave_id);
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    send_cpu_response(0);
                    state_reg._next = ST_IDLE;
                }
            }
        }
        else if (state_reg == ST_IO_AR) {
            if (axi_out_driver_comb_func().ar.valid && axi_out_selected_resp_comb_func().ar.ready) {
                state_reg._next = ST_IO_R;
            }
        }
        else if (state_reg == ST_IO_R) {
            if (axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready) {
                refill_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                state_reg._next = ST_IO_R_RESULT;
            }
        }
        else if (state_reg == ST_IO_R_RESULT) {
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            send_slave_read_response(i, req_reg.slave_id, refill_data_reg);
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    send_cpu_response(refill_data_reg);
                    state_reg._next = ST_IDLE;
                }
        }
        else if (state_reg == ST_DONE) {
            // Retained for checkpoint/state-number compatibility; all new
            // completions enter the registered CacheResponse stage directly.
            state_reg._next = ST_IDLE;
        }

        if (reset) {
            state_reg.clr();
            req_reg.clr();
            request_pipe_reg.clr();
            request_pipe_valid_reg.clr();
            cpu_rr_reg.clr();
            victim_reg.clr();
            fill_way_reg.clr();
            init_set_reg.clr();
            cross_low_reg.clr();
            cross_high_reg.clr();
            refill_data_reg.clr();
            fill_beat_reg.clr();
            evict_beat_reg.clr();
            evict_tag_reg.clr();
            evict_line_reg.clr();
            for (i = 0; i < RESPONSE_SLOTS; ++i) {
                // Clear by field because whole struct-array clr() is not generator-safe.
                response_reg._next[i].valid = false;
                response_reg._next[i].read = false;
                response_reg._next[i].write = false;
                response_reg._next[i].data_port = false;
                response_reg._next[i].addr = 0;
                response_reg._next[i].b.valid = false;
                response_reg._next[i].b.id = 0;
                response_reg._next[i].r.valid = false;
                response_reg._next[i].r.id = 0;
                response_reg._next[i].r.data = 0;
                response_reg._next[i].r.last = false;
            }
            for (i = 0; i < MEM_PORTS; ++i) {
                slave_aw_reg._next[i].valid = false;
                slave_aw_reg._next[i].addr = 0;
                slave_aw_reg._next[i].id = 0;
                slave_aw_seen_reg._next[i].valid = false;
                slave_aw_seen_reg._next[i].addr = 0;
                slave_aw_seen_reg._next[i].id = 0;
                slave_ar_seen_reg._next[i].valid = false;
                slave_ar_seen_reg._next[i].addr = 0;
                slave_ar_seen_reg._next[i].id = 0;
            }
            slave_aw_novelty_reg.clr();
            slave_ar_novelty_reg.clr();
            lookup_data_reg.clr();
            lookup_tag_reg.clr();
            lookup_hit_reg.clr();
            lookup_evict_reg.clr();
            lookup_write_pair_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe_l2_clock()
    {
        for (size_t bank = 0; bank < DATA_BANKS; ++bank)
            data_ram[bank]._strobe_l2_clock();
        for (size_t way = 0; way < WAYS; ++way)
            tag_ram[way]._strobe_l2_clock();
        lookup_data_reg.strobe();
        lookup_tag_reg.strobe();
        lookup_hit_reg.strobe();
        lookup_evict_reg.strobe();
        lookup_write_pair_reg.strobe();
        state_reg.strobe();
        req_reg.strobe();
        request_pipe_reg.strobe();
        request_pipe_valid_reg.strobe();
        // Arbitration order is transient and must not change the checkpoint stream format.
        cpu_rr_reg.strobe();
        victim_reg.strobe();
        fill_way_reg.strobe();
        init_set_reg.strobe();
        response_reg.strobe();
        cross_low_reg.strobe();
        cross_high_reg.strobe();
        refill_data_reg.strobe();
        fill_beat_reg.strobe();
        evict_beat_reg.strobe();
        // Transient eviction metadata is intentionally omitted from the
        // checkpoint stream to keep existing checkpoint files compatible.
        evict_tag_reg.strobe();
        evict_line_reg.strobe();
        slave_aw_reg.strobe();
        slave_aw_seen_reg.strobe();
        slave_ar_seen_reg.strobe();
        slave_aw_novelty_reg.strobe();
        slave_ar_novelty_reg.strobe();
    }

#ifndef SYNTHESIS
    // Preserve the exact legacy checkpoint byte order.  In particular, the
    // old packed implementation wrote all RAM images first, followed by all
    // synchronous read-output registers.  New controller state belongs only
    // in checkpoint_l2_pipeline(), which the owner writes as an optional
    // end-of-file trailer.
    void checkpoint_l2(FILE* checkpoint_fd)
    {
        for (size_t bank = 0; bank < DATA_BANKS; ++bank)
            data_ram[bank].checkpoint_memory_l2(checkpoint_fd);
        for (size_t way = 0; way < WAYS; ++way)
            tag_ram[way].checkpoint_memory_l2(checkpoint_fd);
        for (size_t bank = 0; bank < DATA_BANKS; ++bank)
            data_ram[bank].checkpoint_read_data_current_l2(checkpoint_fd);
        for (size_t bank = 0; bank < DATA_BANKS; ++bank)
            data_ram[bank].checkpoint_read_data_next_l2(checkpoint_fd);
        for (size_t way = 0; way < WAYS; ++way)
            tag_ram[way].checkpoint_read_data_current_l2(checkpoint_fd);
        for (size_t way = 0; way < WAYS; ++way)
            tag_ram[way].checkpoint_read_data_next_l2(checkpoint_fd);
        lookup_data_reg.strobe(checkpoint_fd);
        lookup_tag_reg.strobe(checkpoint_fd);
        lookup_hit_reg.strobe(checkpoint_fd);
        lookup_evict_reg.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        req_reg.strobe(checkpoint_fd);
        // Arbitration order is transient and is not part of the stream.
        cpu_rr_reg.strobe();
        victim_reg.strobe(checkpoint_fd);
        fill_way_reg.strobe(checkpoint_fd);
        init_set_reg.strobe(checkpoint_fd);
        response_reg.strobe(checkpoint_fd);
        cross_low_reg.strobe(checkpoint_fd);
        cross_high_reg.strobe(checkpoint_fd);
        refill_data_reg.strobe(checkpoint_fd);
        fill_beat_reg.strobe(checkpoint_fd);
        evict_beat_reg.strobe(checkpoint_fd);
        // Transient eviction and request-novelty metadata remain omitted for
        // compatibility with checkpoints produced before the clock split.
        evict_tag_reg.strobe();
        evict_line_reg.strobe();
        slave_aw_reg.strobe(checkpoint_fd);
        slave_aw_seen_reg.strobe();
        slave_ar_seen_reg.strobe();
        slave_aw_novelty_reg.strobe();
        slave_ar_novelty_reg.strobe();
    }

    // State introduced by the timing pipeline cannot be inserted into the
    // legacy stream above.  The top-level checkpoint owner serializes it only
    // after all legacy and AXI-CDC state, under its own optional trailer magic.
    void checkpoint_l2_pipeline(FILE* checkpoint_fd)
    {
        lookup_write_pair_reg.strobe(checkpoint_fd);
        request_pipe_reg.strobe(checkpoint_fd);
        request_pipe_valid_reg.strobe(checkpoint_fd);
    }

    void clear_checkpoint_l2_pipeline()
    {
        lookup_write_pair_reg.clr();
        request_pipe_reg.clr();
        request_pipe_valid_reg.clr();
    }
#endif

    // L2 state is owned exclusively by the divided memory clock.
    void _work_clk(bool) {}
    void _strobe_clk() {}
};

#undef L2_FOR_EACH_TAG_BANK
#undef L2_FOR_EACH_DATA_BANK
