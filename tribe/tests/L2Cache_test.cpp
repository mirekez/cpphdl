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

static constexpr size_t LINE_SIZE = 32;
static constexpr size_t RAM_LINES_PER_PORT = 32768;
static constexpr size_t WAIT_LIMIT = 128;

#ifdef VERILATOR
#define PORT_VALUE(val) val
#define L2_VALUE(val) (eval_l2(false), val)
#else
#define PORT_VALUE(val) val()
#define L2_VALUE(val) val()
#endif
#define PORT_EXPR(val) _ASSIGN(PORT_VALUE(val))

template<size_t L2_SIZE, size_t PORT_BITS, size_t MEM_PORTS>
class TestL2Cache : public Module
{
    static constexpr size_t WAYS = 4;
    static constexpr size_t SETS = L2_SIZE / LINE_SIZE / WAYS;
    static constexpr uint64_t REGION_SIZE64 = 0x100000000ull / MEM_PORTS;
    static constexpr uint32_t REGION_SIZE = (uint32_t)REGION_SIZE64;
    static constexpr size_t DEVICE_PORT = MEM_PORTS - 1;
#ifdef VERILATOR
    VERILATOR_MODEL l2;
#else
    L2Cache<L2_SIZE, PORT_BITS, LINE_SIZE, WAYS, 32, 32, MEM_PORTS> l2;
#endif
    Axi4Ram<32, 4, PORT_BITS, RAM_LINES_PER_PORT> ram[MEM_PORTS];

    bool read = false;
    bool d_read = false;
    bool write = false;
    uint32_t addr = 0;
    uint32_t wdata = 0;
    uint8_t wmask = 0;
    Axi4Driver<32, 4, PORT_BITS> slave_axi[MEM_PORTS] = {};
    bool region_uncached[MEM_PORTS] = {};
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        l2.i_read_in = _ASSIGN_REG(read);
        l2.i_write_in = _ASSIGN(false);
        l2.i_addr_in = _ASSIGN_REG(addr);
        l2.i_write_data_in = _ASSIGN((uint32_t)0);
        l2.i_write_mask_in = _ASSIGN((uint8_t)0);

