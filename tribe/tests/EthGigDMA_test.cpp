#include "cpphdl.h"
#include "Axi4.h"
#include "Axi4Ram.h"
#include "net/ethgig/ethgig_phy.h"
#include "net/ethgig/ethgig_pcs.h"
#include "net/ethgig/ethgig_mac.h"
#include "net/ethgig/ethgig_dma.h"
#include "RGMIIVerif.h"

#include <chrono>
#include <print>
#include <vector>

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 64, size_t FIFO_DEPTH = 256>
class EthGigDMATop : public Module
{
public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_out[1];

    _PORT(bool) rgmii_tx_ctl_out;
    _PORT(u<4>) rgmii_txd_out;
    _PORT(bool) rgmii_tx_last_out;
    _PORT(bool) rgmii_rx_ctl_in;
    _PORT(u<4>) rgmii_rxd_in;
    _PORT(bool) rgmii_rx_last_in;
    _PORT(bool) tx_irq_out;
    _PORT(bool) rx_irq_out;
    _PORT(uint32_t) debug_state_out;
    _PORT(bool) debug_rx_ready_out;

private:
    EthGigDMA<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> dma;
    EthGigMAC<FIFO_DEPTH> mac;
    EthGigPCS<FIFO_DEPTH> pcs;
    EthGigPHY phy;

public:
    void _assign()
    {
        mac.tx_valid_in = dma.mac_tx_valid_out;
        mac.tx_data_in = dma.mac_tx_data_out;
        mac.tx_last_in = dma.mac_tx_last_out;
        mac.local_mac_in = _ASSIGN(logic<48>(0x020000000002ull));
        mac.local_ip_in = _ASSIGN((uint32_t)0xc0a8012au);
        mac.local_mask_in = _ASSIGN((uint32_t)0xffffff00u);
        mac.promisc_in = _ASSIGN(false);
        dma.mac_tx_ready_in = mac.tx_ready_out;

        dma.mac_rx_valid_in = mac.rx_valid_out;
        dma.mac_rx_data_in = mac.rx_data_out;
        dma.mac_rx_last_in = mac.rx_last_out;
        mac.rx_ready_in = dma.mac_rx_ready_out;

        pcs.tx_valid_in = mac.pcs_tx_valid_out;
        pcs.tx_data_in = mac.pcs_tx_data_out;
        pcs.tx_last_in = mac.pcs_tx_last_out;
        mac.pcs_tx_ready_in = pcs.tx_ready_out;

        phy.tx_valid_in = pcs.tx_valid_out;
        phy.tx_data_in = pcs.tx_data_out;
        phy.tx_last_in = pcs.tx_last_out;
        pcs.tx_ready_in = phy.tx_ready_out;

        pcs.rx_valid_in = phy.rx_valid_out;
        pcs.rx_data_in = phy.rx_data_out;
        pcs.rx_last_in = phy.rx_last_out;
        phy.rx_ready_in = pcs.rx_ready_out;

        mac.pcs_rx_valid_in = pcs.rx_valid_out;
        mac.pcs_rx_data_in = pcs.rx_data_out;
        mac.pcs_rx_last_in = pcs.rx_last_out;
        pcs.rx_ready_in = mac.pcs_rx_ready_out;

        rgmii_tx_ctl_out = _ASSIGN(phy.rgmii_tx_ctl_out());
        rgmii_txd_out = _ASSIGN(phy.rgmii_txd_out());
        rgmii_tx_last_out = _ASSIGN(phy.rgmii_tx_last_out());
        phy.rgmii_rx_ctl_in = rgmii_rx_ctl_in;
        phy.rgmii_rxd_in = rgmii_rxd_in;
        phy.rgmii_rx_last_in = rgmii_rx_last_in;
        phy.mdio_mdc_in = _ASSIGN(false);
        phy.mdio_host_oe_in = _ASSIGN(false);
        phy.mdio_host_data_in = _ASSIGN(true);
        tx_irq_out = _ASSIGN(dma.tx_irq_out());
        rx_irq_out = _ASSIGN(dma.rx_irq_out());
        debug_state_out = _ASSIGN(dma.debug_state_out());
        debug_rx_ready_out = _ASSIGN(dma.mac_rx_ready_out());

        dma.__inst_name = "dma";
        mac.__inst_name = "mac";
        pcs.__inst_name = "pcs";
        phy.__inst_name = "phy";
        dma._assign();
        mac._assign();
        pcs._assign();
        phy._assign();

        AXI4_DRIVER_FROM(dma.axi_in, axi_in);
        AXI4_RESPONDER_FROM(axi_in, dma.axi_in);

        AXI4_DRIVER_FROM(axi_out[0], dma.dma_out);
        AXI4_RESPONDER_FROM_LATE(dma.dma_out, axi_out[0]);
    }

