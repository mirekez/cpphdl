#pragma once

#include "L2CacheState.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Selects and captures CPU/AXI input requests, address decode, masks, and cross-line request properties.
class L2CacheRequest : public L2CacheState<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>
{
protected:
    using Base = L2CacheState<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS>;
public:
    using Base::i_mem_in;
    using Base::d_mem_in;
    using Base::memory_base_in;
    using Base::memory_size_in;
    using Base::axi_in;

protected:
    using Base::LINE_WORDS;
    using Base::PORT_BYTES;
    using Base::PORT_WORDS;
    using Base::SET_BITS;
    using Base::LINE_BITS;
    using Base::req_reg;
    using Base::slave_b_reg;
    using Base::slave_r_reg;
    using Base::slave_aw_reg;

    _LAZY_COMB(slave_write_pending_comb, bool)
        size_t i;
        slave_write_pending_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (((slave_aw_reg[i].valid && axi_in[i].wvalid_in()) ||
                 (axi_in[i].awvalid_in() && axi_in[i].wvalid_in())) && !slave_b_reg[i].valid) {
                slave_write_pending_comb = true;
            }
        }
        return slave_write_pending_comb;
    }

    // True when any external AXI master is offering a one-beat read.
    _LAZY_COMB(slave_read_pending_comb, bool)
        size_t i;
        slave_read_pending_comb = false;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (axi_in[i].arvalid_in() && !slave_r_reg[i].valid) {
                slave_read_pending_comb = true;
            }
        }
        return slave_read_pending_comb;
    }

    // Lowest-numbered external AXI slave port with a pending request.
    _LAZY_COMB(active_slave_index_comb, u<clog2(MEM_PORTS)>)
        size_t i;
        active_slave_index_comb = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (!slave_write_pending_comb_func() && axi_in[i].arvalid_in() && !slave_r_reg[i].valid) {
                active_slave_index_comb = i;
            }
            if (((slave_aw_reg[i].valid && axi_in[i].wvalid_in()) ||
                 (axi_in[i].awvalid_in() && axi_in[i].wvalid_in())) && !slave_b_reg[i].valid) {
                active_slave_index_comb = i;
            }
        }
        return active_slave_index_comb;
    }

    // External AXI masters arbitrate between D-cache and I-cache, sharing one coherent L2 tag/data RAM.
    _LAZY_COMB(active_is_slave_comb, bool)
        return active_is_slave_comb = slave_write_pending_comb_func() || slave_read_pending_comb_func();
    }

    // Port arbitration: external AXI masters share the coherent L2 before CPU data/instruction ports.
    _LAZY_COMB(active_is_d_comb, bool)
        return active_is_d_comb = !active_is_slave_comb_func() && (d_mem_in.write_in() || d_mem_in.read_in());
    }

    // Active request read flag after data/instruction arbitration.
    _LAZY_COMB(active_read_comb, bool)
        return active_read_comb = (active_is_slave_comb_func() && !slave_write_pending_comb_func()) ||
            (!active_is_slave_comb_func() && d_mem_in.read_in()) ||
            (!d_mem_in.write_in() && !d_mem_in.read_in() && !slave_write_pending_comb_func() && !slave_read_pending_comb_func() && i_mem_in.read_in());
    }

    // Active request write flag after data/instruction arbitration.
    _LAZY_COMB(active_write_comb, bool)
        return active_write_comb = (active_is_slave_comb_func() && slave_write_pending_comb_func()) ||
            (!active_is_slave_comb_func() && d_mem_in.write_in()) ||
            (!d_mem_in.read_in() && !d_mem_in.write_in() && !slave_write_pending_comb_func() && !slave_read_pending_comb_func() && i_mem_in.write_in());
    }

    // Address of the currently selected input port.
    _LAZY_COMB(active_addr_comb, uint32_t)
        size_t port_index;
        uint32_t slave_addr;
        active_addr_comb = active_is_d_comb_func() ? d_mem_in.addr_in() : i_mem_in.addr_in();
        slave_addr = active_addr_comb;
        for (port_index = 0; port_index < MEM_PORTS; ++port_index) {
            if (active_is_slave_comb_func() && active_slave_index_comb_func() == port_index) {
                slave_addr = slave_write_pending_comb_func() ?
                    (slave_aw_reg[port_index].valid ? (uint32_t)slave_aw_reg[port_index].addr : (uint32_t)axi_in[port_index].awaddr_in()) :
                    (uint32_t)axi_in[port_index].araddr_in();
                // External AXI masters connected inside the SoC use local RAM
                // offsets because their top-level address port is sized to the
                // modeled memory window. CPU ports use full physical addresses.
                active_addr_comb = slave_addr < memory_base_in() ? slave_addr + memory_base_in() : slave_addr;
            }
        }
        return active_addr_comb;
    }

    // Write data of the currently selected input port.
    _LAZY_COMB(active_write_data_comb, uint32_t)
        size_t port_index;
        uint32_t lane;
        lane = 0;
        active_write_data_comb = active_is_d_comb_func() ? d_mem_in.write_data_in() : i_mem_in.write_data_in();
        for (port_index = 0; port_index < MEM_PORTS; ++port_index) {
            if (active_is_slave_comb_func() && slave_write_pending_comb_func() && active_slave_index_comb_func() == port_index) {
                lane = ((slave_aw_reg[port_index].valid ? (uint32_t)slave_aw_reg[port_index].addr : (uint32_t)axi_in[port_index].awaddr_in()) % PORT_BYTES) / 4u;
                active_write_data_comb = (uint32_t)(axi_in[port_index].wdata_in() >> (lane * 32u));
            }
        }
        return active_write_data_comb;
    }

    // Full write beat supplied by an external AXI master. The local CPU/L1
    // ports still carry 32-bit stores through req_reg.write_data/mask.
    _LAZY_COMB(active_write_beat_comb, logic<PORT_BITWIDTH>)
        size_t i;
        active_write_beat_comb = 0;
        for (i = 0; i < MEM_PORTS; ++i) {
            if (active_is_slave_comb_func() && slave_write_pending_comb_func() &&
                active_slave_index_comb_func() == i) {
                active_write_beat_comb = axi_in[i].wdata_in();
            }
        }
        return active_write_beat_comb;
    }

    // Write byte mask of the currently selected input port.
    _LAZY_COMB(active_write_mask_comb, uint8_t)
        return active_write_mask_comb = active_is_slave_comb_func() ? (uint8_t)0xf :
            (active_is_d_comb_func() ? d_mem_in.write_mask_in() : i_mem_in.write_mask_in());
    }

    _LAZY_COMB(active_write_strobe_comb, logic<PORT_BITWIDTH / 8>)
        size_t i;
        uint32_t byte;
        uint32_t word;
        active_write_strobe_comb = 0;
        byte = 0;
        word = 0;
        if (active_is_slave_comb_func()) {
            for (i = 0; i < MEM_PORTS; ++i) {
                if (slave_write_pending_comb_func() && active_slave_index_comb_func() == i) {
                    active_write_strobe_comb = axi_in[i].wstrb_in();
                }
            }
        }
        else {
            byte = active_addr_comb_func() % 4u;
            word = (active_addr_comb_func() % PORT_BYTES) / 4u;
            for (i = 0; i < 4; ++i) {
                if ((active_write_mask_comb_func() & (1u << i)) != 0 && word * 4u + byte + i < PORT_BYTES) {
                    active_write_strobe_comb[word * 4u + byte + i] = 1;
                }
            }
        }
        return active_write_strobe_comb;
    }

    _LAZY_COMB(active_write_word_mask_comb, logic<PORT_BITWIDTH / 32>)
        size_t i;
        size_t word;
        active_write_word_mask_comb = 0;
        for (word = 0; word < PORT_WORDS; ++word) {
            for (i = 0; i < 4; ++i) {
                if (active_write_strobe_comb_func()[word * 4 + i]) {
                    active_write_word_mask_comb[word] = 1;
                }
            }
        }
        return active_write_word_mask_comb;
    }

    // Set index of the registered request.
    _LAZY_COMB(req_set_comb, u<clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS)>)
        return req_set_comb = (uint32_t)req_reg.addr >> LINE_BITS;
    }

    // Set index of the currently selected input request.
    _LAZY_COMB(active_set_comb, u<clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS)>)
        return active_set_comb = active_addr_comb_func() >> LINE_BITS;
    }

    // 32-bit word index inside the registered cache line.
    _LAZY_COMB(req_word_comb, u<clog2(CACHE_LINE_SIZE / 4)>)
        return req_word_comb = ((uint32_t)req_reg.addr >> 2) & (LINE_WORDS - 1);
    }

    // PORT_BITWIDTH beat index inside the registered cache line.
    _LAZY_COMB(req_beat_comb, u<((CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)) <= 1 ? 1 : clog2(CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)))>)
        return req_beat_comb = ((uint32_t)req_reg.addr & (CACHE_LINE_SIZE - 1)) / PORT_BYTES;
    }

    // Tag bits of the registered request.
    _LAZY_COMB(req_tag_comb, u<ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE)>)
        return req_tag_comb = (uint32_t)req_reg.addr >> (LINE_BITS + SET_BITS);
    }

    // True for an unaligned read whose 32-bit result crosses the current memory beat.
    _LAZY_COMB(req_cross_beat_read_comb, bool)
        uint32_t byte;
        uint32_t word;
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr % PORT_BYTES) / 4u;
        return req_cross_beat_read_comb = req_reg.read && !req_reg.from_slave &&
            byte != 0 && word + 1 >= PORT_WORDS;
    }

    // True for an unaligned read whose 32-bit result crosses the cache-line boundary.
    _LAZY_COMB(active_cross_line_read_comb, bool)
        uint32_t byte;
        uint32_t word;
        byte = active_addr_comb_func() & 3u;
        word = (active_addr_comb_func() >> 2) & (LINE_WORDS - 1);
        active_cross_line_read_comb = active_read_comb_func() && !active_is_slave_comb_func() &&
            !active_is_d_comb_func() && byte != 0 && word == LINE_WORDS - 1;
        return active_cross_line_read_comb;
    }

    // True for a write with bytes that spill from the final line word into the next line.
    _LAZY_COMB(req_cross_line_write_comb, bool)
        uint32_t byte;
        uint32_t word;
        uint32_t i;
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr >> 2) & (LINE_WORDS - 1);
        req_cross_line_write_comb = false;
        if (req_reg.write && byte != 0 && word == LINE_WORDS - 1) {
            for (i = 0; i < 4; ++i) {
                if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                    req_cross_line_write_comb = true;
                }
            }
        }
        return req_cross_line_write_comb;
    }

    // Write data shifted down for the second word/line of a cross-line write.
    _LAZY_COMB(cross_write_data_comb, uint32_t)
        uint32_t byte;
        byte = (uint32_t)req_reg.addr & 3u;
        return cross_write_data_comb = byte == 0 ? (uint32_t)0 : (uint32_t)req_reg.write_data >> (32 - byte * 8);
    }

    // Byte mask remapped for bytes landing in the second word/line of a cross-line write.
    _LAZY_COMB(cross_write_mask_comb, uint8_t)
        uint32_t byte;
        uint32_t i;
        byte = (uint32_t)req_reg.addr & 3u;
        cross_write_mask_comb = 0;
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                cross_write_mask_comb |= 1u << (i + byte - 4);
            }
        }
        return cross_write_mask_comb;
    }

    // Bounds check for the currently selected input address against the exposed memory window.
    _LAZY_COMB(addr_in_memory_comb, bool)
        uint32_t addr;
        uint32_t local;
        uint32_t size;
        addr = active_addr_comb_func();
        local = addr - memory_base_in();
        size = memory_size_in();
        addr_in_memory_comb = addr >= memory_base_in() && size != 0 && local < size;
        return addr_in_memory_comb;
    }

    // Bounds check for the registered request address against the exposed memory window.
    _LAZY_COMB(req_addr_in_memory_comb, bool)
        uint32_t addr;
        uint32_t local;
        uint32_t size;
        addr = req_reg.addr;
        local = addr - memory_base_in();
        size = memory_size_in();
        req_addr_in_memory_comb = addr >= memory_base_in() && size != 0 && local < size;
        return req_addr_in_memory_comb;
    }

    // Full AXI read address for normal fills, MMIO reads, or the two halves of a cross-line read.
};
