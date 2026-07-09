#pragma once

#include "L2CacheRequest.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Routes refill, eviction, and uncached transactions between the cache controller and memory/device AXI ports.
class L2CacheMemory : public L2CacheRequest<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>
{
protected:
    using Base = L2CacheRequest<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>;
public:
    using Base::memory_base_in;
    using Base::mem_region_size_in;
    using Base::mem_region_uncached_in;
    using Base::axi_out;

protected:
    using Base::LINE_WORDS;
    using Base::PORT_BYTES;
    using Base::PORT_WORDS;
    using Base::SET_BITS;
    using Base::LINE_BITS;
    using Base::TAG_BITS;
    using Base::DATA_BANKS;
    using Base::MEM_ADDR_MASK64;
    using Base::data_q_reg;
    using Base::tag_q_reg;
    using Base::state_reg;
    using Base::req_reg;
    using Base::victim_reg;
    using Base::fill_way_reg;
    using Base::fill_beat_reg;
    using Base::evict_beat_reg;
    using Base::evict_tag_reg;
    using Base::evict_line_reg;
    using Base::req_set_comb_func;
    using Base::req_beat_comb_func;
    using Base::req_addr_in_memory_comb_func;

    // Compute both read and write AXI address routes in one place so later layers consume route fields instead of scalar AXI combs.
    _LAZY_COMB(axi_route_comb, L2AxiRouteComb)
        size_t i;
        uint64_t base;
        uint32_t ar_total_local;
        uint32_t ar_region_base;
        uint32_t aw_total_local;
        uint32_t aw_region_base;

