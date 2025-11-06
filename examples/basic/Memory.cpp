#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD = true>
class Memory : public Module
{
    logic<MEM_WIDTH_BYTES*8> data_out_comb;
    reg<logic<MEM_WIDTH_BYTES*8>> data_out_reg;
    memory<u8,MEM_WIDTH_BYTES,MEM_DEPTH> buffer;

    size_t i;

public:
    u<clog2(MEM_DEPTH)>*      write_addr_in   = nullptr;
    bool*                     write_in        = nullptr;
    logic<MEM_WIDTH_BYTES*8>* data_in         = nullptr;

    u<clog2(MEM_DEPTH)>*      read_addr_in    = nullptr;
    bool*                     read_in         = nullptr;
    logic<MEM_WIDTH_BYTES*8>* data_out        = &data_out_comb;

    bool                      debugen_in;

    void connect() {}

    void data_out_comb_func()
    {
        if (SHOWAHEAD) {
            data_out_comb = buffer[*read_addr_in];
        }
        else {
            data_out_comb = data_out_reg;
        }
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;

        if (*write_in) {
            buffer[*write_addr_in] = *data_in;
        }
        if (!SHOWAHEAD) {
            data_out_reg.next = buffer[*read_addr_in];
        }

        if (debugen_in) {
            std::print("{:s}: input: ({}){}@{}, output: ({}){}@{}\n", __inst_name, (int)*write_in, *data_in, *write_addr_in, (int)*read_in, *data_out, *read_addr_in);
        }
    }

    void strobe()
    {
        buffer.apply();
        data_out_reg.strobe();
    }

    void comb() {}
};
/////////////////////////////////////////////////////////////////////////

template class Memory<64,65535>;

// C++HDL TEST //////////////////////////////////////////////////////////

#ifndef SYNTHESIS  // TEST FOLLOWS

#include <chrono>
#ifdef VERILATOR
#include "VMemory.h"
#endif

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD>
class TestMemory : Module
{
#ifdef VERILATOR
    VMemory mem;
#else
    Memory<MEM_WIDTH_BYTES,MEM_DEPTH,SHOWAHEAD> mem;
#endif

    reg<u<clog2(MEM_DEPTH)>>       write_addr_reg;
    reg<logic<MEM_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                        write_reg;
    reg<u<clog2(MEM_DEPTH)>>       read_addr_reg;
    reg<u1>                        read_reg;
    reg<u1>                  clean;
    reg<u1>                  was_read;
    reg<u<clog2(MEM_DEPTH)>> was_read_addr;
    reg<u16>                 to_write_cnt;
    reg<u16>                 to_read_cnt;
    bool                     error = false;

    std::array<uint8_t,MEM_WIDTH_BYTES>* mem_copy;

public:
    u<clog2(MEM_DEPTH)>*      write_addr_out = &write_addr_reg;
    bool*                     write_out      = &write_reg;
    logic<MEM_WIDTH_BYTES*8>* data_out       = &data_reg;

    u<clog2(MEM_DEPTH)>*      read_addr_out  = &read_addr_reg;
    bool*                     read_out       = &read_reg;
    logic<MEM_WIDTH_BYTES*8>* data_in        = nullptr;

    bool                      debugen_in;

    TestMemory(bool debug)
    {
        debugen_in = debug;
        mem_copy = new std::array<uint8_t,MEM_WIDTH_BYTES>[MEM_DEPTH];
    }

    ~TestMemory()
    {
        delete[] mem_copy;
    }

    void connect()
    {
#ifndef VERILATOR
        mem.__inst_name = __inst_name + "/mem";

        mem.write_addr_in = write_addr_out;
        mem.write_in      = write_out;
        mem.data_in       = data_out;
        mem.read_addr_in  = read_addr_out;
        mem.read_in       = read_out;
        mem.debugen_in    = debugen_in;
        mem.connect();

        data_in           = mem.data_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        mem.work(clk, reset);
#else
        mem.write_addr_in = *write_addr_out;
        mem.write_in      = *write_out;
        memcpy(&mem.data_in.m_storage, data_out, sizeof(mem.data_in.m_storage));
        mem.read_addr_in  = *read_addr_out;
        mem.read_in       = *read_out;
        mem.debugen_in    = debugen_in;

        data_in           = (logic<MEM_WIDTH_BYTES*8>*) &mem.data_out.m_storage;

        mem.clk = clk;
        mem.reset = reset;
        mem.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            clean.set(1);
            to_write_cnt.clr();
            to_read_cnt.clr();
            was_read.clr();
            return;
        }

