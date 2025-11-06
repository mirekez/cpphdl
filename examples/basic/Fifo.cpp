#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "Memory.cpp"
#include <print>

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD = true>
class Fifo : public Module
{
    Memory<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD> mem;

    u1 full_comb;
    u1 empty_comb;

    reg<u<clog2(FIFO_DEPTH)>> wp_reg;
    reg<u<clog2(FIFO_DEPTH)>> rp_reg;
    reg<u1> full_reg;

    reg<u1> afull_reg;

public:
    bool                         *write_in  = nullptr;
    logic<FIFO_WIDTH_BYTES*8>    *data_in   = nullptr;

    bool                         *read_in   = nullptr;
    logic<FIFO_WIDTH_BYTES*8>    *data_out  = mem.data_out;

    bool                         *empty_out = &empty_comb;
    bool                         *full_out  = &full_comb;
    bool                         *clear_in  = &ZERO;
    bool                         *afull_out = &afull_reg;

    bool                         debugen_in;

    void connect()
    {
        mem.data_in       = data_in;
        mem.write_in      = write_in;
        mem.write_addr_in = &wp_reg;
        mem.read_in       = read_in;
        mem.read_addr_in  = &rp_reg;
        mem.connect();

        mem.__inst_name = __inst_name + "/mem";
    }

    bool full_comb_func()
    {
        full_comb = (wp_reg.next == rp_reg.next) && full_reg.next;
        return full_comb;
    }

    bool empty_comb_func()
    {
        empty_comb = (wp_reg.next == rp_reg.next) && !full_reg.next;
        return empty_comb;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;
        mem.work(clk, reset);

        if (reset) {
            wp_reg.clr();
            rp_reg.clr();
            full_reg.clr();
            afull_reg.clr();
            return;
        }

        if (*read_in) {

            if (empty_comb_func()) {
                printf("%s: reading from an empty fifo\n", __inst_name.c_str());
                exit(1);
            }
            if (!empty_comb_func()) {
                rp_reg.next = rp_reg + 1;
            }
            if (!*write_in) {
                full_reg.next = 0;
            }
        }

        if (*write_in) {

            if (full_comb_func()) {
                printf("%s: writing to a full fifo\n", __inst_name.c_str());
                exit(1);
            }
            if (!full_comb_func()) {
                wp_reg.next = wp_reg + 1;
            }
            if (wp_reg.next == rp_reg.next) {
                full_reg.next = 1;
            }
        }

        if (*clear_in) {
            wp_reg.next = 0;
            rp_reg.next = 0;
            full_reg.next = 0;
        }

        afull_reg.next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;

        if (debugen_in) {
            std::print("{:s}: input: ({}){}, output: ({}){}, full: {}, empty: {}\n", __inst_name, (int)*write_in, *data_in, (int)*read_in, *data_out, (int)*full_out, (int)*empty_out);
        }
    }

    void strobe()
    {
        mem.strobe();
        wp_reg.strobe();
        rp_reg.strobe();
        full_reg.strobe();
        afull_reg.strobe();
    }

    void comb()
    {
        mem.comb();
        mem.data_out_comb_func();
    }
};
/////////////////////////////////////////////////////////////////////////

template class Fifo<64,65536>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

// C++HDL INLINE TEST ///////////////////////////////////////////////////

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"
#ifdef VERILATOR
#include "VFifo.h"
#endif

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD>
class TestFifo : public Module
{
#ifdef VERILATOR
    VFifo fifo;
#else
    Fifo<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD> fifo;
#endif

    reg<u<clog2(FIFO_DEPTH)>>       read_addr;
    reg<u<clog2(FIFO_DEPTH)>>       write_addr;
    reg<logic<FIFO_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                         write_reg;
    reg<u1>                         read_reg;
    reg<u1>  was_read;
    reg<u1>  clear_reg;
    reg<u16> to_write_cnt;
    reg<u16> to_read_cnt;
    bool     error = false;

    std::array<uint8_t,FIFO_WIDTH_BYTES>* mem_ref;

public:
    bool                      *write_out      = &write_reg;
    logic<FIFO_WIDTH_BYTES*8> *data_out       = &data_reg;

    bool                      *read_out       = &read_reg;
    logic<FIFO_WIDTH_BYTES*8> *data_in        = nullptr;

    bool            *empty_in = nullptr;
    bool            *full_in  = nullptr;
    bool            *clear_out  = &clear_reg;
    bool            *afull_in = nullptr;

    bool             debugen_in;

    TestFifo(bool debug)
    {
        debugen_in = debug;
        mem_ref = new std::array<uint8_t,FIFO_WIDTH_BYTES>[FIFO_DEPTH];
    }

    ~TestFifo()
    {
        delete[] mem_ref;
    }