        axi_route_comb.ar_full_addr =
            ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)fill_beat_reg * PORT_BYTES);
        if (state_reg == ST_IO_AR || state_reg == ST_IO_R) {
            axi_route_comb.ar_full_addr = req_reg.addr;
        }
        if (state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_R0) {
            axi_route_comb.ar_full_addr =
                ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)req_beat_comb_func() * PORT_BYTES);
        }
        if (state_reg == ST_CROSS_AR1 || state_reg == ST_CROSS_R1) {
            axi_route_comb.ar_full_addr = ((uint32_t)req_reg.addr & ~(uint32_t)(PORT_BYTES - 1)) + PORT_BYTES;
        }
        ar_total_local = (uint32_t)axi_route_comb.ar_full_addr - memory_base_in();
        base = 0;
        ar_region_base = 0;
        axi_route_comb.ar_sel = MEM_PORTS - 1;
        for (i = 0; i < MEM_PORTS; ++i) {
            if ((uint64_t)ar_total_local >= base && (uint64_t)ar_total_local < base + mem_region_size_in[i]()) {
                axi_route_comb.ar_sel = i;
                ar_region_base = (uint32_t)base;
            }
            base += mem_region_size_in[i]();
        }
        axi_route_comb.ar_local_addr = (uint32_t)(((uint64_t)(ar_total_local - ar_region_base)) & MEM_ADDR_MASK64);

        axi_route_comb.aw_full_addr = (((uint32_t)evict_tag_reg << (SET_BITS + LINE_BITS)) |
            ((uint32_t)req_set_comb_func() << LINE_BITS)) + ((uint32_t)evict_beat_reg * PORT_BYTES);
        if (state_reg == ST_IO_AW || state_reg == ST_IO_W || state_reg == ST_IO_B) {
            axi_route_comb.aw_full_addr = req_reg.addr;
        }
        aw_total_local = (uint32_t)axi_route_comb.aw_full_addr - memory_base_in();
        base = 0;
        aw_region_base = 0;
        axi_route_comb.aw_sel = MEM_PORTS - 1;
        for (i = 0; i < MEM_PORTS; ++i) {
            if ((uint64_t)aw_total_local >= base && (uint64_t)aw_total_local < base + mem_region_size_in[i]()) {
                axi_route_comb.aw_sel = i;
                aw_region_base = (uint32_t)base;
            }
            base += mem_region_size_in[i]();
        }
        axi_route_comb.aw_local_addr = (uint32_t)(((uint64_t)(aw_total_local - aw_region_base)) & MEM_ADDR_MASK64);
        return axi_route_comb;
    }

    // Build the logical AXI master transaction before fanout to the selected memory/device port.
    _LAZY_COMB(axi_out_driver_comb, Axi4Driver<32, 4, 256>)
        axi_out_driver_comb.aw.valid = req_addr_in_memory_comb_func() && (state_reg == ST_EVICT_AW || state_reg == ST_IO_AW);
        axi_out_driver_comb.aw.addr = (u<32>)(uint32_t)axi_route_comb_func().aw_local_addr;
        axi_out_driver_comb.aw.id = (u<4>)0;
        axi_out_driver_comb.w.valid = req_addr_in_memory_comb_func() && (state_reg == ST_EVICT_W || state_reg == ST_IO_W);
        axi_out_driver_comb.w.data = (logic<256>)(state_reg == ST_IO_W ? io_write_beat_comb_func() : evict_line_comb_func());
        axi_out_driver_comb.w.strb = (logic<32>)(state_reg == ST_IO_W ? io_write_strobe_comb_func() : ~logic<PORT_BYTES>(0));
        axi_out_driver_comb.w.last = axi_out_driver_comb.w.valid;
        axi_out_driver_comb.b.ready = axi_route_comb_func().aw_sel < MEM_PORTS;
        axi_out_driver_comb.ar.valid = req_addr_in_memory_comb_func() &&
            (state_reg == ST_AXI_AR || state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_AR1 || state_reg == ST_IO_AR);
        axi_out_driver_comb.ar.addr = (u<32>)(uint32_t)axi_route_comb_func().ar_local_addr;
        axi_out_driver_comb.ar.id = (u<4>)0;
        axi_out_driver_comb.r.ready = state_reg == ST_AXI_R || state_reg == ST_CROSS_R0 || state_reg == ST_CROSS_R1 || state_reg == ST_IO_R;
        return axi_out_driver_comb;
    }

    // Gather the response side of the currently selected AXI memory/device ports into one responder bundle.
    _LAZY_COMB(axi_out_selected_resp_comb, Axi4Responder<4, 256>)
        size_t i;
        axi_out_selected_resp_comb.aw.ready = false;
        axi_out_selected_resp_comb.w.ready = false;
        axi_out_selected_resp_comb.b.valid = false;
        axi_out_selected_resp_comb.b.id = 0;
        axi_out_selected_resp_comb.ar.ready = false;
        axi_out_selected_resp_comb.r.valid = false;
        axi_out_selected_resp_comb.r.data = 0;
        axi_out_selected_resp_comb.r.last = false;
        axi_out_selected_resp_comb.r.id = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if ((uint32_t)axi_route_comb_func().aw_sel == i) {
                axi_out_selected_resp_comb.aw.ready = axi_out[i].awready_out();
                axi_out_selected_resp_comb.w.ready = axi_out[i].wready_out();
                axi_out_selected_resp_comb.b.valid = axi_out[i].bvalid_out();
                axi_out_selected_resp_comb.b.id = axi_out[i].bid_out();
            }
            if ((uint32_t)axi_route_comb_func().ar_sel == i) {
                axi_out_selected_resp_comb.ar.ready = axi_out[i].arready_out();
                axi_out_selected_resp_comb.r.valid = axi_out[i].rvalid_out();
                axi_out_selected_resp_comb.r.data = (logic<256>)axi_out[i].rdata_out();
                axi_out_selected_resp_comb.r.last = axi_out[i].rlast_out();
                axi_out_selected_resp_comb.r.id = axi_out[i].rid_out();
            }
        }
        return axi_out_selected_resp_comb;
    }

    // Way to evict or refill, depending on whether lookup has already chosen fill_way_reg.
    _LAZY_COMB(evict_way_comb, u<(WAYS <= 1 ? 1 : clog2(WAYS))>)
        return evict_way_comb = (state_reg == ST_LOOKUP) ? victim_reg : fill_way_reg;
    }

    // Valid bit of the candidate eviction way.
    _LAZY_COMB(evict_valid_comb, bool)
        bool valid;
        size_t i;
        valid = false;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                valid = (bool)tag_q_reg[i][TAG_BITS + 1];
            }
        }
        return evict_valid_comb = valid;
    }

    // Dirty bit of the candidate eviction way.
    _LAZY_COMB(evict_dirty_comb, bool)
        bool dirty;
        size_t i;
        dirty = false;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                dirty = (bool)tag_q_reg[i][TAG_BITS];
            }
        }
        return evict_dirty_comb = dirty;
    }

    // Tag of the candidate eviction way, used to reconstruct the writeback address.
    _LAZY_COMB(evict_tag_comb, u<ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE)>)
        size_t i;
        evict_tag_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (evict_way_comb_func() == i) {
                evict_tag_comb = (uint64_t)tag_q_reg[i].bits(TAG_BITS - 1, 0);
            }
        }
        return evict_tag_comb;
    }

    // Snapshot the candidate eviction line at the miss lookup cycle.
    _LAZY_COMB(evict_line_snapshot_comb, logic<CACHE_LINE_SIZE * 8>)
        size_t i;
        size_t way;
        size_t word;
        way = 0;
        word = 0;
        evict_line_snapshot_comb = 0;
        for (i = 0; i < DATA_BANKS; ++i) {
            way = i / LINE_WORDS;
            word = i % LINE_WORDS;
            if (evict_way_comb_func() == way) {
                evict_line_snapshot_comb.bits(word * 32 + 31, word * 32) = data_q_reg[i];
            }
        }
        return evict_line_snapshot_comb;
    }

    // Pack the evicted cache way snapshot into the current PORT_BITWIDTH AXI write beat.
    _LAZY_COMB(evict_line_comb, logic<PORT_BITWIDTH>)
        size_t word;
        size_t beat_word;
        word = 0;
        beat_word = 0;
        evict_line_comb = 0;
        for (word = (uint32_t)evict_beat_reg * PORT_WORDS;
             word < ((uint32_t)evict_beat_reg + 1u) * PORT_WORDS && word < LINE_WORDS;
             ++word) {
            beat_word = word - (uint32_t)evict_beat_reg * PORT_WORDS;
            evict_line_comb.bits(beat_word * 32 + 31, beat_word * 32) =
                evict_line_reg.bits(word * 32 + 31, word * 32);
        }
        return evict_line_comb;
    }

    // Configured uncached flag for the region containing the registered request.
    _LAZY_COMB(req_uncached_region_comb, bool)
        uint32_t local;
        uint64_t base;
        size_t i;
        local = (uint32_t)req_reg.addr - memory_base_in();
        base = 0;
        req_uncached_region_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if ((uint64_t)local >= base && (uint64_t)local < base + mem_region_size_in[i]()) {
                req_uncached_region_comb = mem_region_uncached_in[i]();
            }
            base += mem_region_size_in[i]();
        }
        return req_uncached_region_comb = req_addr_in_memory_comb_func() && req_uncached_region_comb;
    }

    // Write beat for uncached MMIO stores, placing the 32-bit word at its byte lane.
    _LAZY_COMB(io_write_beat_comb, logic<PORT_BITWIDTH>)
        uint32_t byte;
        uint32_t word;
        io_write_beat_comb = 0;
        if (req_reg.from_slave) {
            return io_write_beat_comb = (logic<PORT_BITWIDTH>)req_reg.write_beat;
        }
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr % PORT_BYTES) / 4u;
        // CPU stores keep the store value in low bits and carry the byte address separately.
        // The AXI-like device bus has no write strobe, so align the value to the addressed byte lane.
        io_write_beat_comb.bits(word * 32 + 31, word * 32) = (uint32_t)req_reg.write_data << (byte * 8u);
        return io_write_beat_comb;
    }

    _LAZY_COMB(io_write_strobe_comb, logic<PORT_BITWIDTH / 8>)
        uint32_t byte;
        uint32_t word;
        size_t i;
        io_write_strobe_comb = 0;
        if (req_reg.from_slave) {
            return io_write_strobe_comb = (logic<PORT_BYTES>)req_reg.write_strobe;
        }
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr % PORT_BYTES) / 4u;
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) != 0 && word * 4u + byte + i < PORT_BYTES) {
                io_write_strobe_comb[word * 4u + byte + i] = 1;
            }
        }
        return io_write_strobe_comb;
    }

    // Associative tag compare for the registered request.
};
