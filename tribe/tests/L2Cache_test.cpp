#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "L2Cache.h"
#include "Axi4Ram.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

template<size_t WIDTH, typename T>
static logic<WIDTH> copy_to_logic(const T& bits)
{
    logic<WIDTH> out = 0;
    memcpy(out.bytes, &bits, sizeof(out.bytes));
    return out;
}

#ifdef VERILATOR
template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH == WORDS * 32);
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

template<size_t WORDS>
static uint32_t port_word(const VlWide<WORDS>& bits, size_t word)
{
    return bits.m_storage[word];
}
#else
template<size_t WIDTH>
static uint32_t port_word(logic<WIDTH> bits, size_t word)
{
    return (uint32_t)bits.bits(word * 32 + 31, word * 32);
}
#endif

long sys_clock = -1;

static constexpr size_t L2_SIZE = 4096;
static constexpr size_t LINE_SIZE = 32;
static constexpr size_t PORT_BITS = 256;
static constexpr size_t RAM_LINES = 256;
static constexpr size_t MEM_PORTS = 4;
static constexpr size_t RAM_LINES_PER_PORT = RAM_LINES / MEM_PORTS;
static constexpr size_t WAIT_LIMIT = 128;

#ifdef VERILATOR
#define PORT_VALUE(val) val
#define L2_VALUE(val) (eval_l2(false), val)
#else
#define PORT_VALUE(val) val()
#define L2_VALUE(val) val()
#endif
#define PORT_EXPR(val) __EXPR(PORT_VALUE(val))

class TestL2Cache : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL l2;
#else
    L2Cache<L2_SIZE, PORT_BITS, LINE_SIZE, 4, 32, 32, MEM_PORTS> l2;
#endif
    Axi4Ram<32, 4, PORT_BITS, RAM_LINES_PER_PORT> ram[MEM_PORTS];

    bool read = false;
    bool d_read = false;
    bool write = false;
    uint32_t addr = 0;
    uint32_t wdata = 0;
    uint8_t wmask = 0;
    bool slave_awvalid[MEM_PORTS] = {};
    uint32_t slave_awaddr[MEM_PORTS] = {};
    uint32_t slave_awid[MEM_PORTS] = {};
    bool slave_wvalid[MEM_PORTS] = {};
    logic<PORT_BITS> slave_wdata[MEM_PORTS] = {};
    bool slave_wlast[MEM_PORTS] = {};
    bool slave_bready[MEM_PORTS] = {};
    bool slave_arvalid[MEM_PORTS] = {};
    uint32_t slave_araddr[MEM_PORTS] = {};
    uint32_t slave_arid[MEM_PORTS] = {};
    bool slave_rready[MEM_PORTS] = {};
    bool region_uncached[MEM_PORTS] = {};
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        l2.i_read_in = __VAR(read);
        l2.i_write_in = __EXPR(false);
        l2.i_addr_in = __VAR(addr);
        l2.i_write_data_in = __EXPR((uint32_t)0);
        l2.i_write_mask_in = __EXPR((uint8_t)0);

        l2.d_read_in = __VAR(d_read);
        l2.d_write_in = __VAR(write);
        l2.d_addr_in = __VAR(addr);
        l2.d_write_data_in = __VAR(wdata);
        l2.d_write_mask_in = __VAR(wmask);
        l2.memory_base_in = __EXPR((uint32_t)0);
        l2.memory_size_in = __EXPR((uint32_t)0xffffffffu);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.mem_region_size_in[i] = __EXPR((uint32_t)0x40000000u);
            l2.mem_region_uncached_in[i] = __EXPR_CAP((i), region_uncached[i]);
            l2.axi_in[i].awvalid_in = __EXPR_CAP((i), slave_awvalid[i]);
            l2.axi_in[i].awaddr_in = __EXPR_CAP((i), (u<32>)slave_awaddr[i]);
            l2.axi_in[i].awid_in = __EXPR_CAP((i), (u<4>)slave_awid[i]);
            l2.axi_in[i].wvalid_in = __EXPR_CAP((i), slave_wvalid[i]);
            l2.axi_in[i].wdata_in = __EXPR_CAP((i), slave_wdata[i]);
            l2.axi_in[i].wlast_in = __EXPR_CAP((i), slave_wlast[i]);
            l2.axi_in[i].bready_in = __EXPR_CAP((i), slave_bready[i]);
            l2.axi_in[i].arvalid_in = __EXPR_CAP((i), slave_arvalid[i]);
            l2.axi_in[i].araddr_in = __EXPR_CAP((i), (u<32>)slave_araddr[i]);
            l2.axi_in[i].arid_in = __EXPR_CAP((i), (u<4>)slave_arid[i]);
            l2.axi_in[i].rready_in = __EXPR_CAP((i), slave_rready[i]);
        }
        l2.debugen_in = false;
        l2.__inst_name = "l2";
        l2._assign();
