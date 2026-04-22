#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

#include <cstdint>
#include <type_traits>

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4If : Interface
{
    __PORT(bool)               awvalid_in;
    __PORT(bool)               awready_out;
    __PORT(u<ADDR_WIDTH>)      awaddr_in;
    __PORT(u<ID_WIDTH>)        awid_in;

    __PORT(bool)               wvalid_in;
    __PORT(bool)               wready_out;
    __PORT(logic<DATA_WIDTH>)  wdata_in;
    __PORT(bool)               wlast_in;

    __PORT(bool)               bvalid_out;
    __PORT(bool)               bready_in;
    __PORT(u<ID_WIDTH>)        bid_out;

    __PORT(bool)               arvalid_in;
    __PORT(bool)               arready_out;
    __PORT(u<ADDR_WIDTH>)      araddr_in;
    __PORT(u<ID_WIDTH>)        arid_in;

    __PORT(bool)               rvalid_out;
    __PORT(bool)               rready_in;
    __PORT(logic<DATA_WIDTH>)  rdata_out;
    __PORT(bool)               rlast_out;
    __PORT(u<ID_WIDTH>)        rid_out;
};

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4Mux : public Module
{
public:
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    slaves_in[N];
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    master_out;
private:
    // Round-robin pointers
    reg<u<clog2(N)>> rr_aw, rr_ar;

    // Active transaction tracking
    reg<u<clog2(N)>> aw_sel, ar_sel;
    reg<u1> aw_active, ar_active;

    // AW arbitration
    u<clog2(N)> aw_next_comb;
    auto& aw_next_comb_func()
    {
        u8 i, idx = rr_aw;
        for (i = 0; i < N; i++) {
            if (slaves_in[idx].awvalid_in()) {
                 idx = (rr_aw + i) % N;
            }
        }
        return aw_next_comb = idx;
    }

    // AR arbitration
    u<clog2(N)> ar_next_comb;
    auto& ar_next_comb_func()
    {
        u8 i, idx = rr_ar;
        for (i = 0; i < N; i++) {
            if (slaves_in[idx].arvalid_in()) {
                 idx = (rr_ar + i) % N;
            }
        }
        return ar_next_comb = idx;
    }

public:
    void _connect()
    {
        u8 i;

        master_out.awvalid_in = __EXPR( !aw_active && slaves_in[aw_next_comb_func()].awvalid_in() );
        master_out.awaddr_in  = __EXPR( slaves_in[aw_next_comb_func()].awaddr_in() );
        master_out.awid_in    = __EXPR( slaves_in[aw_next_comb_func()].awid_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].awready_out = __EXPR( (!aw_active && aw_next_comb_func() == i) ? master_out.awready_out() : 0 );
        }

        // W channel (follows AW)
        master_out.wvalid_in = __EXPR( aw_active ? slaves_in[aw_sel].wvalid_in() : 0 );
        master_out.wdata_in  = __EXPR( slaves_in[aw_sel].wdata_in() );
        master_out.wlast_in  = __EXPR( slaves_in[aw_sel].wlast_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].wready_out = __EXPR( (aw_active && aw_sel == i) ? master_out.wready_out() : 0 );
        }

        // B response routing
        master_out.bready_in = __EXPR( slaves_in[aw_sel].bready_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].bvalid_out = __EXPR( (aw_sel == i) ? master_out.bvalid_out() : 0 );
            slaves_in[i].bid_out    = master_out.bid_out;
        }

        master_out.arvalid_in = __EXPR( !ar_active && slaves_in[ar_next_comb_func()].arvalid_in() );
        master_out.araddr_in  = __EXPR( slaves_in[ar_next_comb_func()].araddr_in() );
        master_out.arid_in    = __EXPR( slaves_in[ar_next_comb_func()].arid_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].arready_out = __EXPR( (!ar_active && ar_next_comb_func() == i) ? master_out.arready_out() : 0 );
        }

        // R response routing
        master_out.rready_in = __EXPR( slaves_in[ar_sel].rready_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].rvalid_out = __EXPR( (ar_sel == i) ? master_out.rvalid_out() : 0 );
            slaves_in[i].rdata_out  = master_out.rdata_out;
            slaves_in[i].rlast_out  = master_out.rlast_out;
            slaves_in[i].rid_out    = master_out.rid_out;
        }
    }

    void _work(bool reset)
    {
        if (!ar_active && master_out.arvalid_in() && master_out.arready_out()) {
            ar_active._next = 1;
            ar_sel._next    = ar_next_comb_func();
            rr_ar._next     = ar_next_comb_func() + 1;
        }
        if (master_out.rvalid_out() && master_out.rready_in() && master_out.rlast_out()) {
            ar_active._next = 0;
        }

        if (!aw_active && master_out.awvalid_in() && master_out.awready_out()) {
            aw_active._next = 1;
            aw_sel._next    = aw_next_comb_func();
            rr_aw._next     = aw_next_comb_func() + 1;
        }
        if (master_out.bvalid_out() && master_out.bready_in()) {
            aw_active._next = 0;
        }

        if (reset) {
            ar_active._next = 0;
            rr_ar._next = 0;

            aw_active._next = 0;
            rr_aw._next = 0;
        }
    }


    void _strobe()
    {
        rr_aw.strobe();
        rr_ar.strobe();
        aw_sel.strobe();
        ar_sel.strobe();
        aw_active.strobe();
        ar_active.strobe();
    }

    bool     debugen_in;
};
/////////////////////////////////////////////////////////////////////////

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class Axi4Mux<4,32,8,128>;
template class Axi4Mux<8,64,16,512>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class TestAxi4Mux : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL mux;
#else
    Axi4Mux<N,ADDR_WIDTH,ID_WIDTH,DATA_WIDTH> mux;
