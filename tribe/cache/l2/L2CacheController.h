#pragma once

#include "L2CacheWait.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Final port-compatible L2 cache controller: wires RAM/AXI ports, advances FSM state, and checkpoints registers.
class L2Cache : public L2CacheWait<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>
{
protected:
    using Base = L2CacheWait<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>;
public:
    using Base::i_mem_in;
    using Base::d_mem_in;
    using Base::axi_in;
    using Base::axi_out;
    using Base::debugen_in;

private:
    using Base::LINE_WORDS;
    using Base::PORT_WORDS;
    using Base::LINE_BEATS;
    using Base::SETS;
    using Base::DATA_BANKS;
    using Base::data_ram;
    using Base::tag_ram;
    using Base::data_q_reg;
    using Base::tag_q_reg;
    using Base::state_reg;
    using Base::req_reg;
    using Base::victim_reg;
    using Base::fill_way_reg;
    using Base::init_set_reg;
    using Base::last_data_reg;
    using Base::slave_fill_data_reg;
    using Base::cross_low_reg;
    using Base::cross_high_reg;
    using Base::fill_beat_reg;
    using Base::evict_beat_reg;
    using Base::evict_tag_reg;
    using Base::evict_line_reg;
    using Base::slave_b_reg;
    using Base::slave_r_reg;
    using Base::slave_aw_reg;
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

    // Build all AXI slave-side responder bundles and return the array so comb users depend on the driven values.
    Axi4Responder<4,256> (&axi_in_comb_func())[MEM_PORTS]
    {
        uint32_t index;
        L2ActiveRequestComb active_request;

        active_request = active_request_comb_func();
        for (index = 0; index < MEM_PORTS; ++index) {
            axi_in_comb[index].aw.ready = state_reg == ST_IDLE && !slave_aw_reg[index].valid &&
                !slave_b_reg[index].valid && axi_in[index].awvalid_in();
            axi_in_comb[index].w.ready = state_reg == ST_IDLE &&
                active_request.request.from_slave && active_request.request.write &&
                active_request.request.slave_index == index;
            axi_in_comb[index].b.valid = slave_b_reg[index].valid;
            axi_in_comb[index].b.id = slave_b_reg[index].id;
            axi_in_comb[index].ar.ready = state_reg == ST_IDLE &&
                active_request.request.from_slave && active_request.request.read &&
                active_request.request.slave_index == index;
            axi_in_comb[index].r.valid = slave_r_reg[index].valid;
            axi_in_comb[index].r.data = (logic<256>)slave_r_reg[index].data;
            axi_in_comb[index].r.last = slave_r_reg[index].last;
            axi_in_comb[index].r.id = slave_r_reg[index].id;
        }
        return axi_in_comb;
    }

    // Build all AXI master-side driver bundles and return the array so comb users depend on the driven values.
    Axi4Driver<32,4,256> (&axi_out_comb_func())[MEM_PORTS]
    {
        uint32_t index;

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
        slave_r_reg._next[index].valid = true;
        slave_r_reg._next[index].id = id;
        slave_r_reg._next[index].data = data;
        slave_r_reg._next[index].last = true;
    }

    // AXI slave write responses use the same exact-width index after the accepted write is committed or forwarded.
    void send_slave_write_response(u<3> index, u<4> id)
    {
        slave_b_reg._next[index].valid = true;
        slave_b_reg._next[index].id = id;
    }

public:
    void _assign()
    {
        uint32_t i;
        this->i_mem_in.read_data_out = _ASSIGN_COMB(read_data_comb_func());
        this->i_mem_in.wait_out = _ASSIGN_COMB(cpu_wait_comb_func().instruction);
        d_mem_in.read_data_out = _ASSIGN_COMB(read_data_comb_func());
        d_mem_in.wait_out = _ASSIGN_COMB(cpu_wait_comb_func().data);

        for (i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM_COMB_INDEXED(axi_in[i], axi_in_comb_func(), i);
            AXI4_DRIVER_FROM_COMB_INDEXED(axi_out[i], axi_out_comb_func(), i);
        }
    }