    void _work(bool reset)
    {
        dma._work(reset);
        mac._work(reset);
        pcs._work(reset);
        phy._work(reset);
    }

    void _strobe()
    {
        dma._strobe();
        mac._strobe();
        pcs._strobe();
        phy._strobe();
    }
};

template class EthGigDMATop<32, 4, 64, 256>;
template class EthGigDMATop<32, 4, 256, 256>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

template<size_t TEST_DATA_WIDTH>
class TestEthGigDMA : public Module
{
    static constexpr uint32_t DATA_WIDTH = TEST_DATA_WIDTH;
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;
    static constexpr uint32_t TX_DESC = 0x100;
    static constexpr uint32_t RX_DESC = 0x180;
    static constexpr uint32_t TX_DESC2 = 0x1c0;
    static constexpr uint32_t RX_DESC2 = 0x200;
    static constexpr uint32_t TX_BUF = 0x300;
    static constexpr uint32_t TX_BUF2 = 0x340;
    static constexpr uint32_t RX_BUF = 0x400;
    static constexpr uint32_t RX_BUF2 = 0x480;
    static constexpr uint32_t DMA_REG_BASE = 0x100;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    EthGigDMATop<32, 4, DATA_WIDTH, 256> dut;
#endif
    Axi4Ram<32, 4, DATA_WIDTH, 256> ram;
    RGMIIVerifFrontend verif;
    Axi4Driver<32, 4, DATA_WIDTH> cpu = {};
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        AXI4_DRIVER_FROM_DRIVER(dut.axi_in, cpu);
        ram.axi_in.awvalid_in = _ASSIGN(dut.axi_out[0].awvalid_in());
        ram.axi_in.awaddr_in = _ASSIGN(dut.axi_out[0].awaddr_in());
        ram.axi_in.awid_in = _ASSIGN(dut.axi_out[0].awid_in());
        ram.axi_in.wvalid_in = _ASSIGN(dut.axi_out[0].wvalid_in());
        ram.axi_in.wdata_in = _ASSIGN(dut.axi_out[0].wdata_in());
        ram.axi_in.wstrb_in = _ASSIGN(dut.axi_out[0].wstrb_in());
        ram.axi_in.wlast_in = _ASSIGN(dut.axi_out[0].wlast_in());
        ram.axi_in.bready_in = _ASSIGN(dut.axi_out[0].bready_in());
        ram.axi_in.arvalid_in = _ASSIGN(dut.axi_out[0].arvalid_in());
        ram.axi_in.araddr_in = _ASSIGN(dut.axi_out[0].araddr_in());
        ram.axi_in.arid_in = _ASSIGN(dut.axi_out[0].arid_in());
        ram.axi_in.rready_in = _ASSIGN(dut.axi_out[0].rready_in());
        ram.debugen_in = false;
        ram.__inst_name = "ram";
        ram._assign();
        AXI4_RESPONDER_FROM(dut.axi_out[0], ram.axi_in);
        verif.rgmii_tx_ctl_in = _ASSIGN(dut.rgmii_tx_ctl_out());
        verif.rgmii_txd_in = _ASSIGN(dut.rgmii_txd_out());
        verif.rgmii_tx_last_in = _ASSIGN(dut.rgmii_tx_last_out());
        dut.rgmii_rx_ctl_in = _ASSIGN(verif.rgmii_rx_ctl_out());
        dut.rgmii_rxd_in = _ASSIGN(verif.rgmii_rxd_out());
        dut.rgmii_rx_last_in = _ASSIGN(verif.rgmii_rx_last_out());
        dut.__inst_name = "ethgig_dma_top";
        verif.__inst_name = "rgmii_verif";
        dut._assign();
        verif._assign();
#endif
    }

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        (void)reset;
#else
        dut._work(reset);
        ram._work(reset);
        verif._work(reset);
        dut._strobe();
        ram._strobe();
        verif._strobe();
