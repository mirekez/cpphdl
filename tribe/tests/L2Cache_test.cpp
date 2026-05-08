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
    L2Cache<L2_SIZE, PORT_BITS, LINE_SIZE, 4, 32, MEM_PORTS> l2;
#endif
    Axi4Ram<32, 4, PORT_BITS, RAM_LINES_PER_PORT> ram[MEM_PORTS];

    bool read = false;
    bool write = false;
    uint32_t addr = 0;
    uint32_t wdata = 0;
    uint8_t wmask = 0;
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

        l2.d_read_in = __EXPR(false);
        l2.d_write_in = __VAR(write);
        l2.d_addr_in = __VAR(addr);
        l2.d_write_data_in = __VAR(wdata);
        l2.d_write_mask_in = __VAR(wmask);
        l2.debugen_in = false;
        l2.__inst_name = "l2";
        l2._assign();
#endif

        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i].axi_awvalid_in = __EXPR_I(PORT_VALUE(l2.axi_awvalid_out[i]));
            ram[i].axi_awaddr_in = __EXPR_I(PORT_VALUE(l2.axi_awaddr_out[i]));
            ram[i].axi_awid_in = __EXPR_I(PORT_VALUE(l2.axi_awid_out[i]));
            ram[i].axi_wvalid_in = __EXPR_I(PORT_VALUE(l2.axi_wvalid_out[i]));
            ram[i].axi_wdata_in = __EXPR_I(copy_to_logic<PORT_BITS>(PORT_VALUE(l2.axi_wdata_out[i])));
            ram[i].axi_wlast_in = __EXPR_I(PORT_VALUE(l2.axi_wlast_out[i]));
            ram[i].axi_bready_in = __EXPR_I(PORT_VALUE(l2.axi_bready_out[i]));
            ram[i].axi_arvalid_in = __EXPR_I(PORT_VALUE(l2.axi_arvalid_out[i]));
            ram[i].axi_araddr_in = __EXPR_I(PORT_VALUE(l2.axi_araddr_out[i]));
            ram[i].axi_arid_in = __EXPR_I(PORT_VALUE(l2.axi_arid_out[i]));
            ram[i].axi_rready_in = __EXPR_I(PORT_VALUE(l2.axi_rready_out[i]));

            ram[i].debugen_in = false;
            ram[i].__inst_name = "ram" + std::to_string(i);
            ram[i]._assign();
        }

#ifndef VERILATOR
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.axi_awready_in[i] = ram[i].axi_awready_out;
            l2.axi_wready_in[i] = ram[i].axi_wready_out;
            l2.axi_bvalid_in[i] = ram[i].axi_bvalid_out;
            l2.axi_bid_in[i] = ram[i].axi_bid_out;
            l2.axi_arready_in[i] = ram[i].axi_arready_out;
            l2.axi_rvalid_in[i] = ram[i].axi_rvalid_out;
            l2.axi_rdata_in[i] = ram[i].axi_rdata_out;
            l2.axi_rlast_in[i] = ram[i].axi_rlast_out;
            l2.axi_rid_in[i] = ram[i].axi_rid_out;
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
        l2.d_read_in = false;
        l2.d_write_in = write;
        l2.d_addr_in = addr;
        l2.d_write_data_in = wdata;
        l2.d_write_mask_in = wmask;
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.axi_awready_in[i] = ram[i].axi_awready_out();
            l2.axi_wready_in[i] = ram[i].axi_wready_out();
            l2.axi_bvalid_in[i] = ram[i].axi_bvalid_out();
            l2.axi_bid_in[i] = ram[i].axi_bid_out();
            l2.axi_arready_in[i] = ram[i].axi_arready_out();
            l2.axi_rvalid_in[i] = ram[i].axi_rvalid_out();
            verilator_logic_to_wide(l2.axi_rdata_in[i], ram[i].axi_rdata_out());
            l2.axi_rlast_in[i] = ram[i].axi_rlast_out();
            l2.axi_rid_in[i] = ram[i].axi_rid_out();
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
        write = false;
        addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(l2.i_wait_out); ++i) {
            cycle(false);
        }
        if (L2_VALUE(l2.i_wait_out) || L2_VALUE(l2.i_read_data_out) != expected) {
            std::print("\nread ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(l2.i_wait_out), L2_VALUE(l2.i_read_data_out), expected);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void write_then_read_check(uint32_t request_addr, uint32_t data, uint8_t mask, uint32_t expected)
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
            L2_SIZE, PORT_BITS, LINE_SIZE, 4, 32, MEM_PORTS);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("L2Cache_4096_256_32_4_32_4/obj_dir/VL2Cache") == 0;
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