    void connect()
    {
#ifndef VERILATOR
        fifo.__inst_name = __inst_name + "/fifo";

        fifo.write_in      = write_out;
        fifo.data_in       = data_out;
        fifo.read_in       = read_out;
        fifo.clear_in      = clear_out;
        fifo.connect();

        data_in           = fifo.data_out;
        empty_in          = fifo.empty_out;
        full_in           = fifo.full_out;
        afull_in          = fifo.afull_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        fifo.work(clk, reset);
#else
        fifo.write_in      = *write_out;
        memcpy(&fifo.data_in.m_storage, data_out, sizeof(fifo.data_in.m_storage));
        fifo.read_in       = *read_out;
        fifo.clear_in      = *clear_out;
        fifo.debugen_in    = debugen_in;
printf("--- %d ---\n", );

        data_in           = (logic<FIFO_WIDTH_BYTES*8>*) &fifo.data_out.m_storage;
        empty_in          = (bool*)&fifo.empty_out;
        full_in           = (bool*)&fifo.full_out;
        afull_in          = (bool*)&fifo.afull_out;

        fifo.clk = clk;
        fifo.reset = reset;
        fifo.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            clear_reg.clr();
            read_addr.clr();
            write_addr.clr();
            was_read.clr();
            return;
        }

        if (!clk) {  // all checks on negedge edge
            if (!reset && to_read_cnt && memcmp(data_in, &mem_ref[read_addr], sizeof(*data_in)) != 0 && ((SHOWAHEAD && read_reg) || (!SHOWAHEAD && was_read))) {
                std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                    __inst_name,
                    *(logic<FIFO_WIDTH_BYTES*8>*)data_in,
                    *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[read_addr],
                    read_addr);
                error = true;
            }
            return;
        }

        write_reg.next = 0;
        if (to_write_cnt) {
            write_addr.next = write_addr + 1;
            write_reg.next = 1;
            data_reg.next = *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[write_addr];
            to_write_cnt.next = to_write_cnt - 1;

            to_read_cnt.next = std::min((unsigned)random()%30, (unsigned)to_write_cnt);
        }
        read_reg.next = 0;
        if (to_read_cnt) {
            to_read_cnt.next = to_read_cnt - 1;
            read_reg.next = 1;
        }
        if (!to_write_cnt) {
            to_write_cnt.next = random()%100;
        }
        was_read.next = 0;
        if (read_reg && !SHOWAHEAD) {
            was_read.next = 1;
        }
        if ((read_reg && SHOWAHEAD) || (was_read && !SHOWAHEAD)) {
            read_addr.next = read_addr + 1;
        }
    }

    void strobe()
    {
#ifndef VERILATOR
        fifo.strobe();
#endif

        read_addr.strobe();
        write_addr.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_reg.strobe();
        was_read.strobe();

        clear_reg.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();
    }

    void comb()
    {
#ifndef VERILATOR
        fifo.comb();
        fifo.full_comb_func();
        fifo.empty_comb_func();
#endif
    }

    bool run()
    {
        for (size_t i=0; i < FIFO_DEPTH; ++i) {
            for (size_t j=0; j < FIFO_WIDTH_BYTES; ++j) {
                mem_ref[i][j] = random();
            }
        }
#ifdef VERILATOR
        std::print("VERILATOR TestFifo, FIFO_WIDTH_BYTES: {}, FIFO_DEPTH: {}, SHOWAHEAD: {}...", FIFO_WIDTH_BYTES, FIFO_DEPTH, SHOWAHEAD);
#else
        std::print("C++HDL TestFifo, FIFO_WIDTH_BYTES: {}, FIFO_DEPTH: {}, SHOWAHEAD: {}...", FIFO_WIDTH_BYTES, FIFO_DEPTH, SHOWAHEAD);
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "fifo_test";
        connect();
        work(0, 1);
        work(1, 1);
        int cycles = 100000;
        int clk = 0;
        while (--cycles && !error) {
            comb();
            work(clk, 0);
            strobe();
            clk = !clk;
        }
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }
    int only = -1;
    if (argc > 1 && strcmp(argv[argc-1], "--debug") != 0) {
        only = atoi(argv[argc-1]);
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
//    std::cout << "Building verilator simulation... =============================================================\n";
//    ok &= VerilatorCompile("Fifo", {"Memory"}, 64, 65535, 1);
//    ok &= VerilatorCompile("Fifo", {"Memory"}, 64, 65535, 0);
//    std::cout << "Executing tests... ===========================================================================\n";
//    std::system((std::string("Fifo_64_65535_1/obj_dir/VFifo") + (debug?" --debug":"") + " 0").c_str());
//    std::system((std::string("Fifo_64_65535_0/obj_dir/VFifo") + (debug?" --debug":"") + " 1").c_str());
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
    && ((only != -1 && only != 0) || TestFifo<64,65535,1>(debug).run())
    && ((only != -1 && only != 1) || TestFifo<64,65535,0>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