        l2.d_read_in = _ASSIGN_REG(d_read);
        l2.d_write_in = _ASSIGN_REG(write);
        l2.d_addr_in = _ASSIGN_REG(addr);
        l2.d_write_data_in = _ASSIGN_REG(wdata);
        l2.d_write_mask_in = _ASSIGN_REG(wmask);
        l2.memory_base_in = _ASSIGN((uint32_t)0);
        l2.memory_size_in = _ASSIGN((uint32_t)0xffffffffu);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.mem_region_size_in[i] = _ASSIGN((uint32_t)REGION_SIZE);
            l2.mem_region_uncached_in[i] = _ASSIGN_REG_I(region_uncached[i]);
            AXI4_DRIVER_FROM_DRIVER_I(l2.axi_in[i], slave_axi[i]);
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
            l2.mem_region_size_in[i] = REGION_SIZE;
            l2.mem_region_uncached_in[i] = region_uncached[i];
            AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER_I(l2, axi_in, i, slave_axi[i]);
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
        // Keep backing RAM at reset-zero for the first fill/hit smoke check.
        // Later sections write explicit data through the CPU and AXI slave ports.
        for (size_t port = 0; port < MEM_PORTS; ++port) {
            for (size_t beat = 0; beat < RAM_LINES_PER_PORT; ++beat) {
                ram[port].ram.buffer.data[beat] = 0;
            }
        }
    }

    void set_backing_word(uint32_t request_addr, uint32_t data)
    {
        size_t port = (REGION_SIZE64 == 0) ? 0 : (size_t)(request_addr / REGION_SIZE64);
        uint32_t local_addr = request_addr - (uint32_t)(REGION_SIZE64 * port);
        size_t beat = local_addr / (PORT_BITS / 8);
        size_t lane = (local_addr % (PORT_BITS / 8)) / 4;
        logic<PORT_BITS> value = ram[port].ram.buffer.data[beat];
        value.bits(lane * 32 + 31, lane * 32) = data;
        ram[port].ram.buffer.data[beat] = value;
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
        slave_axi[port].aw.valid = true;
        slave_axi[port].aw.addr = request_addr;
        slave_axi[port].aw.id = 3;
        slave_axi[port].w.valid = false;
        slave_axi[port].w.data = beat;
        slave_axi[port].w.last = true;
        slave_axi[port].b.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_awready(port); ++i) {
            cycle(false);
        }
        if (!slave_awready(port)) {
            std::print("\naxi write address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].aw.valid = false;
        slave_axi[port].w.valid = true;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_wready(port); ++i) {
            cycle(false);
        }
        if (!slave_wready(port)) {
            std::print("\naxi write data ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].w.valid = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_bvalid(port); ++i) {
            cycle(false);
        }
        if (!slave_bvalid(port)) {
            std::print("\naxi write response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_axi[port].b.ready = true;
        cycle(false);
        slave_axi[port].b.ready = false;
        cycle(false);
    }

    uint32_t axi_read_word(size_t port, uint32_t request_addr)
    {
        slave_axi[port].ar.valid = true;
        slave_axi[port].ar.addr = request_addr;
        slave_axi[port].ar.id = 5;
        slave_axi[port].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(port); ++i) {
            cycle(false);
        }
        if (!slave_arready(port)) {
            std::print("\naxi read address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].ar.valid = false;
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
        slave_axi[port].r.ready = true;
        cycle(false);
        slave_axi[port].r.ready = false;
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

        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000104u;
        slave_axi[0].ar.id = 1;
        slave_axi[0].r.ready = false;
        slave_axi[1].aw.valid = true;
        slave_axi[1].aw.addr = 0x0000010cu;
        slave_axi[1].aw.id = 2;
        slave_axi[1].w.valid = true;
        slave_axi[1].w.last = true;
        slave_axi[1].b.ready = false;
        slave_axi[1].w.data = 0;
        {
            size_t lane = (0x0000010cu % (PORT_BITS / 8)) / 4;
            slave_axi[1].w.data.bits(lane * 32 + 31, lane * 32) = 0x99aabbccu;
        }
        for (size_t i = 0; i < WAIT_LIMIT && (!slave_rvalid(0) || !slave_bvalid(1)); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0) || !slave_bvalid(1)) {
            std::print("\nsimultaneous AXI masters ERROR rvalid={} bvalid={}\n", slave_rvalid(0), slave_bvalid(1));
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[1].aw.valid = false;
        slave_axi[1].w.valid = false;
        slave_axi[0].r.ready = true;
        slave_axi[1].b.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        slave_axi[1].b.ready = false;
        cycle(false);
        read_check(0x0000010cu, 0x99aabbccu);
    }

    void uncached_device_region_check()
    {
        region_uncached[DEVICE_PORT] = true;
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x20u;
        write_only(device_addr, 0xdeadbeefu, 0xf);
        uint32_t by_master = axi_read_word(DEVICE_PORT, device_addr);
        if (by_master != 0xdeadbeefu) {
            std::print("\nuncached CPU write/device read ERROR got={:#x}\n", by_master);
            error = true;
        }
        axi_write_word(DEVICE_PORT, device_addr + 4, 0xfeed1234u);
        read_check(device_addr + 4, 0xfeed1234u);
    }

    void slave_completion_does_not_release_cpu_iport_check()
    {
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 6;
        slave_axi[0].r.ready = false;
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
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.i_wait_out); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((0x00000008u % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(l2.i_read_data_out), beat_word);
        if (L2_VALUE(l2.i_wait_out) || data != 0) {
            std::print("\nCPU iport after AXI read ERROR wait={} data={:#x}\n", L2_VALUE(l2.i_wait_out), data);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void slave_request_does_not_hide_cpu_write_completion_check()
    {
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x40u;
        region_uncached[DEVICE_PORT] = true;
        read = false;
        d_read = false;
        write = true;
        addr = device_addr;
        wdata = 0x1234abcdu;
        wmask = 0xf;
        cycle(false);
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 7;
        slave_axi[0].r.ready = false;
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
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        cycle(false);
        read_check(device_addr, 0x1234abcdu);
    }

    void slave_request_does_not_drop_cpu_dport_read_check()
    {
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x80u;
        region_uncached[DEVICE_PORT] = true;
        axi_write_word(DEVICE_PORT, device_addr, 0x0badc0deu);

        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 8;
        slave_axi[0].r.ready = false;
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
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;

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

        write_only(0x0000017cu, 0x0000004du, 0x1);
        write_only(0x0000017du, 0x00000045u, 0x1);
        read_check(0x0000017cu, 0x0000454du);
        d_read_check(0x0000017du, 0x0000454du);
    }

    void dirty_eviction_check()
    {
        constexpr uint32_t set_stride = (L2_SIZE / LINE_SIZE / WAYS) * LINE_SIZE;
        // Linux stack frames save ra/s0/s1 in adjacent words near the end of a line.
        // Dirty eviction must preserve every word lane, including offset +12.
        write_then_read_check(0 * set_stride + 4, 0x11111111u, 0xf, 0x11111111u);
        write_then_read_check(0 * set_stride + 12, 0x1a2b3c4du, 0xf, 0x1a2b3c4du);
        write_then_read_check(1 * set_stride + 4, 0x22222222u, 0xf, 0x22222222u);
        write_then_read_check(2 * set_stride + 4, 0x33333333u, 0xf, 0x33333333u);
        write_then_read_check(3 * set_stride + 4, 0x44444444u, 0xf, 0x44444444u);
        write_then_read_check(4 * set_stride + 4, 0x55555555u, 0xf, 0x55555555u);
        read_check(0 * set_stride + 4, 0x11111111u);
        read_check(0 * set_stride + 12, 0x1a2b3c4du);
    }

    void immediate_hit_after_fill_check()
    {
        // Linux D-cache refill asks L2 for several PORT_BITS beats from one line.
        // A hit immediately after an AXI fill must wait until the selected beat is
        // registered, otherwise L1 can capture stale zero instead of the stack word.
        uint32_t base = 0x00000600u;
        set_backing_word(base + 0,  0x11112222u);
        set_backing_word(base + 4,  0x33334444u);
        set_backing_word(base + 8,  0x55556666u);
        set_backing_word(base + 12, 0x77778888u);
        set_backing_word(base + 16, 0x9999aaaau);
        set_backing_word(base + 20, 0xbbbbccccu);
        set_backing_word(base + 24, 0xddddeeeeu);
        set_backing_word(base + 28, 0xffff0001u);

        d_read_check(base + 0,  0x11112222u);
        d_read_check(base + 12, 0x77778888u);
        d_read_check(base + 20, 0xbbbbccccu);
        d_read_check(base + 28, 0xffff0001u);
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestL2Cache<SIZE={},PORT_BITS={},MEM_PORTS={}>...", L2_SIZE, PORT_BITS, MEM_PORTS);
#else
        std::print("CppHDL TestL2Cache<SIZE={},PORT_BITS={},MEM_PORTS={}>...", L2_SIZE, PORT_BITS, MEM_PORTS);
#endif
        std::print("\n  features under test:"
                   "\n    - cached CPU read fill and hit"
                   "\n    - immediate CPU hit from a line that was just filled"
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
        cycle(false);
        for (size_t i = 0; i < SETS + 8 && (L2_VALUE(l2.i_wait_out) || L2_VALUE(l2.d_wait_out)); ++i) {
            cycle(false);
        }
        read_check(8, 0);
        read_check(8, 0);
        immediate_hit_after_fill_check();
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
    int only = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        const auto source_root = CpphdlSourceRootFrom(__FILE__);
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        auto compile_l2 = [&](size_t cache_size, size_t port_bits, size_t mem_ports) {
            return VerilatorCompile(__FILE__, "L2Cache", {"Predef_pkg", "RAM1PORT"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "cache").string()},
                cache_size, port_bits, LINE_SIZE, 4, 32, 32, mem_ports);
        };
        ok &= compile_l2(16384, 64, 4);
        ok &= compile_l2(16384, 256, 4);
        ok &= compile_l2(16384, 64, 8);
        ok &= compile_l2(16384, 256, 8);
        ok &= compile_l2(65536, 64, 4);
        ok &= compile_l2(65536, 256, 4);
        ok &= compile_l2(65536, 64, 8);
        ok &= compile_l2(65536, 256, 8);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok &&
            std::system("L2Cache_16384_64_32_4_32_32_4/obj_dir/VL2Cache 0") == 0 &&
            std::system("L2Cache_16384_256_32_4_32_32_4/obj_dir/VL2Cache 1") == 0 &&
            std::system("L2Cache_16384_64_32_4_32_32_8/obj_dir/VL2Cache 2") == 0 &&
            std::system("L2Cache_16384_256_32_4_32_32_8/obj_dir/VL2Cache 3") == 0 &&
            std::system("L2Cache_65536_64_32_4_32_32_4/obj_dir/VL2Cache 4") == 0 &&
            std::system("L2Cache_65536_256_32_4_32_32_4/obj_dir/VL2Cache 5") == 0 &&
            std::system("L2Cache_65536_64_32_4_32_32_8/obj_dir/VL2Cache 6") == 0 &&
            std::system("L2Cache_65536_256_32_4_32_32_8/obj_dir/VL2Cache 7") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && ((only != -1 && only != 0) || TestL2Cache<16384,64,4>().run());
    ok = ok && ((only != -1 && only != 1) || TestL2Cache<16384,256,4>().run());
    ok = ok && ((only != -1 && only != 2) || TestL2Cache<16384,64,8>().run());
    ok = ok && ((only != -1 && only != 3) || TestL2Cache<16384,256,8>().run());
    ok = ok && ((only != -1 && only != 4) || TestL2Cache<65536,64,4>().run());
    ok = ok && ((only != -1 && only != 5) || TestL2Cache<65536,256,4>().run());
    ok = ok && ((only != -1 && only != 6) || TestL2Cache<65536,64,8>().run());
    ok = ok && ((only != -1 && only != 7) || TestL2Cache<65536,256,8>().run());
    return !ok;
}

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