#endif

        for (size_t i = 0; i < MEM_PORTS; ++i) {
#ifndef VERILATOR
            AXI4_DRIVER_FROM(ram[i].axi_in, l2.axi_out[i]);
#else
            AXI4_DRIVER_FROM_VERILATOR(ram[i].axi_in, l2, i, u<32>, copy_to_logic<PORT_BITS>);
#endif

            ram[i].debugen_in = false;
            ram[i].__inst_name = "ram" + std::to_string(i);
            ram[i]._assign();
        }

#ifndef VERILATOR
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM(l2.axi_out[i], ram[i].axi_in);
        }
#endif
    }

#ifdef VERILATOR
    void eval_l2(bool reset)
    {
        l2.i_read_in = read;
        l2.i_write_in = false;
        l2.i_addr_in = addr;
        l2.i_write_data_in = 0;
        l2.i_write_mask_in = 0;
        l2.d_read_in = d_read;
        l2.d_write_in = write;
        l2.d_addr_in = addr;
        l2.d_write_data_in = wdata;
        l2.d_write_mask_in = wmask;
        l2.memory_base_in = 0;
        l2.memory_size_in = 0xffffffffu;
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.mem_region_size_in[i] = 0x40000000u;
            l2.mem_region_uncached_in[i] = region_uncached[i];
            l2.axi_in___05Fawvalid_in[i] = slave_awvalid[i];
            l2.axi_in___05Fawaddr_in[i] = slave_awaddr[i];
            l2.axi_in___05Fawid_in[i] = slave_awid[i];
            l2.axi_in___05Fwvalid_in[i] = slave_wvalid[i];
            verilator_logic_to_wide(l2.axi_in___05Fwdata_in[i], slave_wdata[i]);
            l2.axi_in___05Fwlast_in[i] = slave_wlast[i];
            l2.axi_in___05Fbready_in[i] = slave_bready[i];
            l2.axi_in___05Farvalid_in[i] = slave_arvalid[i];
            l2.axi_in___05Faraddr_in[i] = slave_araddr[i];
            l2.axi_in___05Farid_in[i] = slave_arid[i];
            l2.axi_in___05Frready_in[i] = slave_rready[i];
        }
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM_VERILATOR(l2, ram[i].axi_in, i);
        }
        l2.debugen_in = false;
        l2.reset = reset;
        l2.eval();
    }
#endif

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        l2.clk = 0;
        eval_l2(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._work(reset);
        }
        l2.clk = 1;
        eval_l2(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._strobe();
        }
        l2.clk = 0;
        eval_l2(reset);
#else
        l2._work(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._work(reset);
        }
        l2._strobe();
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._strobe();
        }
