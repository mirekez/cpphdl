#pragma once

#include "L2CacheState.h"

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1, size_t CPU_PORTS = 1>
// Selects and captures CPU/AXI input requests, address decode, masks, and cross-line request properties.
class L2CacheRequest : public L2CacheState<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>
{
protected:
    using Base = L2CacheState<CACHE_SIZE, PORT_BITWIDTH, CACHE_LINE_SIZE, WAYS, ADDR_BITS, MEM_ADDR_BITS, MEM_PORTS, CPU_PORTS>;
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
    using Base::SETS;
    using Base::SET_BITS;
    using Base::LINE_BITS;
    using Base::req_reg;
    using Base::cpu_rr_reg;
    using Base::response_reg;
    using Base::slave_aw_reg;
    using Base::slave_aw_seen_reg;
    using Base::slave_ar_seen_reg;

    // Derive AW and AR novelty together so every request/ready consumer applies
    // the same sticky-valid replay rule for all external ports.
    _LAZY_COMB(slave_request_novelty_comb, L2AxiRequestNoveltyComb)
        uint32_t index;
        slave_request_novelty_comb = {};
        for (index = 0; index < MEM_PORTS; ++index) {
            slave_request_novelty_comb.aw[index] = !slave_aw_seen_reg[index].valid ||
                slave_aw_seen_reg[index].addr != axi_in[index].awaddr_in() ||
                slave_aw_seen_reg[index].id != axi_in[index].awid_in();
            slave_request_novelty_comb.ar[index] = !slave_ar_seen_reg[index].valid ||
                slave_ar_seen_reg[index].addr != axi_in[index].araddr_in() ||
                slave_ar_seen_reg[index].id != axi_in[index].arid_in();
        }
        return slave_request_novelty_comb;
    }

    // Arbitrate one live request and derive its complete payload once. External
    // AXI writes retain priority over AXI reads, then D precedes I.
    _LAZY_COMB(active_request_comb, L2ActiveRequestComb)
        uint32_t port_index;
        uint32_t cpu_index;
        size_t byte_index;
        size_t word_index;
        uint32_t selected_slave;
        uint32_t selected_cpu;
        uint32_t candidate_cpu;
        uint32_t slave_addr;
        uint32_t lane;
        uint32_t byte;
        uint32_t word;
        bool slave_write_pending;
        bool slave_read_pending;
        bool cpu_request_pending;

        active_request_comb = {};
        selected_slave = 0;
        selected_cpu = 0;
        candidate_cpu = 0;
        slave_addr = 0;
        lane = 0;
        byte = 0;
        word = 0;
        slave_write_pending = false;
        slave_read_pending = false;
        cpu_request_pending = false;

        for (port_index = 0; port_index < MEM_PORTS; ++port_index) {
            if (((slave_aw_reg[port_index].valid && axi_in[port_index].wvalid_in()) ||
                 (axi_in[port_index].awvalid_in() && slave_request_novelty_comb_func().aw[port_index] &&
                  axi_in[port_index].wvalid_in())) &&
                (!response_reg[port_index].b.valid || axi_in[port_index].bready_in())) {
                slave_write_pending = true;
            }
            if (axi_in[port_index].arvalid_in() && slave_request_novelty_comb_func().ar[port_index] &&
                (!response_reg[port_index].r.valid || axi_in[port_index].rready_in())) {
                slave_read_pending = true;
            }
        }
        for (port_index = 0; port_index < MEM_PORTS; ++port_index) {
            if (!slave_write_pending && axi_in[port_index].arvalid_in() &&
                slave_request_novelty_comb_func().ar[port_index] &&
                (!response_reg[port_index].r.valid || axi_in[port_index].rready_in())) {
                selected_slave = port_index;
            }
            if (((slave_aw_reg[port_index].valid && axi_in[port_index].wvalid_in()) ||
                 (axi_in[port_index].awvalid_in() && slave_request_novelty_comb_func().aw[port_index] &&
                  axi_in[port_index].wvalid_in())) &&
                (!response_reg[port_index].b.valid || axi_in[port_index].bready_in())) {
                selected_slave = port_index;
            }
        }

        // Select one CPU pair deterministically. Data traffic has priority over
        // instruction traffic inside that pair, matching the original one-CPU contract.
        for (cpu_index = 0; cpu_index < CPU_PORTS; ++cpu_index) {
            candidate_cpu = ((uint32_t)cpu_rr_reg + cpu_index) % CPU_PORTS;
            if (!cpu_request_pending && (d_mem_in[candidate_cpu].write_in() || d_mem_in[candidate_cpu].read_in() ||
                i_mem_in[candidate_cpu].write_in() || i_mem_in[candidate_cpu].read_in())) {
                selected_cpu = candidate_cpu;
                cpu_request_pending = true;
            }
        }

        active_request_comb.request.from_slave = slave_write_pending || slave_read_pending;
        active_request_comb.request.cpu_index = (u<3>)selected_cpu;
        active_request_comb.request.port = !active_request_comb.request.from_slave &&
            cpu_request_pending && (d_mem_in[selected_cpu].write_in() || d_mem_in[selected_cpu].read_in());
        active_request_comb.request.read =
            (active_request_comb.request.from_slave && !slave_write_pending) ||
            (!active_request_comb.request.from_slave && cpu_request_pending && d_mem_in[selected_cpu].read_in()) ||
            (!active_request_comb.request.from_slave && cpu_request_pending &&
             !d_mem_in[selected_cpu].write_in() && !d_mem_in[selected_cpu].read_in() &&
             i_mem_in[selected_cpu].read_in());
        active_request_comb.request.write =
            (active_request_comb.request.from_slave && slave_write_pending) ||
            (!active_request_comb.request.from_slave && cpu_request_pending && d_mem_in[selected_cpu].write_in()) ||
            (!active_request_comb.request.from_slave && cpu_request_pending &&
             !d_mem_in[selected_cpu].read_in() && !d_mem_in[selected_cpu].write_in() &&
             i_mem_in[selected_cpu].write_in());
        active_request_comb.request.addr = active_request_comb.request.port ?
            d_mem_in[selected_cpu].addr_in() : i_mem_in[selected_cpu].addr_in();
        active_request_comb.request.write_data = active_request_comb.request.port ?
            d_mem_in[selected_cpu].write_data_in() : i_mem_in[selected_cpu].write_data_in();
        active_request_comb.request.write_mask = active_request_comb.request.from_slave ? (uint8_t)0xf :
            (active_request_comb.request.port ? d_mem_in[selected_cpu].write_mask_in() : i_mem_in[selected_cpu].write_mask_in());
        active_request_comb.request.cache_disable = !active_request_comb.request.from_slave &&
            (active_request_comb.request.port ? d_mem_in[selected_cpu].cache_disable_in() :
                i_mem_in[selected_cpu].cache_disable_in());
        active_request_comb.request.slave_index = selected_slave;

        for (port_index = 0; port_index < MEM_PORTS; ++port_index) {
            if (active_request_comb.request.from_slave && selected_slave == port_index) {
                slave_addr = slave_write_pending ?
                    (slave_aw_reg[port_index].valid ? (uint32_t)slave_aw_reg[port_index].addr :
                        (uint32_t)axi_in[port_index].awaddr_in()) :
                    (uint32_t)axi_in[port_index].araddr_in();
                // Internal AXI masters may present RAM-local offsets; CPU ports
                // always retain their full physical addresses.
                active_request_comb.request.addr = slave_addr < memory_base_in() ?
                    slave_addr + memory_base_in() : slave_addr;
                active_request_comb.request.slave_id = slave_write_pending ?
                    (slave_aw_reg[port_index].valid ? slave_aw_reg[port_index].id :
                        axi_in[port_index].awid_in()) :
                    axi_in[port_index].arid_in();
                if (slave_write_pending) {
                    lane = ((slave_aw_reg[port_index].valid ?
                        (uint32_t)slave_aw_reg[port_index].addr :
                        (uint32_t)axi_in[port_index].awaddr_in()) % PORT_BYTES) / 4u;
                    active_request_comb.request.write_data =
                        (uint32_t)(axi_in[port_index].wdata_in() >> (lane * 32u));
                    active_request_comb.request.write_beat = axi_in[port_index].wdata_in();
                    active_request_comb.request.write_strobe = axi_in[port_index].wstrb_in();
                }
            }
        }

        if (!active_request_comb.request.from_slave) {
            byte = (uint32_t)active_request_comb.request.addr % 4u;
            word = ((uint32_t)active_request_comb.request.addr % PORT_BYTES) / 4u;
            for (byte_index = 0; byte_index < 4; ++byte_index) {
                if ((active_request_comb.request.write_mask & (1u << byte_index)) != 0 &&
                    word * 4u + byte + byte_index < PORT_BYTES) {
                    active_request_comb.request.write_strobe[word * 4u + byte + byte_index] = 1;
                }
            }
        }
        for (word_index = 0; word_index < PORT_WORDS; ++word_index) {
            for (byte_index = 0; byte_index < 4; ++byte_index) {
                if (active_request_comb.request.write_strobe[word_index * 4u + byte_index]) {
                    active_request_comb.request.write_word_mask[word_index] = 1;
                }
            }
        }

        active_request_comb.set = ((uint32_t)active_request_comb.request.addr >> LINE_BITS) & (SETS - 1);
        byte = (uint32_t)active_request_comb.request.addr & 3u;
        word = ((uint32_t)active_request_comb.request.addr >> 2) & (LINE_WORDS - 1);
        active_request_comb.valid = active_request_comb.request.read || active_request_comb.request.write;
        active_request_comb.cross_line_read = active_request_comb.request.read &&
            !active_request_comb.request.from_slave && !active_request_comb.request.port &&
            byte != 0 && word == LINE_WORDS - 1;
        return active_request_comb;
    }

    // Decode every property derived from req_reg in one comb so tag, RAM, fill,
    // and cross-line consumers observe the same registered address interpretation.
    _LAZY_COMB(request_geometry_comb, L2RequestGeometryComb)
        uint32_t byte;
        uint32_t word;
        uint32_t local;
        uint32_t size;
        uint32_t i;

        request_geometry_comb = {};
        byte = (uint32_t)req_reg.addr & 3u;
        word = ((uint32_t)req_reg.addr >> 2) & (LINE_WORDS - 1);
        local = (uint32_t)req_reg.addr - memory_base_in();
        size = memory_size_in();
        request_geometry_comb.set = ((uint32_t)req_reg.addr >> LINE_BITS) & (SETS - 1);
        request_geometry_comb.word = word;
        request_geometry_comb.beat =
            ((uint32_t)req_reg.addr & (CACHE_LINE_SIZE - 1)) / PORT_BYTES;
        request_geometry_comb.tag = (uint32_t)req_reg.addr >> (LINE_BITS + SET_BITS);
        request_geometry_comb.cross_beat_read = req_reg.read && !req_reg.from_slave &&
            byte != 0 && (((uint32_t)req_reg.addr % PORT_BYTES) / 4u) + 1 >= PORT_WORDS;
        request_geometry_comb.cross_write_data = byte == 0 ? (uint32_t)0 :
            (uint32_t)req_reg.write_data >> (32 - byte * 8u);
        for (i = 0; i < 4; ++i) {
            if ((req_reg.write_mask & (1u << i)) && i + byte >= 4) {
                request_geometry_comb.cross_write_mask |= 1u << (i + byte - 4);
                if (req_reg.write && byte != 0 && word == LINE_WORDS - 1) {
                    request_geometry_comb.cross_line_write = true;
                }
            }
        }
        request_geometry_comb.addr_in_memory =
            (uint32_t)req_reg.addr >= memory_base_in() && size != 0 && local < size;
        return request_geometry_comb;
    }

    // Full AXI read address for normal fills, MMIO reads, or the two halves of a cross-line read.
};