#endif
        ++_system_clock;
    }

    void clear_cpu()
    {
        cpu.aw.valid = false;
        cpu.w.valid = false;
        cpu.b.ready = true;
        cpu.ar.valid = false;
        cpu.r.ready = true;
    }

    void mmio_write(uint32_t addr, uint32_t value)
    {
        clear_cpu();
        cpu.aw.valid = true;
        cpu.aw.addr = addr;
        cpu.aw.id = 0;
        for (uint32_t guard = 0; guard < 1000; ++guard) {
            if (dut.axi_in.awready_out()) {
                cycle(false);
                cpu.aw.valid = false;
                break;
            }
            cycle(false);
        }
        if (cpu.aw.valid) {
            std::print("\nERROR: mmio write address timeout @0x{:08x}\n", addr);
            error = true;
            return;
        }

        cpu.w.valid = true;
        cpu.w.data = 0;
        cpu.w.data.bits(((addr % DATA_BYTES) / 4u) * 32 + 31, ((addr % DATA_BYTES) / 4u) * 32) = value;
        cpu.w.last = true;
        for (uint32_t guard = 0; guard < 1000; ++guard) {
            if (dut.axi_in.wready_out()) {
                cycle(false);
                cpu.w.valid = false;
                break;
            }
            cycle(false);
        }
        if (cpu.w.valid) {
            std::print("\nERROR: mmio write data timeout @0x{:08x}\n", addr);
            error = true;
            return;
        }

        for (uint32_t guard = 0; guard < 1000; ++guard) {
            if (dut.axi_in.bvalid_out()) {
                clear_cpu();
                cycle(false);
                return;
            }
            cycle(false);
        }
        std::print("\nERROR: mmio write response timeout @0x{:08x}\n", addr);
        error = true;
    }

    uint32_t mmio_read(uint32_t addr)
    {
        clear_cpu();
        for (uint32_t guard = 0; guard < 1000; ++guard) {
            cpu.ar.valid = true;
            cpu.ar.addr = addr;
            cpu.ar.id = 0;
            if (dut.axi_in.rvalid_out()) {
                uint32_t value = (uint32_t)dut.axi_in.rdata_out().bits(31, 0);
                clear_cpu();
                cycle(false);
                return value;
            }
            cycle(false);
        }
        std::print("\nERROR: mmio read timeout @0x{:08x}\n", addr);
        error = true;
        return 0;
    }

    void mem_write32(uint32_t addr, uint32_t value)
    {
        uint32_t beat = addr / DATA_BYTES;
        uint32_t lane = (addr % DATA_BYTES) / 4u;
        logic<DATA_WIDTH> data = ram.ram.buffer[beat];
        data.bits(lane * 32 + 31, lane * 32) = value;
        ram.ram.buffer[beat] = data;
        ram.ram.buffer.apply();
    }

    uint32_t mem_read32(uint32_t addr)
    {
        uint32_t beat = addr / DATA_BYTES;
        uint32_t lane = (addr % DATA_BYTES) / 4u;
        return (uint32_t)ram.ram.buffer[beat].bits(lane * 32 + 31, lane * 32);
    }

    void mem_write_packet(uint32_t addr, const std::vector<uint8_t>& packet)
    {
        for (size_t i = 0; i < packet.size(); ++i) {
            uint32_t byte_addr = addr + (uint32_t)i;
            uint32_t beat = byte_addr / DATA_BYTES;
            uint32_t lane = byte_addr % DATA_BYTES;
            logic<DATA_WIDTH> data = ram.ram.buffer[beat];
            data.bits(lane * 8 + 7, lane * 8) = packet[i];
            ram.ram.buffer[beat] = data;
            ram.ram.buffer.apply();
        }
    }

    std::vector<uint8_t> mem_read_packet(uint32_t addr, size_t len)
    {
        std::vector<uint8_t> packet;
        for (size_t i = 0; i < len; ++i) {
            uint32_t byte_addr = addr + (uint32_t)i;
            uint32_t beat = byte_addr / DATA_BYTES;
            uint32_t lane = byte_addr % DATA_BYTES;
            logic<DATA_WIDTH> data = ram.ram.buffer[beat];
            packet.push_back((uint8_t)data.bits(lane * 8 + 7, lane * 8));
        }
        return packet;
    }

    bool wait_for(bool rx, uint32_t limit = 20000)
    {
        for (uint32_t i = 0; i < limit; ++i) {
            cycle(false);
            if (rx ? dut.rx_irq_out() : dut.tx_irq_out()) {
                return true;
            }
        }
        return false;
    }

    bool wait_idle(uint32_t limit = 20000)
    {
        for (uint32_t i = 0; i < limit; ++i) {
            cycle(false);
            if (dut.debug_state_out() == 0) {
                return true;
            }
        }
        return false;
    }

    static constexpr uint32_t dma_reg(uint32_t offset)
    {
        return DMA_REG_BASE + offset;
    }

    static uint32_t crc32_next(uint32_t crc, uint8_t data)
    {
        uint32_t value = crc ^ data;
        for (uint32_t i = 0; i < 8; ++i) {
            value = (value & 1u) ? ((value >> 1) ^ 0xedb88320u) : (value >> 1);
        }
        return value;
    }

    static std::vector<uint8_t> make_wire_frame(const std::vector<uint8_t>& packet)
    {
        std::vector<uint8_t> payload = packet;
        uint32_t crc = 0xffffffffu;
        uint32_t fcs;
        while (payload.size() < 60) {
            payload.push_back(0);
        }
        for (uint8_t value : payload) {
            crc = crc32_next(crc, value);
        }
        fcs = ~crc;
        std::vector<uint8_t> wire;
        for (uint32_t i = 0; i < 7; ++i) {
            wire.push_back(0x55);
        }
        wire.push_back(0xd5);
        wire.insert(wire.end(), payload.begin(), payload.end());
        for (uint32_t i = 0; i < 4; ++i) {
            wire.push_back((uint8_t)((fcs >> (i * 8u)) & 0xffu));
        }
        return wire;
    }

    static bool prefix_matches(const std::vector<uint8_t>& got, const std::vector<uint8_t>& expected)
    {
        if (got.size() < expected.size()) {
            return false;
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            if (got[i] != expected[i]) {
                return false;
            }
        }
        return true;
    }

    bool run()
    {
        std::print("CppHDL TestEthGigDMA<{}>...", DATA_WIDTH);
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "ethgig_dma_test";
        _assign();
        clear_cpu();
        for (int i = 0; i < 8; ++i) {
            cycle(true);
        }

        std::vector<uint8_t> tx = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x45, 0x46, 0x47, 0x48};
        mem_write_packet(TX_BUF, tx);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, TX_DESC2);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, TX_BUF);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET,
            (uint32_t)tx.size() | 0x0c000000u);

        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_CDESC_OFFSET), TX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_CR_OFFSET),
            EthGigDMA<>::XAXIDMA_CR_RUNSTOP_MASK | EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_TDESC_OFFSET), TX_DESC);
        if (!wait_for(false)) {
            std::print("\nERROR: TX DMA did not interrupt, state={}, tx_sr={:08x}\n",
                dut.debug_state_out(), mmio_read(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET)));
            error = true;
        }
        for (int i = 0; i < 2000 && !verif.has_tx_packet(); ++i) {
            cycle(false);
        }
        if (!verif.has_tx_packet() || verif.pop_tx_packet() != make_wire_frame(tx)) {
            std::print("\nERROR: TX DMA frame did not reach RGMII verifier\n");
            error = true;
        }
        if (!wait_idle()) {
            std::print("\nERROR: TX DMA did not return idle, state={}\n", dut.debug_state_out());
            error = true;
        }
        if ((mem_read32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0) {
            std::print("\nERROR: TX descriptor was not completed\n");
            error = true;
        }
        if ((mem_read32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET) & EthGigDMA<>::XAXIDMA_BD_CTRL_LENGTH_MASK) != tx.size()) {
            std::print("\nERROR: TX status write corrupted control length\n");
            error = true;
        }
        if (mem_read32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) != 0) {
            std::print("\nERROR: TX DMA followed next descriptor after completing tail, next status={:08x}\n",
                mem_read32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET));
            error = true;
        }

        std::vector<uint8_t> tx_ring_next = {0x02, 0x00, 0x00, 0x00, 0x00, 0x05, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x06, 0x08, 0x06, 0x52, 0x49, 0x4e, 0x47};
        mem_write_packet(TX_BUF2, tx_ring_next);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, TX_BUF2);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET,
            (uint32_t)tx_ring_next.size() | 0x0c000000u);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_TDESC_OFFSET), TX_DESC2);
        if (!wait_for(false)) {
            std::print("\nERROR: next-ring TX DMA did not interrupt, state={}, tx_sr={:08x}\n",
                dut.debug_state_out(), mmio_read(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET)));
            error = true;
        }
        for (int i = 0; i < 2000 && !verif.has_tx_packet(); ++i) {
            cycle(false);
        }
        if (!verif.has_tx_packet() || verif.pop_tx_packet() != make_wire_frame(tx_ring_next)) {
            std::print("\nERROR: next-ring TX DMA frame did not reach RGMII verifier\n");
            error = true;
        }
        if ((mem_read32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0) {
            std::print("\nERROR: next-ring TX descriptor was not completed\n");
            error = true;
        }
        if ((mem_read32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET) & EthGigDMA<>::XAXIDMA_BD_CTRL_LENGTH_MASK) != tx_ring_next.size()) {
            std::print("\nERROR: next-ring TX status write corrupted control length\n");
            error = true;
        }

        std::vector<uint8_t> tx_unaligned = {
            0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x04, 0x86, 0xdd, 0x60, 0x61, 0x62, 0x63};
        mem_write_packet(TX_BUF + 2, tx_unaligned);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, TX_BUF + 2);
        mem_write32(TX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET,
            (uint32_t)tx_unaligned.size() | 0x0c000000u);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_CDESC_OFFSET), TX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_TDESC_OFFSET), TX_DESC);
        if (!wait_for(false)) {
            std::print("\nERROR: unaligned TX DMA did not interrupt, state={}, tx_sr={:08x}\n",
                dut.debug_state_out(), mmio_read(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET)));
            error = true;
        }
        for (int i = 0; i < 2000 && !verif.has_tx_packet(); ++i) {
            cycle(false);
        }
        if (!verif.has_tx_packet()) {
            std::print("\nERROR: unaligned TX DMA did not reach RGMII verifier\n");
            error = true;
        }
        else {
            auto got = verif.pop_tx_packet();
            auto expected = make_wire_frame(tx_unaligned);
            if (got != expected) {
                std::print("\nERROR: unaligned TX DMA frame mismatch got={} expected={}\n", got.size(), expected.size());
                std::print("got:");
                for (uint8_t value : got) {
                    std::print(" {:02x}", value);
                }
                std::print("\nexp:");
                for (uint8_t value : expected) {
                    std::print(" {:02x}", value);
                }
                std::print("\n");
                error = true;
            }
        }

        std::vector<uint8_t> rx = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0xaa, 0xbb, 0xcc, 0xdd};
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET + 4, 0xc081d690u);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_CDESC_OFFSET), RX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_CR_OFFSET),
            EthGigDMA<>::XAXIDMA_CR_RUNSTOP_MASK | EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX descriptor did not arm before TX interleave, state={}\n", dut.debug_state_out());
            error = true;
        }
        std::vector<uint8_t> tx_while_rx_armed = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x0c, 0x08, 0x06, 0x54, 0x58, 0x52, 0x58};
        mem_write_packet(TX_BUF2, tx_while_rx_armed);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, TX_BUF2);
        mem_write32(TX_DESC2 + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET,
            (uint32_t)tx_while_rx_armed.size() | 0x0c000000u);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_CDESC_OFFSET), TX_DESC2);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_TX_TDESC_OFFSET), TX_DESC2);
        if (!wait_for(false)) {
            std::print("\nERROR: TX while RX armed did not interrupt, state={}, tx_sr={:08x}\n",
                dut.debug_state_out(), mmio_read(dma_reg(EthGigDMA<>::XAXIDMA_TX_SR_OFFSET)));
            error = true;
        }
        for (int i = 0; i < 2000 && !verif.has_tx_packet(); ++i) {
            cycle(false);
        }
        if (verif.has_tx_packet()) {
            (void)verif.pop_tx_packet();
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: TX completion cleared armed RX descriptor state, state={}\n", dut.debug_state_out());
            error = true;
        }
        verif.push_rx_packet(make_wire_frame(rx));
        if (!wait_for(true)) {
            std::print("\nERROR: RX DMA did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_got = mem_read_packet(RX_BUF, 60);
        if (!prefix_matches(rx_got, rx)) {
            std::print("\nERROR: RX DMA packet mismatch got={} expected-prefix={}\n", rx_got.size(), rx.size());
            std::print("got:");
            for (uint8_t value : rx_got) {
                std::print(" {:02x}", value);
            }
            std::print("\nexp:");
            for (uint8_t value : rx) {
                std::print(" {:02x}", value);
            }
            std::print("\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0) {
            std::print("\nERROR: RX descriptor was not completed\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_ACTUAL_LEN_MASK) != 60) {
            std::print("\nERROR: RX descriptor length after TX interleave is {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_ACTUAL_LEN_MASK);
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu) != 60) {
            std::print("\nERROR: RX descriptor app4 length after TX interleave is {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu);
            error = true;
        }
        if (mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET + 4) != 0xc081d690u) {
            std::print("\nERROR: RX app4 write corrupted adjacent skb field: {:08x}\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET + 4));
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET) & EthGigDMA<>::XAXIDMA_BD_CTRL_LENGTH_MASK) != 1536) {
            std::print("\nERROR: RX status write corrupted control length\n");
            error = true;
        }

        std::vector<uint8_t> rx_rearmed = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x0d, 0x08, 0x06, 0x30, 0x31, 0x32, 0x33};
        mem_write_packet(RX_BUF, std::vector<uint8_t>(96, 0));
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX single descriptor did not rearm after zero next descriptor, state={}\n", dut.debug_state_out());
            error = true;
        }
        verif.push_rx_packet(make_wire_frame(rx_rearmed));
        if (!wait_for(true)) {
            std::print("\nERROR: rearmed single RX descriptor did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_rearmed_got = mem_read_packet(RX_BUF, 60);
        if (!prefix_matches(rx_rearmed_got, rx_rearmed)) {
            std::print("\nERROR: rearmed single RX descriptor packet mismatch\n");
            error = true;
        }

        std::vector<uint8_t> rx_bad_next = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x0e, 0x08, 0x06, 0x40, 0x41, 0x42, 0x43};
        mem_write_packet(RX_BUF, std::vector<uint8_t>(96, 0));
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0xc081d7fcu);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX descriptor with invalid next did not arm, state={}\n", dut.debug_state_out());
            error = true;
        }
        verif.push_rx_packet(make_wire_frame(rx_bad_next));
        if (!wait_for(true)) {
            std::print("\nERROR: RX descriptor with invalid next did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_bad_next_got = mem_read_packet(RX_BUF, 60);
        if (!prefix_matches(rx_bad_next_got, rx_bad_next)) {
            std::print("\nERROR: invalid-next RX descriptor packet mismatch\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_ACTUAL_LEN_MASK) != 60) {
            std::print("\nERROR: invalid-next RX descriptor length is {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_ACTUAL_LEN_MASK);
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu) != 60) {
            std::print("\nERROR: invalid-next RX descriptor app4 length is {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu);
            error = true;
        }

        std::vector<uint8_t> rx_tail_gate0 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x0f, 0x08, 0x06, 0x50, 0x51, 0x52, 0x53};
        std::vector<uint8_t> rx_tail_gate1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x60, 0x61, 0x62, 0x63};
        mem_write_packet(RX_BUF, std::vector<uint8_t>(96, 0));
        mem_write_packet(RX_BUF2, std::vector<uint8_t>(96, 0));
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, RX_DESC2);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF2);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_CDESC_OFFSET), RX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        verif.push_rx_packet(make_wire_frame(rx_tail_gate0));
        if (!wait_for(true)) {
            std::print("\nERROR: first tail-gated RX descriptor did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        for (int i = 0; i < 200; ++i) {
            cycle(false);
        }
        if (dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX consumed beyond TDESC before software posted the next descriptor\n");
            error = true;
        }
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC2);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX did not rearm after software advanced TDESC to next descriptor\n");
            error = true;
        }
        verif.push_rx_packet(make_wire_frame(rx_tail_gate1));
        if (!wait_for(true)) {
            std::print("\nERROR: second tail-gated RX descriptor did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_tail_gate0_got = mem_read_packet(RX_BUF, 60);
        auto rx_tail_gate1_got = mem_read_packet(RX_BUF2, 60);
        if (!prefix_matches(rx_tail_gate0_got, rx_tail_gate0)) {
            std::print("\nERROR: first tail-gated RX packet mismatch\n");
            error = true;
        }
        if (!prefix_matches(rx_tail_gate1_got, rx_tail_gate1)) {
            std::print("\nERROR: second tail-gated RX packet mismatch\n");
            error = true;
        }

        mem_write_packet(RX_BUF, std::vector<uint8_t>(96, 0));
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        if (!dut.debug_rx_ready_out()) {
            std::print("\nERROR: RX descriptor did not rearm after wrapped TDESC update, state={}\n", dut.debug_state_out());
            error = true;
        }

        std::vector<uint8_t> rx_ring0 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x09, 0x08, 0x06, 0x10, 0x11, 0x12, 0x13};
        std::vector<uint8_t> rx_ring1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x0a, 0x08, 0x06, 0x20, 0x21, 0x22, 0x23};
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, RX_DESC2);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_NDESC_OFFSET, 0);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF2);
        mem_write32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_CDESC_OFFSET), RX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC2);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        verif.push_rx_packet(make_wire_frame(rx_ring0));
        if (!wait_for(true)) {
            std::print("\nERROR: first RX ring descriptor did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        verif.push_rx_packet(make_wire_frame(rx_ring1));
        if (!wait_for(true)) {
            std::print("\nERROR: second RX ring descriptor did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_ring0_got = mem_read_packet(RX_BUF, 60);
        auto rx_ring1_got = mem_read_packet(RX_BUF2, 60);
        if (!prefix_matches(rx_ring0_got, rx_ring0)) {
            std::print("\nERROR: first RX ring packet mismatch\n");
            error = true;
        }
        if (!prefix_matches(rx_ring1_got, rx_ring1)) {
            std::print("\nERROR: second RX ring packet mismatch\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0 ||
            (mem_read32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0) {
            std::print("\nERROR: RX ring descriptors were not both completed\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu) != 60 ||
            (mem_read32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu) != 60) {
            std::print("\nERROR: RX ring descriptor app4 lengths are {} and {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu,
                mem_read32(RX_DESC2 + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu);
            error = true;
        }

        std::vector<uint8_t> rx_unaligned = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x08, 0x08, 0x06, 0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33};
        std::vector<uint8_t> sentinel(96, 0xe5);
        mem_write_packet(RX_BUF, sentinel);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET, 0);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_BUFA_OFFSET, RX_BUF + 2);
        mem_write32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET, 1536);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_SR_OFFSET), EthGigDMA<>::XAXIDMA_IRQ_IOC_MASK);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_CDESC_OFFSET), RX_DESC);
        mmio_write(dma_reg(EthGigDMA<>::XAXIDMA_RX_TDESC_OFFSET), RX_DESC);
        for (int i = 0; i < 100; ++i) {
            cycle(false);
        }
        verif.push_rx_packet(make_wire_frame(rx_unaligned));
        if (!wait_for(true)) {
            std::print("\nERROR: unaligned RX DMA did not interrupt, state={}, rx_ready={}\n",
                dut.debug_state_out(), (bool)dut.debug_rx_ready_out());
            error = true;
        }
        auto rx_unaligned_got = mem_read_packet(RX_BUF + 2, 60);
        if (!prefix_matches(rx_unaligned_got, rx_unaligned)) {
            std::print("\nERROR: unaligned RX DMA packet mismatch got={} expected-prefix={}\n",
                rx_unaligned_got.size(), rx_unaligned.size());
            std::print("got:");
            for (uint8_t value : rx_unaligned_got) {
                std::print(" {:02x}", value);
            }
            std::print("\nexp:");
            for (uint8_t value : rx_unaligned) {
                std::print(" {:02x}", value);
            }
            std::print("\n");
            error = true;
        }
        auto rx_guard = mem_read_packet(RX_BUF, 64);
        if (rx_guard[0] != 0xe5 || rx_guard[1] != 0xe5 || rx_guard[62] != 0xe5 || rx_guard[63] != 0xe5) {
            std::print("\nERROR: unaligned RX DMA corrupted guard bytes {:02x} {:02x} {:02x} {:02x}\n",
                rx_guard[0], rx_guard[1], rx_guard[62], rx_guard[63]);
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_STS_OFFSET) & EthGigDMA<>::XAXIDMA_BD_STS_COMPLETE_MASK) == 0) {
            std::print("\nERROR: unaligned RX descriptor was not completed\n");
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu) != 60) {
            std::print("\nERROR: unaligned RX descriptor app4 length is {}, expected 60\n",
                mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_USR4_OFFSET) & 0xffffu);
            error = true;
        }
        if ((mem_read32(RX_DESC + EthGigDMA<>::XAXIDMA_BD_CTRL_LEN_OFFSET) & EthGigDMA<>::XAXIDMA_BD_CTRL_LENGTH_MASK) != 1536) {
            std::print("\nERROR: unaligned RX status write corrupted control length\n");
            error = true;
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    bool ok = true;
#ifdef VERILATOR
    ok &= TestEthGigDMA<64>().run();
#else
    ok &= TestEthGigDMA<64>().run();
    ok &= TestEthGigDMA<256>().run();
#endif
    return ok ? 0 : 1;
}
#endif
