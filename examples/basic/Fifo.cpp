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
    __PORT(bool)                         write_in;
    __PORT(logic<FIFO_WIDTH_BYTES*8>)    write_data_in;

    __PORT(bool)                         read_in;
    __PORT(logic<FIFO_WIDTH_BYTES*8>)    read_data_out  = mem.read_data_out;

    __PORT(bool)                         empty_out      = __VAL( empty_comb_func() );
    __PORT(bool)                         full_out       = __VAL( full_comb_func() );
    __PORT(bool)                         clear_in       = __VAL( false );
    __PORT(bool)                         afull_out      = __VAL( afull_reg );

    bool                         debugen_in;

    void _connect()
    {
        mem.write_data_in = write_data_in;
        mem.write_data_in = write_data_in;
        mem.write_in      = write_in;
        mem.write_mask_in = __VAL( 0xFFFFFFFFFFFFFFFFULL );
        mem.write_addr_in = __VAL( wp_reg );
        mem.read_in       = read_in;
        mem.read_addr_in  = __VAL( rp_reg );
        mem.__inst_name = __inst_name + "/mem";
        mem.debugen_in  = debugen_in;
        mem._connect();
    }

    bool full_comb_func()
    {
        return full_comb = (wp_reg == rp_reg) && full_reg;
    }

    bool empty_comb_func()
    {
        return empty_comb = (wp_reg == rp_reg) && !full_reg;
    }

    void _work(bool clk, bool reset)
    {
        if (!clk) return;
        mem._work(clk, reset);

        if (debugen_in) {
            std::print("{:s}: input: ({}){}, output: ({}){}, wp_reg: {}, rp_reg: {}, full: {}, empty: {}, reset: {}\n", __inst_name,
                (int)write_in(), write_data_in(), (int)read_in(), read_data_out(), wp_reg, rp_reg, (int)full_out(), (int)empty_out(), reset);
        }

        if (reset) {
            wp_reg.clr();
            rp_reg.clr();
            full_reg.clr();
            afull_reg.clr();
            return;
        }

        if (write_in()) {

            if (full_comb_func() && !read_in()) {
                std::print("{:s}: writing to a full fifo\n", __inst_name);
                exit(1);
            }
            if (!full_comb_func() || read_in()) {
                wp_reg.next = wp_reg + 1;
            }
            if (wp_reg.next == rp_reg) {
                full_reg.next = 1;
            }
        }

        if (read_in()) {

            if (empty_comb_func()) {
                std::print("{:s}: reading from an empty fifo\n", __inst_name);
                exit(1);
            }
            if (!empty_comb_func()) {
                rp_reg.next = rp_reg + 1;
            }
            if (!write_in()) {
                full_reg.next = 0;
            }
        }

        if (clear_in()) {
            wp_reg.next = 0;
            rp_reg.next = 0;
            full_reg.next = 0;
        }

        afull_reg.next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;

    }

    void _strobe()
    {
        mem._strobe();
        wp_reg.strobe();
        rp_reg.strobe();
        full_reg.strobe();
        afull_reg.strobe();
    }
};
/////////////////////////////////////////////////////////////////////////

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class Fifo<64,65536,1>;
template class Fifo<64,65536,0>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD>
class TestFifo : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL fifo;
#else
    Fifo<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD> fifo;
#endif

    reg<u<clog2(FIFO_DEPTH)>>       read_addr;
    reg<u<clog2(FIFO_DEPTH)>>       write_addr;
    reg<logic<FIFO_WIDTH_BYTES*8>>  data_reg;
    reg<u1>     write_reg;
    reg<u1>     read_reg;
    reg<u1>     was_read;
    reg<u1>     clear_reg;
    reg<u16>    to_write_cnt;
    reg<u16>    to_read_cnt;
    bool        error = false;

    logic<FIFO_WIDTH_BYTES*8> fifo_read_data;  // to support Verilator

    std::array<uint8_t,FIFO_WIDTH_BYTES>* mem_ref;

public:
    bool                       debugen_in;

    TestFifo(bool debug)
    {
        debugen_in = debug;
        mem_ref = new std::array<uint8_t,FIFO_WIDTH_BYTES>[FIFO_DEPTH];
    }

    ~TestFifo()
    {
        delete[] mem_ref;
    }

    void _connect()
    {
#ifndef VERILATOR
        fifo.write_in        = __VAL( write_reg );
        fifo.write_data_in   = __VAL( data_reg );
        fifo.read_in         = __VAL( read_reg );
        fifo.clear_in        = __VAL( clear_reg );
        fifo.__inst_name = __inst_name + "/fifo";
        fifo._connect();
#endif
        fifo.debugen_in  = debugen_in;
    }

    void _work(bool clk, bool reset)
    {
#ifndef VERILATOR
        fifo_read_data = fifo.read_data_out();
        fifo._work(clk, reset);
#else
        fifo.write_in      = write_reg;
        memcpy(&fifo.write_data_in, &data_reg, sizeof(fifo.write_data_in));
        fifo.read_in       = read_reg;
        fifo.clear_in      = clear_reg;
        fifo.debugen_in    = debugen_in;

        fifo_read_data = *(logic<FIFO_WIDTH_BYTES*8>*)&fifo.read_data_out;

        fifo.clk = clk;
        fifo.reset = reset;
        fifo.eval();
#endif
        if (reset) {
            clear_reg.clr();
            read_addr.clr();
            write_addr.clr();
            was_read.clr();
            write_reg.clr();
            read_reg.clr();
            to_read_cnt.clr();
            to_write_cnt.clr();
            return;
        }

        if (clk) {
            if (!reset && to_read_cnt && memcmp(&fifo_read_data, &mem_ref[read_addr], sizeof(fifo_read_data)) != 0
                && ((SHOWAHEAD && read_reg) || (!SHOWAHEAD && was_read))) {
                std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                    __inst_name,
                    fifo_read_data,
                    *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[read_addr],
                    read_addr);
                error = true;
            }
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

    void _strobe()
    {
#ifndef VERILATOR
        fifo._strobe();
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
        if (debugen_in) {
            std::print("\n");
        }
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "fifo_test";
        _connect();
        _work(0, 1);
        _work(1, 1);
        int cycles = 100000;
        int clk = 0;
        while (--cycles) {
            _work(clk, 0);

            if (clk) {
                _strobe();
            }

            if (clk && error) {
                break;
            }

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
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Memory"}, 64, 65536, 1);
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Memory"}, 64, 65536, 0);
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Fifo_64_65536_1/obj_dir/VFifo") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 1) || std::system((std::string("Fifo_64_65536_0/obj_dir/VFifo") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestFifo<64,65536,1>(debug).run())
        && ((only != -1 && only != 1) || TestFifo<64,65536,0>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