#endif

    bool error;

public:

    bool debugen_in;

    TestAxi4Mux(bool debug)
    {
        debugen_in = debug;
    }

    ~TestAxi4Mux()
    {
    }

    void _connect()
    {
#ifndef VERILATOR
        mux.__inst_name = __inst_name + "/mux";

//        mux.data_in      = __VAR( out_reg );
        mux.debugen_in   = debugen_in;
        mux._connect();
#endif
    }

    void _work(bool reset)
    {
//        size_t i;
        if (reset) {
            error = false;
            return;
        }

#ifdef VERILATOR
        // we're using this trick to update comb values of Verilator on it's outputs without strobing registers
        // the problem is that it's difficult to see 0-delayed memory output from Verilator
        // because if we write the same cycle Verilator updates combs in eval() and we see same clock written words
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
//        memcpy(mux.data_in, &out_reg, sizeof(mux.data_in));
        mux.clk = 0;
        mux.reset = 0;
        mux.eval();  // so lets update Verilator's combs without strobing registers
//        memcpy(&read_data, &mux.data_out, sizeof(read_data));
#else
//        read_data = mux.data_out();
#endif
        // test result
        if (!reset && 0) {
            std::print("{:s} ERROR:  was read instead of \n",
                __inst_name);
            error = true;
        }

#ifndef VERILATOR
        mux._work(reset);
#else
//        memcpy(mux.data_in, &out_reg, sizeof(mux.data_in));
        mux.debugen_in = debugen_in;

        mux.clk = 1;
        mux.reset = reset;
        mux.eval();  // eval of verilator should be in the end in 0-delay test
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        mux._strobe();
#endif
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        mux.clk = 0;
        mux.reset = reset;
        mux.eval();  // eval of verilator should be in the end
#endif
    }

    void _strobe_neg()
    {
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAxi4Mux, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
#else
        std::print("C++HDL TestAxi4Mux, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "mux_test";
        _connect();
        _work(1);
        _work_neg(1);

        int cycles = 100000;
        while (--cycles) {
            _strobe();
            ++sys_clock;
            _work(0);
            _strobe_neg();
            _work_neg(0);

            if (error) {
                break;
            }
        }
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    int only = -1;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[argc-1]);
        }
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Axi4Mux", {"Predef_pkg"}, {"../../../../include"}, 4, 32, 8, 128);
        ok &= VerilatorCompile(__FILE__, "Axi4Mux", {"Predef_pkg"}, {"../../../../include"}, 8, 64, 16, 512);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Axi4Mux_4_32_8_128/obj_dir/VAxi4Mux") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 0) || std::system((std::string("Axi4Mux_8_64_16_512/obj_dir/VAxi4Mux") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestAxi4Mux<4,32,8,128>(debug).run())
        && ((only != -1 && only != 1) || TestAxi4Mux<8,64,16,512>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