#endif
        ++sys_clock;
    }

    void preload()
    {
        logic<PORT_BITS> line = 0;
        for (size_t i = 0; i < LINE_SIZE / 4; ++i) {
            line.bits(i * 32 + 31, i * 32) = 0xa5000000u + i;
        }
        ram[0].ram.buffer[0] = line;
    }

    void read_check(uint32_t request_addr, uint32_t expected)
    {
        read = true;
        d_read = false;
        write = false;
        addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.i_wait_out); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((request_addr % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(l2.i_read_data_out), beat_word);
        if (L2_VALUE(l2.i_wait_out) || data != expected) {
            std::print("\nread ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(l2.i_wait_out), data, expected);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void d_read_check(uint32_t request_addr, uint32_t expected)
    {
        read = false;
        d_read = true;
        write = false;
        addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.d_wait_out); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((request_addr % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(l2.d_read_data_out), beat_word);
        if (L2_VALUE(l2.d_wait_out) || data != expected) {
            std::print("\nd-read ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(l2.d_wait_out), data, expected);
            error = true;
        }
        cycle(false);
        d_read = false;
        cycle(false);
    }

    void write_then_read_check(uint32_t request_addr, uint32_t data, uint8_t mask, uint32_t expected)
    {
        read = false;
        d_read = false;
        write = true;
        addr = request_addr;
        wdata = data;
        wmask = mask;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.d_wait_out); ++i) {
            cycle(false);
        }
        if (L2_VALUE(l2.d_wait_out)) {
            std::print("\nwrite ERROR addr={:#x} wait=1\n", request_addr);
            error = true;
        }
        cycle(false);
        write = false;
        cycle(false);
        read_check(request_addr, expected);
    }

    void write_only(uint32_t request_addr, uint32_t data, uint8_t mask)
    {
        read = false;
        write = true;
        addr = request_addr;
        wdata = data;
        wmask = mask;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.d_wait_out); ++i) {
            cycle(false);
        }
        if (L2_VALUE(l2.d_wait_out)) {
            std::print("\nwrite ERROR addr={:#x} wait=1\n", request_addr);
            error = true;
        }
        cycle(false);
        write = false;
        cycle(false);
    }

    bool slave_awready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fawready_out[port];
#else
        return l2.axi_in[port].awready_out();
#endif
    }

    bool slave_wready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fwready_out[port];
#else
        return l2.axi_in[port].wready_out();
#endif
    }

    bool slave_bvalid(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fbvalid_out[port];
#else
        return l2.axi_in[port].bvalid_out();
#endif
    }

    bool slave_arready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Farready_out[port];
#else
        return l2.axi_in[port].arready_out();