    void _work(bool reset)
    {
        uint32_t i;
        uint32_t way;
        uint32_t bank_addr;
        uint32_t data_ram_index;
        uint32_t tag_ram_index;
        bool bank_read;
        bool bank_write;
        uint32_t bank_data;
        bool tag_bank_read;
        bool tag_bank_write;
        uint32_t trace_line;
        bool trace_line_enabled;
        bool trace_req_line;
        bool trace_active_line;
        uint32_t trace_word0;
        uint32_t trace_word1;
        // Keep cpphdl local logic declarations after scalar locals: the SV
        // backend emits default construction as an assignment, and Verilator
        // requires all task declarations before the first assignment.
        L2ActiveRequestComb active_request;
        L2RequestGeometryComb request_geometry;
        L2EvictCandidateComb evict_candidate;
        L2HitLookupComb hit_lookup;
        L2WordPairComb hit_write_pair;
        L2WordPairComb fill_write_pair;
        logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8> tag_bank_data;
        active_request = active_request_comb_func();
        request_geometry = request_geometry_comb_func();
        evict_candidate = evict_candidate_comb_func();
        hit_lookup = hit_lookup_comb_func();
        hit_write_pair = hit_write_pair_comb_func();
        fill_write_pair = fill_write_pair_comb_func();
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
        const char* trace_line_env;
        trace_line_env = std::getenv("TRIBE_TRACE_L2_LINE");
        trace_line_enabled = trace_line_env != nullptr;
        trace_line = trace_line_env ? (uint32_t)std::strtoul(trace_line_env, nullptr, 0) & ~(uint32_t)(CACHE_LINE_SIZE - 1) : 0;
        trace_req_line = trace_line_env && (((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
        trace_active_line = trace_line_env && (((uint32_t)active_request.request.addr &
            ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
#endif
        trace_word0 = 0;
        trace_word1 = 0;

        bank_addr = (state_reg == ST_IDLE) ? active_request.set : request_geometry.set;
        // ST_IDLE only latches the arbitrated request. Read tag/data RAMs one
        // cycle later from req_reg so generated SV cannot use a live input set
        // while ST_LOOKUP consumes stale registered RAM outputs.
        bank_read = state_reg == ST_READ || state_reg == ST_CROSS_WRITE_LOOKUP;
        for (i = 0; i < DATA_BANKS; ++i) {
            // Fill writes only the words carried by the current AXI beat;
            // store hits update one or two addressed word banks.
            bank_write =
                (state_reg == ST_AXI_R && axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready && fill_way_reg == (i / LINE_WORDS) &&
                    (i % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ||
                (state_reg == ST_LOOKUP && req_reg.from_slave && req_reg.write && hit_lookup.hit &&
                    hit_lookup.way == (i / LINE_WORDS) &&
                    (i % LINE_WORDS) >= (uint32_t)request_geometry.beat * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)request_geometry.beat + 1u) * PORT_WORDS &&
                    req_reg.write_word_mask[(i % LINE_WORDS) % PORT_WORDS]) ||
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_reg.write && hit_lookup.hit &&
                    !req_reg.from_slave &&
                    hit_lookup.way == (i / LINE_WORDS) &&
                    (request_geometry.word == (i % LINE_WORDS) ||
                     (((uint32_t)req_reg.addr & 3u) != 0 &&
                        (uint32_t)request_geometry.word + 1 == (i % LINE_WORDS))));
            bank_data = (state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) ?
                (req_reg.from_slave ?
                    (uint32_t)(req_reg.write_beat >> (((i % PORT_WORDS) * 32))) :
                    ((((uint32_t)req_reg.addr & 3u) != 0 &&
                        (uint32_t)request_geometry.word + 1 == (i % LINE_WORDS)) ?
                        (uint32_t)hit_write_pair.next_word : (uint32_t)hit_write_pair.word)) :
                ((req_reg.from_slave && req_reg.write && request_geometry.beat == fill_beat_reg &&
                    (i % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ?
                    (req_reg.write_word_mask[(i % LINE_WORDS) % PORT_WORDS] ?
                        (uint32_t)(req_reg.write_beat >> (((i % PORT_WORDS) * 32))) :
                        (uint32_t)(axi_out_selected_resp_comb_func().r.data >> ((((i % LINE_WORDS) % PORT_WORDS) * 32)))) :
                 (req_reg.write && request_geometry.word == (i % LINE_WORDS)) ? (uint32_t)fill_write_pair.word :
                 (req_reg.write && ((uint32_t)req_reg.addr & 3u) != 0 &&
                    (uint32_t)request_geometry.word + 1 == (i % LINE_WORDS)) ? (uint32_t)fill_write_pair.next_word :
                    (uint32_t)(axi_out_selected_resp_comb_func().r.data >> ((((i % LINE_WORDS) % PORT_WORDS) * 32))));
            if (bank_write) {
                data_ram_index = (uint32_t)bank_addr * DATA_BANKS + i;
                data_ram[data_ram_index] = bank_data;
            }
            if (bank_read) {
                data_ram_index = (uint32_t)bank_addr * DATA_BANKS + i;
                data_q_reg._next[i] = data_ram[data_ram_index];
            }
        }

        tag_bank_data = tag_write_data_comb_func();
        for (way = 0; way < WAYS; ++way) {
            tag_bank_read = bank_read;
            tag_bank_write = (state_reg == ST_INIT) ||
                (state_reg == ST_AXI_R && axi_out_selected_resp_comb_func().r.valid && axi_out_driver_comb_func().r.ready && fill_beat_reg == LINE_BEATS - 1 && fill_way_reg == way) ||
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_reg.write && hit_lookup.hit && hit_lookup.way == way);
            if (tag_bank_write) {
                tag_ram_index = (uint32_t)((state_reg == ST_INIT) ? init_set_reg : bank_addr) * WAYS + way;
                tag_ram[tag_ram_index] = tag_bank_data;
            }
            if (tag_bank_read) {
                tag_ram_index = (uint32_t)((state_reg == ST_INIT) ? init_set_reg : bank_addr) * WAYS + way;
                tag_q_reg._next[way] = tag_ram[tag_ram_index];
            }
        }

        for (i = 0; i < MEM_PORTS; ++i) {
            if (slave_b_reg[i].valid && axi_in[i].bready_in()) {
                slave_b_reg._next[i].valid = false;
            }
            if (slave_r_reg[i].valid && axi_in[i].rready_in()) {
                slave_r_reg._next[i].valid = false;
                slave_r_reg._next[i].last = false;
            }
            if (state_reg == ST_IDLE && axi_in[i].awvalid_in() && axi_in[i].awready_out()) {
                slave_aw_reg._next[i].valid = true;
                slave_aw_reg._next[i].addr = axi_in[i].awaddr_in();
                slave_aw_reg._next[i].id = axi_in[i].awid_in();
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
            if (active_request.valid) {
                if (trace_active_line) {
                    std::print("trace-l2 cycle={} accept addr={:08x} rd={} wr={} wdata={:08x} mask={:02x} slave={} dport={} victim={}\n",
                        _system_clock, (uint32_t)active_request.request.addr,
                        (bool)active_request.request.read, (bool)active_request.request.write,
                        (uint32_t)active_request.request.write_data,
                        (uint32_t)active_request.request.write_mask,
                        (bool)active_request.request.from_slave,
                        (bool)active_request.request.port, (uint32_t)victim_reg);
                }
                req_reg._next = active_request.request;
                for (i = 0; i < MEM_PORTS; ++i) {
                    if (active_request.request.from_slave && active_request.request.write &&
                        active_request.request.slave_index == i) {
                        slave_aw_reg._next[i].valid = false;
                    }
                }
                // ST_READ samples tag/data arrays from the latched request.
                // ST_LOOKUP then consumes those registered RAM outputs.
                state_reg._next = active_request.cross_line_read ? ST_CROSS_AR0 : ST_READ;
            }
        }
        else if (state_reg == ST_READ) {
            state_reg._next = ST_LOOKUP;
        }
        else if (state_reg == ST_LOOKUP) {
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
                    if (req_reg.read) {
                        last_data_reg._next = 0;
                    }
                    state_reg._next = ST_DONE;
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
                else if (req_reg.read) {
                    last_data_reg._next = 0;
                }
                if (!req_reg.from_slave && req_reg.read) {
                    if (request_geometry.cross_beat_read) {
                        last_data_reg._next = 0;
                        last_data_reg._next.bits(31, 0) = hit_lookup.read_word;
                    }
                    else {
                        last_data_reg._next = hit_lookup.beat;
                    }
                }
                if (request_geometry.cross_line_write) {
                    // Finish the part of an unaligned store that spills into the first word of the next line.
                    req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                    req_reg._next.write_data = request_geometry.cross_write_data;
                    req_reg._next.write_mask = request_geometry.cross_write_mask;
                    req_reg._next.write_strobe = active_request.request.write_strobe;
                    state_reg._next = ST_CROSS_WRITE_LOOKUP;
                }
                else {
                    // CPU/L1 hits are completed from ST_DONE so read data comes from
                    // a registered beat, not a same-cycle RAM lookup that can be stale after fills.
                    state_reg._next = req_reg.from_slave ? ST_IDLE : ((req_reg.read || req_reg.write) ? ST_DONE : ST_IDLE);
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
        else if (state_reg == ST_CROSS_WRITE_LOOKUP) {
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
                    state_reg._next = ST_DONE;
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
                    state_reg._next = ST_DONE;
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
                if (trace_req_line) {
                    trace_word0 = (uint32_t)axi_out_selected_resp_comb_func().r.data;
                    trace_word1 = PORT_WORDS > 1 ? (uint32_t)(axi_out_selected_resp_comb_func().r.data >> 32) : 0;
                    std::print("trace-l2 cycle={} fill addr={:08x} beat={} data0={:08x} data1={:08x} req_word={} req_beat={}\n",
                        _system_clock, (uint32_t)axi_route_comb_func().ar_full_addr, (uint32_t)fill_beat_reg,
                        trace_word0, trace_word1, (uint32_t)request_geometry.word,
                        (uint32_t)request_geometry.beat);
                }
                if (!req_reg.from_slave && req_reg.read && fill_beat_reg == request_geometry.beat) {
                    last_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                }
                if (req_reg.from_slave && req_reg.read && fill_beat_reg == request_geometry.beat) {
                    // Do not answer from hit_lookup.beat after the final fill beat:
                    // that path can observe stale RAM output for the requested beat.
                    slave_fill_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                }
                if (fill_beat_reg == LINE_BEATS - 1) {
                    // Final fill beat commits the line; a spillover store then re-enters lookup for the next line.
                    victim_reg._next = (victim_reg == WAYS - 1) ? 0 : victim_reg + 1;
                    if (request_geometry.cross_line_write) {
                        req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                        req_reg._next.write_data = request_geometry.cross_write_data;
                        req_reg._next.write_mask = request_geometry.cross_write_mask;
                        req_reg._next.write_strobe = active_request.request.write_strobe;
                        state_reg._next = ST_CROSS_WRITE_LOOKUP;
                    }
                    else {
                        if (req_reg.from_slave) {
                            for (i = 0; i < MEM_PORTS; ++i) {
                                if (req_reg.slave_index == i) {
                                    if (req_reg.read) {
                                        // If the requested beat arrived before the final
                                        // fill beat, return the latched refill data.
                                        send_slave_read_response(i, req_reg.slave_id,
                                            (fill_beat_reg == request_geometry.beat) ?
                                                axi_out_selected_resp_comb_func().r.data : slave_fill_data_reg);
                                    }
                                    if (req_reg.write) {
                                        send_slave_write_response(i, req_reg.slave_id);
                                    }
                                }
                            }
                            state_reg._next = ST_IDLE;
                        }
                        else {
                            state_reg._next = ST_DONE;
                        }
                    }
                }
                else {
                    fill_beat_reg._next = fill_beat_reg + 1;
                    state_reg._next = ST_AXI_AR;
                }
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
                last_data_reg._next = cross_read_data_comb_func();
                state_reg._next = ST_DONE;
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
                    state_reg._next = ST_DONE;
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
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            send_slave_read_response(i, req_reg.slave_id, axi_out_selected_resp_comb_func().r.data);
                        }
                    }
                    state_reg._next = ST_IDLE;
                }
                else {
                    last_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                    state_reg._next = ST_DONE;
                }
            }
        }
        else if (state_reg == ST_DONE) {
            if (req_reg.from_slave) {
                for (i = 0; i < MEM_PORTS; ++i) {
                    if (req_reg.slave_index == i) {
                        if (req_reg.read) {
                            send_slave_read_response(i, req_reg.slave_id, last_data_reg);
                        }
                        if (req_reg.write) {
                            send_slave_write_response(i, req_reg.slave_id);
                        }
                    }
                }
            }
            state_reg._next = ST_IDLE;
        }

        if (reset) {
            state_reg.clr();
            req_reg.clr();
            victim_reg.clr();
            fill_way_reg.clr();
            init_set_reg.clr();
            last_data_reg.clr();
            slave_fill_data_reg.clr();
            cross_low_reg.clr();
            cross_high_reg.clr();
            fill_beat_reg.clr();
            evict_beat_reg.clr();
            evict_tag_reg.clr();
            evict_line_reg.clr();
            for (i = 0; i < MEM_PORTS; ++i) {
                // Clear AXI slave bookkeeping by channel field; whole struct-array clr() is not generator-safe.
                slave_b_reg._next[i].valid = false;
                slave_b_reg._next[i].id = 0;
                slave_r_reg._next[i].valid = false;
                slave_r_reg._next[i].id = 0;
                slave_r_reg._next[i].data = 0;
                slave_r_reg._next[i].last = false;
                slave_aw_reg._next[i].valid = false;
                slave_aw_reg._next[i].addr = 0;
                slave_aw_reg._next[i].id = 0;
            }
            data_q_reg.clr();
            tag_q_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        data_ram.apply(checkpoint_fd);
        tag_ram.apply(checkpoint_fd);
        data_q_reg.strobe(checkpoint_fd);
        tag_q_reg.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        req_reg.strobe(checkpoint_fd);
        victim_reg.strobe(checkpoint_fd);
        fill_way_reg.strobe(checkpoint_fd);
        init_set_reg.strobe(checkpoint_fd);
        last_data_reg.strobe(checkpoint_fd);
        // Transient refill response data is omitted from checkpoints; old
        // checkpoint streams remain load-compatible.
        slave_fill_data_reg.strobe();
        cross_low_reg.strobe(checkpoint_fd);
        cross_high_reg.strobe(checkpoint_fd);
        fill_beat_reg.strobe(checkpoint_fd);
        evict_beat_reg.strobe(checkpoint_fd);
        // Transient eviction metadata is intentionally omitted from the
        // checkpoint stream to keep existing checkpoint files compatible.
        evict_tag_reg.strobe();
        evict_line_reg.strobe();
        slave_b_reg.strobe(checkpoint_fd);
        slave_r_reg.strobe(checkpoint_fd);
        slave_aw_reg.strobe(checkpoint_fd);
    }
};