        if (!clk) {  // all checks on negedge edge
            if (!reset && to_read_cnt && memcmp(data_in, &mem_copy[SHOWAHEAD?read_addr_reg:was_read_addr], sizeof(*data_in)) != 0 && (was_read || SHOWAHEAD)) {
                std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                    __inst_name,
                    *(logic<MEM_WIDTH_BYTES*8>*)data_in,
                    *(logic<MEM_WIDTH_BYTES*8>*)&mem_copy[read_addr_reg],
                    SHOWAHEAD?read_addr_reg:was_read_addr);
                error = true;
            }
            if (write_reg) {  // change after all checks
                memcpy(&mem_copy[write_addr_reg], &data_reg, sizeof(mem_copy[write_addr_reg]));
            }
            return;
        }

        write_reg.next = 0;
        if (clean) {
            if (to_write_cnt != MEM_DEPTH) {
                write_addr_reg.next = to_write_cnt;
                to_write_cnt.next = to_write_cnt + 1;
                write_reg.next = 1;
            }
            else {
                clean.next = 0;
                to_write_cnt.next = 0;
                to_read_cnt.next = 0;
            }
        }
        else {
            if (to_write_cnt) {
                write_addr_reg.next = random()%MEM_DEPTH;
                write_reg.next = 1;
                for (size_t i=0; i < MEM_WIDTH_BYTES/4; ++i) {
                    data_reg.next.bits(i*32+31,i*32) = random();
                }
                to_write_cnt.next = to_write_cnt - 1;

                to_read_cnt.next = std::min((unsigned)random()%30, (unsigned)to_write_cnt);
            }
            read_reg.next = 0;
            if (to_read_cnt) {
                read_addr_reg.next = random()%MEM_DEPTH;
                to_read_cnt.next = to_read_cnt - 1;
                if (!SHOWAHEAD) {
                    read_reg.next = 1;
                }
            }
            if (!to_write_cnt && !to_read_cnt) {
                to_write_cnt.next = random()%100;
            }
        }
        if (was_read) {
            was_read_addr.next = read_addr_reg;
        }
    }

    void strobe()
    {
#ifndef VERILATOR
        mem.strobe();
#endif

        write_addr_reg.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_addr_reg.strobe();
        read_reg.strobe();

        clean.strobe();
        was_read.strobe();
        was_read_addr.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();
    }

    void comb()
    {
#ifndef VERILATOR
        mem.comb();
        mem.data_out_comb_func();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestMemory, MEM_WIDTH_BYTES: {}, MEM_DEPTH: {}, SHOWAHEAD: {}...", MEM_WIDTH_BYTES, MEM_DEPTH, SHOWAHEAD);
#else
        std::print("C++HDL TestMemory, MEM_WIDTH_BYTES: {}, MEM_DEPTH: {}, SHOWAHEAD: {}...", MEM_WIDTH_BYTES, MEM_DEPTH, SHOWAHEAD);
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "mem_test";
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

#ifndef NO_MAINFILE
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"
int main (int argc, char** argv)
{
    bool debug = false;
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }
    int only = -1;
    if (argc > 1) {
        only = atoi(argv[argc-1]);
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    std::cout << "Building verilator simulation... =============================================================\n";
    ok &= VerilatorCompile("Memory", 64, 65535, 1);
    ok &= VerilatorCompile("Memory", 64, 65535, 0);
    std::cout << "Executing tests... ===========================================================================\n";
    std::system((std::string("Memory_64_65535_1/obj_dir/VMemory") + (debug?"--debug":"") + " 0").c_str());
    std::system((std::string("Memory_64_65535_0/obj_dir/VMemory") + (debug?"--debug":"") + " 1").c_str());
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
    && ((only != -1 && only != 0) || TestMemory<64,65535,1>(debug).run())
    && ((only != -1 && only != 1) || TestMemory<64,65535,0>(debug).run())
    );
}
#endif  //NO_MAINFILE

#endif  //SYNTHESIS

/////////////////////////////////////////////////////////////////////////

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
