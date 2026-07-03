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

    _LAZY_COMB(axi_araddr_full_comb, uint32_t)
        uint32_t line_addr;
        line_addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)fill_beat_reg * PORT_BYTES);
        if (state_reg == ST_IO_AR || state_reg == ST_IO_R) {
            line_addr = req_reg.addr;
        }
        if (state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_R0) {
            line_addr = ((uint32_t)req_reg.addr & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)req_beat_comb_func() * PORT_BYTES);
        }
        if (state_reg == ST_CROSS_AR1 || state_reg == ST_CROSS_R1) {
            line_addr = ((uint32_t)req_reg.addr & ~(uint32_t)(PORT_BYTES - 1)) + PORT_BYTES;
        }
        return axi_araddr_full_comb = line_addr;
    }

    // AXI read address relative to memory_base_in before memory-port selection.
    _LAZY_COMB(axi_araddr_total_local_comb, uint32_t)
        return axi_araddr_total_local_comb = axi_araddr_full_comb_func() - memory_base_in();
    }

    // Memory/device port selected by cumulative region sizes for AXI reads.
    _LAZY_COMB(axi_ar_sel_comb, u<clog2(MEM_PORTS)>)
        size_t i;
        uint64_t base;
        uint64_t local;
        local = axi_araddr_total_local_comb_func();
        base = 0;
        axi_ar_sel_comb = MEM_PORTS - 1;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (local >= base && local < base + mem_region_size_in[i]()) {
                axi_ar_sel_comb = i;
            }
            base += mem_region_size_in[i]();
        }
        return axi_ar_sel_comb;
    }

    // Region base offset inside the full memory window for the selected read port.
    _LAZY_COMB(axi_ar_region_base_comb, uint32_t)
        size_t i;
        uint64_t base;
        base = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (i < axi_ar_sel_comb_func()) {
                base += mem_region_size_in[i]();
            }
        }
        return axi_ar_region_base_comb = base;
    }

    // Local AXI read address presented to the selected memory/device port.
    _LAZY_COMB(axi_araddr_local_comb, u<MEM_ADDR_BITS>)
        // Ports are now disjoint cumulative regions, not interleaved banks, so
        // keep the full byte address inside the selected region.
        return axi_araddr_local_comb = (u<MEM_ADDR_BITS>)((uint64_t)(axi_araddr_total_local_comb_func() - axi_ar_region_base_comb_func()) & MEM_ADDR_MASK64);
    }

    // AXI read address valid for normal fills and cross-line read beats.
    _LAZY_COMB(axi_arvalid_comb, bool)
        return axi_arvalid_comb = req_addr_in_memory_comb_func() &&
            (state_reg == ST_AXI_AR || state_reg == ST_CROSS_AR0 || state_reg == ST_CROSS_AR1 || state_reg == ST_IO_AR);
    }

    // AXI read data ready while waiting for a normal fill, cross-line beat, or uncached IO read.
    _LAZY_COMB(axi_rready_comb, bool)
        return axi_rready_comb = state_reg == ST_AXI_R || state_reg == ST_CROSS_R0 || state_reg == ST_CROSS_R1 || state_reg == ST_IO_R;
    }

    // Ready from the currently selected AXI read-address port.
    _LAZY_COMB(axi_arready_selected_comb, bool)
        size_t i;
        axi_arready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_arready_selected_comb = axi_out[i].arready_out();
            }
        }
        return axi_arready_selected_comb;
    }

    // Valid from the currently selected AXI read-data port.
    _LAZY_COMB(axi_rvalid_selected_comb, bool)
        size_t i;
        axi_rvalid_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_rvalid_selected_comb = axi_out[i].rvalid_out();
            }
        }
        return axi_rvalid_selected_comb;
    }

    // Read data from the currently selected AXI memory port.
    _LAZY_COMB(axi_rdata_selected_comb, logic<PORT_BITWIDTH>)
        size_t i;
        axi_rdata_selected_comb = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_ar_sel_comb_func() == i) {
                axi_rdata_selected_comb = axi_out[i].rdata_out();
            }
        }
        return axi_rdata_selected_comb;
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

    // Full AXI writeback address for the current evicted PORT_BITWIDTH beat or MMIO store.
    _LAZY_COMB(axi_awaddr_full_comb, uint32_t)
        uint32_t addr;
        addr = (((uint32_t)evict_tag_reg << (SET_BITS + LINE_BITS)) |
            ((uint32_t)req_set_comb_func() << LINE_BITS)) + ((uint32_t)evict_beat_reg * PORT_BYTES);
        if (state_reg == ST_IO_AW || state_reg == ST_IO_W || state_reg == ST_IO_B) {
            addr = req_reg.addr;
        }
        return axi_awaddr_full_comb = addr;
    }

    // AXI writeback address relative to memory_base_in before memory-port selection.
    _LAZY_COMB(axi_awaddr_total_local_comb, uint32_t)
        return axi_awaddr_total_local_comb = axi_awaddr_full_comb_func() - memory_base_in();
    }

    // Memory/device port selected by cumulative region sizes for AXI writes.
    _LAZY_COMB(axi_aw_sel_comb, u<clog2(MEM_PORTS)>)
        size_t i;
        uint64_t base;
        uint64_t local;
        local = axi_awaddr_total_local_comb_func();
        base = 0;
        axi_aw_sel_comb = MEM_PORTS - 1;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (local >= base && local < base + mem_region_size_in[i]()) {
                axi_aw_sel_comb = i;
            }
            base += mem_region_size_in[i]();
        }
        return axi_aw_sel_comb;
    }

    // Region base offset inside the full memory window for the selected write port.
    _LAZY_COMB(axi_aw_region_base_comb, uint32_t)
        size_t i;
        uint64_t base;
        base = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (i < axi_aw_sel_comb_func()) {
                base += mem_region_size_in[i]();
            }
        }
        return axi_aw_region_base_comb = base;
    }

    // Local AXI write address presented to the selected memory/device port.
    _LAZY_COMB(axi_awaddr_local_comb, u<MEM_ADDR_BITS>)
        // Ports are now disjoint cumulative regions, not interleaved banks, so
        // keep the full byte address inside the selected region.
        return axi_awaddr_local_comb = (u<MEM_ADDR_BITS>)((uint64_t)(axi_awaddr_total_local_comb_func() - axi_aw_region_base_comb_func()) & MEM_ADDR_MASK64);
    }

    // AXI write address valid during dirty-line eviction.
    _LAZY_COMB(axi_awvalid_comb, bool)
        return axi_awvalid_comb = req_addr_in_memory_comb_func() && (state_reg == ST_EVICT_AW || state_reg == ST_IO_AW);
    }

    // AXI write data valid during dirty-line eviction.
    _LAZY_COMB(axi_wvalid_comb, bool)
        return axi_wvalid_comb = req_addr_in_memory_comb_func() && (state_reg == ST_EVICT_W || state_reg == ST_IO_W);
    }

    // Ready from the currently selected AXI write-address port.
    _LAZY_COMB(axi_awready_selected_comb, bool)
        size_t i;
        axi_awready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_awready_selected_comb = axi_out[i].awready_out();
            }
        }
        return axi_awready_selected_comb;
    }

    // Ready from the currently selected AXI write-data port.
    _LAZY_COMB(axi_wready_selected_comb, bool)
        size_t i;
        axi_wready_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_wready_selected_comb = axi_out[i].wready_out();
            }
        }
        return axi_wready_selected_comb;
    }

    // Write response valid from the currently selected AXI port.
    _LAZY_COMB(axi_bvalid_selected_comb, bool)
        size_t i;
        axi_bvalid_selected_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_aw_sel_comb_func() == i) {
                axi_bvalid_selected_comb = axi_out[i].bvalid_out();
            }
        }
        return axi_bvalid_selected_comb;
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

    // Select cache-line eviction data or the single MMIO write beat for AXI W.
    _LAZY_COMB(axi_wdata_comb, logic<PORT_BITWIDTH>)
        return axi_wdata_comb = state_reg == ST_IO_W ? io_write_beat_comb_func() : evict_line_comb_func();
    }

    _LAZY_COMB(axi_wstrb_comb, logic<PORT_BITWIDTH / 8>)
        return axi_wstrb_comb = state_reg == ST_IO_W ? io_write_strobe_comb_func() : ~logic<PORT_BYTES>(0);
    }

    // Associative tag compare for the registered request.
};