#endif
    }

    bool slave_rvalid(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Frvalid_out[port];
#else
        return l2.axi_in[port].rvalid_out();
#endif
    }

    logic<PORT_BITS> slave_rdata(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return copy_to_logic<PORT_BITS>(l2.axi_in___05Frdata_out[port]);
#else
        return l2.axi_in[port].rdata_out();
#endif
    }

    void axi_write_word(size_t port, uint32_t request_addr, uint32_t data)
    {
        logic<PORT_BITS> beat = 0;
        size_t lane = (request_addr % (PORT_BITS / 8)) / 4;
        beat.bits(lane * 32 + 31, lane * 32) = data;
        slave_awvalid[port] = true;
        slave_awaddr[port] = request_addr;
        slave_awid[port] = 3;
        slave_wvalid[port] = false;
        slave_wdata[port] = beat;
        slave_wlast[port] = true;
        slave_bready[port] = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_awready(port); ++i) {
            cycle(false);
        }
        if (!slave_awready(port)) {
            std::print("\naxi write address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_awvalid[port] = false;
        slave_wvalid[port] = true;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_wready(port); ++i) {
            cycle(false);
        }
        if (!slave_wready(port)) {
            std::print("\naxi write data ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_wvalid[port] = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_bvalid(port); ++i) {
            cycle(false);
        }
        if (!slave_bvalid(port)) {
            std::print("\naxi write response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_bready[port] = true;
        cycle(false);
        slave_bready[port] = false;
        cycle(false);
    }

    uint32_t axi_read_word(size_t port, uint32_t request_addr)
    {
        slave_arvalid[port] = true;
        slave_araddr[port] = request_addr;
        slave_arid[port] = 5;
        slave_rready[port] = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(port); ++i) {
            cycle(false);
        }
        if (!slave_arready(port)) {
            std::print("\naxi read address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_arvalid[port] = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(port); ++i) {
            cycle(false);
        }
        logic<PORT_BITS> beat = slave_rdata(port);
        uint32_t lane = (request_addr % (PORT_BITS / 8)) / 4;
        uint32_t data = (uint32_t)beat.bits(lane * 32 + 31, lane * 32);
        if (!slave_rvalid(port)) {
            std::print("\naxi read response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_rready[port] = true;
        cycle(false);
        slave_rready[port] = false;
        cycle(false);
        return data;
    }

    void slave_coherence_check()
    {
        write_only(0x00000104u, 0x11223344u, 0xf);
        uint32_t by_master = axi_read_word(0, 0x00000104u);
        if (by_master != 0x11223344u) {
            std::print("\nCPU->AXI coherence ERROR got={:#x}\n", by_master);
            error = true;
        }

        axi_write_word(1, 0x00000108u, 0x55667788u);
        read_check(0x00000108u, 0x55667788u);

        slave_arvalid[0] = true;
        slave_araddr[0] = 0x00000104u;
        slave_arid[0] = 1;
        slave_rready[0] = false;
        slave_awvalid[1] = true;
        slave_awaddr[1] = 0x0000010cu;
        slave_awid[1] = 2;
        slave_wvalid[1] = true;
        slave_wlast[1] = true;
        slave_bready[1] = false;
        slave_wdata[1] = 0;
        slave_wdata[1].bits(3 * 32 + 31, 3 * 32) = 0x99aabbccu;
        for (size_t i = 0; i < WAIT_LIMIT && (!slave_rvalid(0) || !slave_bvalid(1)); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0) || !slave_bvalid(1)) {
            std::print("\nsimultaneous AXI masters ERROR rvalid={} bvalid={}\n", slave_rvalid(0), slave_bvalid(1));
            error = true;
        }
        slave_arvalid[0] = false;
        slave_awvalid[1] = false;
        slave_wvalid[1] = false;
        slave_rready[0] = true;
        slave_bready[1] = true;
        cycle(false);
        slave_rready[0] = false;
        slave_bready[1] = false;
        cycle(false);
        read_check(0x0000010cu, 0x99aabbccu);
    }

    void uncached_device_region_check()
    {
        region_uncached[3] = true;
        uint32_t device_addr = 0xc0000000u + 0x20u;
        write_only(device_addr, 0xdeadbeefu, 0xf);
        uint32_t by_master = axi_read_word(2, device_addr);
        if (by_master != 0xdeadbeefu) {
            std::print("\nuncached CPU write/device read ERROR got={:#x}\n", by_master);
            error = true;
        }
        axi_write_word(2, device_addr + 4, 0xfeed1234u);
        read_check(device_addr + 4, 0xfeed1234u);
    }

    void slave_completion_does_not_release_cpu_iport_check()
    {
        slave_arvalid[0] = true;
        slave_araddr[0] = 0x00000000u;
        slave_arid[0] = 6;
        slave_rready[0] = false;
        read = true;
        addr = 0x00000008u;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI read while CPU iport waits ERROR: no slave response\n");
            error = true;
        }
        if (!L2_VALUE(l2.i_wait_out)) {
            std::print("\nAXI read while CPU iport waits ERROR: slave completion released iport\n");
            error = true;
        }
        slave_arvalid[0] = false;
        slave_rready[0] = true;
        cycle(false);
        slave_rready[0] = false;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.i_wait_out); ++i) {
            cycle(false);
        }
        uint32_t data = port_word(L2_VALUE(l2.i_read_data_out), 2);
        if (L2_VALUE(l2.i_wait_out) || data != 0xa5000002u) {
            std::print("\nCPU iport after AXI read ERROR wait={} data={:#x}\n", L2_VALUE(l2.i_wait_out), data);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void slave_request_does_not_hide_cpu_write_completion_check()
    {
        uint32_t device_addr = 0xc0000000u + 0x40u;
        region_uncached[3] = true;
        read = false;
        d_read = false;
        write = true;
        addr = device_addr;
        wdata = 0x1234abcdu;
        wmask = 0xf;
        cycle(false);
        slave_arvalid[0] = true;
        slave_araddr[0] = 0x00000000u;
        slave_arid[0] = 7;
        slave_rready[0] = false;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.d_wait_out); ++i) {
            cycle(false);
        }
        if (L2_VALUE(l2.d_wait_out)) {
            std::print("\nAXI request while CPU write completes ERROR: dport completion hidden\n");
            error = true;
        }
        cycle(false);
        write = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI request after CPU write ERROR: no slave response\n");
            error = true;
        }
        slave_arvalid[0] = false;
        slave_rready[0] = true;
        cycle(false);
        slave_rready[0] = false;
        cycle(false);
        read_check(device_addr, 0x1234abcdu);
    }

    void slave_request_does_not_drop_cpu_dport_read_check()
    {
        uint32_t device_addr = 0xc0000000u + 0x80u;
        region_uncached[3] = true;
        axi_write_word(3, device_addr, 0x0badc0deu);

        slave_arvalid[0] = true;
        slave_araddr[0] = 0x00000000u;
        slave_arid[0] = 8;
        slave_rready[0] = false;
        read = false;
        d_read = true;
        write = false;
        addr = device_addr;

        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI request while CPU dport read waits ERROR: no slave response\n");
            error = true;
        }
        slave_arvalid[0] = false;
        slave_rready[0] = true;
        cycle(false);
        slave_rready[0] = false;

        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.d_wait_out); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((device_addr % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(l2.d_read_data_out), beat_word);
        if (L2_VALUE(l2.d_wait_out) || data != 0x0badc0deu) {
            std::print("\nCPU dport after AXI read ERROR wait={} data={:#x}\n", L2_VALUE(l2.d_wait_out), data);
            error = true;
        }
        cycle(false);
        d_read = false;
        cycle(false);
    }

    void stack_alias_check()
    {
        write_then_read_check(0xfffffb08u, 0x000002bcu, 0xf, 0x000002bcu);
        read_check(0x000001a0u, 0x00000000u);
        read_check(0x000001c0u, 0x00000000u);
        read_check(0x000002bcu, 0x00000000u);
        read_check(0xfffffb08u, 0x000002bcu);
    }

    void byte_store_check()
    {
        write_only(0xfffffe54u, 0x00000002u, 0x1);
        write_only(0xfffffe55u, 0x00000000u, 0x1);
        write_only(0xfffffe56u, 0x00000000u, 0x1);
        write_only(0xfffffe57u, 0x00000000u, 0x1);
        read_check(0xfffffe54u, 0x00000002u);
    }

    void dirty_eviction_check()
    {
        constexpr uint32_t set_stride = (L2_SIZE / LINE_SIZE / 4) * LINE_SIZE;
        write_then_read_check(0 * set_stride + 4, 0x11111111u, 0xf, 0x11111111u);
        write_then_read_check(1 * set_stride + 4, 0x22222222u, 0xf, 0x22222222u);
        write_then_read_check(2 * set_stride + 4, 0x33333333u, 0xf, 0x33333333u);
        write_then_read_check(3 * set_stride + 4, 0x44444444u, 0xf, 0x44444444u);
        write_then_read_check(4 * set_stride + 4, 0x55555555u, 0xf, 0x55555555u);
        read_check(0 * set_stride + 4, 0x11111111u);
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestL2Cache...");
#else
        std::print("CppHDL TestL2Cache...");
#endif
        std::print("\n  features under test:"
                   "\n    - cached CPU read fill and hit"
                   "\n    - cached CPU partial writes"
                   "\n    - dirty eviction"
                   "\n    - external AXI master read of CPU-written cache line"
                   "\n    - CPU read of external AXI-master-written cache line"
                   "\n    - simultaneous CPU/private and two external masters on one line"
                   "\n    - external AXI completion does not release CPU instruction port"
                   "\n    - external AXI request does not hide CPU write completion"
                   "\n    - external AXI request does not drop CPU data-port MMIO read"
                   "\n    - uncached/device region CPU and AXI-master accesses\n");
        auto start = std::chrono::high_resolution_clock::now();
        _assign();
        preload();
        cycle(true);
        for (size_t i = 0; i < 8; ++i) {
            cycle(false);
        }
        read_check(8, 0xa5000002u);
        read_check(8, 0xa5000002u);
        write_then_read_check(64 + 4, 0x12345678u, 0xf, 0x12345678u);
        write_then_read_check(64 + 4, 0xabcd0000u, 0xc, 0xabcd5678u);
        stack_alias_check();
        byte_store_check();
        dirty_eviction_check();
        slave_coherence_check();
        slave_completion_does_not_release_cpu_iport_check();
        slave_request_does_not_hide_cpu_write_completion_check();
        slave_request_does_not_drop_cpu_dport_read_check();
        uncached_device_region_check();

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "L2Cache", {"Predef_pkg", "RAM1PORT"},
            {"../../../../../include", "../../../../../tribe/common", "../../../../../tribe/cache"},
            L2_SIZE, PORT_BITS, LINE_SIZE, 4, 32, 32, MEM_PORTS);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("L2Cache_4096_256_32_4_32_32_4/obj_dir/VL2Cache") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestL2Cache().run();
    return !ok;
}

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
