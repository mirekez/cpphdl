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
    using Base::slave_write_pending_comb_func;
    using Base::slave_read_pending_comb_func;
    using Base::active_slave_index_comb_func;
    using Base::active_is_slave_comb_func;
    using Base::active_is_d_comb_func;
    using Base::active_read_comb_func;
    using Base::active_write_comb_func;
    using Base::active_addr_comb_func;
    using Base::active_write_data_comb_func;
    using Base::active_write_beat_comb_func;
    using Base::active_write_mask_comb_func;
    using Base::active_write_strobe_comb_func;
    using Base::active_write_word_mask_comb_func;
    using Base::req_set_comb_func;
    using Base::active_set_comb_func;
    using Base::req_word_comb_func;
    using Base::req_beat_comb_func;
    using Base::req_cross_beat_read_comb_func;
    using Base::active_cross_line_read_comb_func;
    using Base::req_cross_line_write_comb_func;
    using Base::cross_write_data_comb_func;
    using Base::cross_write_mask_comb_func;
    using Base::req_addr_in_memory_comb_func;
    using Base::axi_route_comb_func;
    using Base::axi_out_driver_comb_func;
    using Base::axi_out_selected_resp_comb_func;
    using Base::evict_way_comb_func;
    using Base::evict_valid_comb_func;
    using Base::evict_dirty_comb_func;
    using Base::evict_tag_comb_func;
    using Base::evict_line_snapshot_comb_func;
    using Base::evict_line_comb_func;
    using Base::req_uncached_region_comb_func;
    using Base::hit_comb_func;
    using Base::hit_way_comb_func;
    using Base::hit_word_comb_func;
    using Base::write_word_comb_func;
    using Base::write_next_word_comb_func;
    using Base::fill_write_word_comb_func;
    using Base::fill_write_next_word_comb_func;
    using Base::hit_beat_comb_func;
    using Base::cross_read_data_comb_func;
    using Base::tag_write_data_comb_func;
    using Base::read_data_comb_func;
    using Base::i_wait_comb_func;
    using Base::d_wait_comb_func;

    Axi4Responder<4,256> axi_in_comb[MEM_PORTS];
    Axi4Driver<32,4,256> axi_out_comb[MEM_PORTS];

    // Build all AXI slave-side responder bundles and return the array so comb users depend on the driven values.
    Axi4Responder<4,256> (&axi_in_comb_func())[MEM_PORTS]
    {
        size_t index;

        for (index = 0; index < MEM_PORTS; ++index) {
            axi_in_comb[index].aw.ready = state_reg == ST_IDLE && !slave_aw_reg[index].valid &&
                !slave_b_reg[index].valid && axi_in[index].awvalid_in();
            axi_in_comb[index].w.ready = state_reg == ST_IDLE &&
                slave_write_pending_comb_func() && active_slave_index_comb_func() == index;
            axi_in_comb[index].b.valid = slave_b_reg[index].valid;
            axi_in_comb[index].b.id = slave_b_reg[index].id;
            axi_in_comb[index].ar.ready = state_reg == ST_IDLE && active_is_slave_comb_func() &&
                !slave_write_pending_comb_func() && slave_read_pending_comb_func() &&
                active_slave_index_comb_func() == index;
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
        size_t index;

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

    // AXI slave read responses are emitted by the controller FSM after a hit, fill, IO read, or cross-line read completes.
    void send_slave_read_response(size_t index, u<4> id, logic<256> data)
    {
        slave_r_reg._next[index].valid = true;
        slave_r_reg._next[index].id = id;
        slave_r_reg._next[index].data = data;
        slave_r_reg._next[index].last = true;
    }

    // AXI slave write responses are emitted by the controller FSM after the accepted write is committed or forwarded.
    void send_slave_write_response(size_t index, u<4> id)
    {
        slave_b_reg._next[index].valid = true;
        slave_b_reg._next[index].id = id;
    }

public:
    void _assign()
    {
        size_t i;
        this->i_mem_in.read_data_out = _ASSIGN_COMB(read_data_comb_func());
        this->i_mem_in.wait_out = _ASSIGN_COMB(i_wait_comb_func());
        d_mem_in.read_data_out = _ASSIGN_COMB(read_data_comb_func());
        d_mem_in.wait_out = _ASSIGN_COMB(d_wait_comb_func());

        for (i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM_COMB_INDEXED(axi_in[i], axi_in_comb_func(), i);
            AXI4_DRIVER_FROM_COMB_INDEXED(axi_out[i], axi_out_comb_func(), i);
        }
    }

    void _work(bool reset)
    {
        size_t i;
        size_t way;
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
        logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8> tag_bank_data;
        trace_line = 0;
        trace_line_enabled = false;
        trace_req_line = false;
        trace_active_line = false;
        if (debugen_in) {
            trace_line_enabled = true;
            trace_line = 0x400u;
            trace_req_line = (((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
            trace_active_line = ((active_addr_comb_func() & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
        }
#ifndef SYNTHESIS
        const char* trace_line_env;
        trace_line_env = std::getenv("TRIBE_TRACE_L2_LINE");
        trace_line_enabled = trace_line_env != nullptr;
        trace_line = trace_line_env ? (uint32_t)std::strtoul(trace_line_env, nullptr, 0) & ~(uint32_t)(CACHE_LINE_SIZE - 1) : 0;
        trace_req_line = trace_line_env && (((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
        trace_active_line = trace_line_env && ((active_addr_comb_func() & ~(uint32_t)(CACHE_LINE_SIZE - 1)) == trace_line);
#endif
        trace_word0 = 0;
        trace_word1 = 0;

        bank_addr = (state_reg == ST_IDLE) ? active_set_comb_func() : req_set_comb_func();
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
                (state_reg == ST_LOOKUP && req_reg.from_slave && req_reg.write && hit_comb_func() &&
                    hit_way_comb_func() == (i / LINE_WORDS) &&
                    (i % LINE_WORDS) >= (uint32_t)req_beat_comb_func() * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)req_beat_comb_func() + 1u) * PORT_WORDS &&
                    req_reg.write_word_mask[(i % LINE_WORDS) % PORT_WORDS]) ||
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_reg.write && hit_comb_func() &&
                    !req_reg.from_slave &&
                    hit_way_comb_func() == (i / LINE_WORDS) &&
                    (req_word_comb_func() == (i % LINE_WORDS) ||
                     (((uint32_t)req_reg.addr & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS))));
            bank_data = (state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) ?
                (req_reg.from_slave ?
                    (uint32_t)(req_reg.write_beat >> (((i % PORT_WORDS) * 32))) :
                    ((((uint32_t)req_reg.addr & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS)) ?
                        write_next_word_comb_func() : write_word_comb_func())) :
                ((req_reg.from_slave && req_reg.write && req_beat_comb_func() == fill_beat_reg &&
                    (i % LINE_WORDS) >= (uint32_t)fill_beat_reg * PORT_WORDS &&
                    (i % LINE_WORDS) < ((uint32_t)fill_beat_reg + 1u) * PORT_WORDS) ?
                    (req_reg.write_word_mask[(i % LINE_WORDS) % PORT_WORDS] ?
                        (uint32_t)(req_reg.write_beat >> (((i % PORT_WORDS) * 32))) :
                        (uint32_t)(axi_out_selected_resp_comb_func().r.data >> ((((i % LINE_WORDS) % PORT_WORDS) * 32)))) :
                 (req_reg.write && req_word_comb_func() == (i % LINE_WORDS)) ? fill_write_word_comb_func() :
                 (req_reg.write && ((uint32_t)req_reg.addr & 3u) != 0 && req_word_comb_func() + 1 == (i % LINE_WORDS)) ? fill_write_next_word_comb_func() :
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
                ((state_reg == ST_LOOKUP || state_reg == ST_CROSS_WRITE_LOOKUP) && req_reg.write && hit_comb_func() && hit_way_comb_func() == way);
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
            if (active_read_comb_func() || active_write_comb_func()) {
                if (trace_active_line) {
                    std::print("trace-l2 cycle={} accept addr={:08x} rd={} wr={} wdata={:08x} mask={:02x} slave={} dport={} victim={}\n",
                        _system_clock, active_addr_comb_func(), active_read_comb_func(), active_write_comb_func(),
                        active_write_data_comb_func(), active_write_mask_comb_func(), active_is_slave_comb_func(),
                        active_is_d_comb_func(), (uint32_t)victim_reg);
                }
                req_reg._next.addr = active_addr_comb_func();
                req_reg._next.write_data = active_write_data_comb_func();
                req_reg._next.write_beat = active_write_beat_comb_func();
                req_reg._next.write_mask = active_write_mask_comb_func();
                req_reg._next.write_strobe = active_write_strobe_comb_func();
                req_reg._next.write_word_mask = active_write_word_mask_comb_func();
                req_reg._next.read = active_read_comb_func();
                req_reg._next.write = active_write_comb_func();
                req_reg._next.port = active_is_d_comb_func();
                req_reg._next.from_slave = active_is_slave_comb_func();
                req_reg._next.slave_index = 0;
                for (i = 0; i < MEM_PORTS; ++i) {
                    if (active_is_slave_comb_func() && active_slave_index_comb_func() == i) {
                        // Store the loop winner directly. This avoids cpphdl
                        // reusing an earlier clog2(MEM_PORTS) cast when the
                        // selected slave index later has to address 8 ports.
                        req_reg._next.slave_index = i;
                        req_reg._next.slave_id = slave_write_pending_comb_func() ?
                            (slave_aw_reg[i].valid ? slave_aw_reg[i].id : axi_in[i].awid_in()) :
                            axi_in[i].arid_in();
                        if (slave_write_pending_comb_func()) {
                            slave_aw_reg._next[i].valid = false;
                        }
                    }
                }
                // ST_READ samples tag/data arrays from the latched request.
                // ST_LOOKUP then consumes those registered RAM outputs.
                state_reg._next = active_cross_line_read_comb_func() ? ST_CROSS_AR0 : ST_READ;
            }
        }
        else if (state_reg == ST_READ) {
            state_reg._next = ST_LOOKUP;
        }
        else if (state_reg == ST_LOOKUP) {
            if (!req_addr_in_memory_comb_func()) {
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
            else if (hit_comb_func()) {
                if (trace_req_line) {
                    trace_word0 = (uint32_t)hit_beat_comb_func();
                    trace_word1 = PORT_WORDS > 1 ? (uint32_t)(hit_beat_comb_func() >> 32) : 0;
                    std::print("trace-l2 cycle={} lookup-hit addr={:08x} rd={} wr={} way={} word={} hit_word={:08x} beat0={:08x} beat1={:08x} wdata={:08x} mask={:02x}\n",
                        _system_clock, (uint32_t)req_reg.addr, (bool)req_reg.read, (bool)req_reg.write,
                        (uint32_t)hit_way_comb_func(), (uint32_t)req_word_comb_func(), hit_word_comb_func(),
                        trace_word0, trace_word1, (uint32_t)req_reg.write_data, (uint32_t)req_reg.write_mask);
                }
                if (req_reg.from_slave) {
                    for (i = 0; i < MEM_PORTS; ++i) {
                        if (req_reg.slave_index == i) {
                            if (req_reg.read) {
                                send_slave_read_response(i, req_reg.slave_id, hit_beat_comb_func());
                            }
                            if (req_reg.write && !req_cross_line_write_comb_func()) {
                                send_slave_write_response(i, req_reg.slave_id);
                            }
                        }
                    }
                }
                else if (req_reg.read) {
                    last_data_reg._next = 0;
                }
                if (!req_reg.from_slave && req_reg.read) {
                    if (req_cross_beat_read_comb_func()) {
                        last_data_reg._next = 0;
                        last_data_reg._next.bits(31, 0) = hit_word_comb_func();
                    }
                    else {
                        last_data_reg._next = hit_beat_comb_func();
                    }
                }
                if (req_cross_line_write_comb_func()) {
                    // Finish the part of an unaligned store that spills into the first word of the next line.
                    req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                    req_reg._next.write_data = cross_write_data_comb_func();
                    req_reg._next.write_mask = cross_write_mask_comb_func();
                    req_reg._next.write_strobe = active_write_strobe_comb_func();
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
                        (uint32_t)victim_reg, evict_valid_comb_func(), evict_dirty_comb_func(), (uint32_t)evict_tag_comb_func());
                }
                fill_way_reg._next = victim_reg;
                fill_beat_reg._next = 0;
                evict_beat_reg._next = 0;
                evict_tag_reg._next = evict_tag_comb_func();
                evict_line_reg._next = evict_line_snapshot_comb_func();
                state_reg._next = (!req_reg.from_slave && req_cross_beat_read_comb_func()) ? ST_CROSS_AR0 :
                    ((evict_valid_comb_func() && evict_dirty_comb_func()) ? ST_EVICT_AW : ST_AXI_AR);
            }
        }
        else if (state_reg == ST_CROSS_WRITE_LOOKUP) {
            if (!req_addr_in_memory_comb_func()) {
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
            else if (hit_comb_func()) {
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
                evict_tag_reg._next = evict_tag_comb_func();
                evict_line_reg._next = evict_line_snapshot_comb_func();
                state_reg._next = (evict_valid_comb_func() && evict_dirty_comb_func()) ? ST_EVICT_AW : ST_AXI_AR;
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
                        trace_word0, trace_word1, (uint32_t)evict_way_comb_func());
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
                        trace_word0, trace_word1, (uint32_t)req_word_comb_func(), (uint32_t)req_beat_comb_func());
                }
                if (!req_reg.from_slave && req_reg.read && fill_beat_reg == req_beat_comb_func()) {
                    last_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                }
                if (req_reg.from_slave && req_reg.read && fill_beat_reg == req_beat_comb_func()) {
                    // Do not answer from hit_beat_comb_func() after the final fill beat:
                    // that path can observe stale RAM output for the requested beat.
                    slave_fill_data_reg._next = axi_out_selected_resp_comb_func().r.data;
                }
                if (fill_beat_reg == LINE_BEATS - 1) {
                    // Final fill beat commits the line; a spillover store then re-enters lookup for the next line.
                    victim_reg._next = (victim_reg == WAYS - 1) ? 0 : victim_reg + 1;
                    if (req_cross_line_write_comb_func()) {
                        req_reg._next.addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + CACHE_LINE_SIZE;
                        req_reg._next.write_data = cross_write_data_comb_func();
                        req_reg._next.write_mask = cross_write_mask_comb_func();
                        req_reg._next.write_strobe = active_write_strobe_comb_func();
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
                                            (fill_beat_reg == req_beat_comb_func()) ?
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
