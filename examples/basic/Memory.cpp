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
    __PORT(u<clog2(MEM_DEPTH)>)       write_addr_in;
    __PORT(bool)                      write_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  write_data_in;
    __PORT(logic<MEM_WIDTH_BYTES>)    write_mask_in;

    __PORT(u<clog2(MEM_DEPTH)>)       read_addr_in;
    __PORT(bool)                      read_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  read_data_out = __VAL( data_out_comb_func() );

    bool                      debugen_in;

    void _connect() {}

    logic<MEM_WIDTH_BYTES*8>& data_out_comb_func()
    {
        if (SHOWAHEAD) {
            data_out_comb = buffer[read_addr_in()];
        }
        else {
            data_out_comb = data_out_reg;
        }
        return data_out_comb;
    }

    logic<MEM_WIDTH_BYTES*8> mask;

    void _work(bool clk, bool reset)
    {
        if (!clk) return;

        if (write_in()) {
            mask = 0;
            for (i=0; i < MEM_WIDTH_BYTES; ++i) {
                mask.bits((i+1)*8-1,i*8) = write_mask_in()[i] ? 0xFF : 0 ;
            }
            buffer[write_addr_in()] = (buffer[write_addr_in()]&~mask) | (write_data_in()&mask);
        }

        if (!SHOWAHEAD) {
            data_out_reg.next = buffer[read_addr_in()];
        }

        if (debugen_in) {
            std::print("{:s}: input: ({}){}@{}({}), output: ({}){}@{}\n", __inst_name,
                (int)write_in(), write_data_in(), write_addr_in(), write_mask_in(),
                (int)read_in(), read_data_out(), read_addr_in());
        }
    }

    void _strobe()
    {
        buffer.apply();
        data_out_reg.strobe();
    }
};
/////////////////////////////////////////////////////////////////////////

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class Memory<64,65536,1>;
template class Memory<64,65536,0>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD>
class TestMemory : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL mem;
#else
    Memory<MEM_WIDTH_BYTES,MEM_DEPTH,SHOWAHEAD> mem;
#endif

    reg<u<clog2(MEM_DEPTH)>>       write_addr_reg;
    reg<logic<MEM_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                        write_reg;
    reg<u<clog2(MEM_DEPTH)>>       read_addr_reg;
    reg<u1>                        read_reg;
//    reg<u1>                  clean;
    reg<u1>                  was_read;
    reg<u<clog2(MEM_DEPTH)>> was_read_addr;
    reg<u16>                 to_write_cnt;
    reg<u16>                 to_read_cnt;
    bool                     error = false;

    logic<MEM_WIDTH_BYTES*8> mem_read_data;  // to support Verilator

    std::array<uint8_t,MEM_WIDTH_BYTES>* mem_copy;

public:
    bool                      debugen_in;

    TestMemory(bool debug)
    {
        debugen_in = debug;
        mem_copy = new std::array<uint8_t,MEM_WIDTH_BYTES>[MEM_DEPTH];
        memset((void*)mem_copy, 0, sizeof(std::array<uint8_t,MEM_WIDTH_BYTES>[MEM_DEPTH]));
    }

    ~TestMemory()
    {
        delete[] mem_copy;
    }

    void _connect()
    {
#ifndef VERILATOR
        mem.write_addr_in = __VAL( write_addr_reg );
        mem.write_in =      __VAL( write_reg );
        mem.write_data_in = __VAL( data_reg );
        mem.write_mask_in = __VAL( 0xFFFFFFFFFFFFFFFFULL );
        mem.read_addr_in =  __VAL( read_addr_reg );
        mem.read_in =       __VAL( read_reg );
        mem.__inst_name = __inst_name + "/mem";
        mem._connect();
#endif
        mem.debugen_in  = debugen_in;
    }

    void _work(bool clk, bool reset)
    {
#ifndef VERILATOR
        mem_read_data = mem.read_data_out();
        mem._work(clk, reset);
#else
        mem.write_addr_in = write_addr_reg;
        mem.write_in      = write_reg;
        memcpy(&mem.write_data_in, &data_reg, sizeof(mem.write_data_in));
        memcpy(&mem.write_mask_in, &__ONES1024, sizeof(mem.write_mask_in));
        mem.read_addr_in  = read_addr_reg;
        mem.read_in       = read_reg;
        mem.debugen_in    = debugen_in;

        mem_read_data = *(logic<MEM_WIDTH_BYTES*8>*)&mem.read_data_out;

        mem.clk = clk;
        mem.reset = reset;
        mem.eval();
#endif
        if (reset) {
//            clean.set(1);
            write_reg.clr();
            read_reg.clr();
            to_write_cnt.clr();
            to_read_cnt.clr();
            was_read.clr();
            return;
        }

        if (clk) {
            if (!reset && to_read_cnt && memcmp(&mem_read_data, &mem_copy[SHOWAHEAD?read_addr_reg:was_read_addr], sizeof(mem_read_data)) != 0
                && (was_read || SHOWAHEAD)) {
                std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                    __inst_name,
                    mem_read_data,
                    *(logic<MEM_WIDTH_BYTES*8>*)&mem_copy[read_addr_reg],
                    SHOWAHEAD?read_addr_reg:was_read_addr);
                error = true;
            }
            if (write_reg) {  // change after all checks (we use simple, not registered memory here)
                memcpy(&mem_copy[write_addr_reg], &data_reg, sizeof(mem_copy[write_addr_reg]));
            }
        }

        write_reg.next = 0;
//        if (clean) {
//            if (to_write_cnt != MEM_DEPTH) {
//                write_addr_reg.next = to_write_cnt;
//                to_write_cnt.next = to_write_cnt + 1;
//                write_reg.next = 1;
//            }
//            else {
//                clean.next = 0;
//                to_write_cnt.next = 0;
//                to_read_cnt.next = 0;
//            }
//        }
//        else {
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
//        }
        if (was_read) {
            was_read_addr.next = read_addr_reg;
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        mem._strobe();
#endif

        write_addr_reg.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_addr_reg.strobe();
        read_reg.strobe();

//        clean.strobe();
        was_read.strobe();
        was_read_addr.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestMemory, MEM_WIDTH_BYTES: {}, MEM_DEPTH: {}, SHOWAHEAD: {}...", MEM_WIDTH_BYTES, MEM_DEPTH, SHOWAHEAD);
#else
        std::print("C++HDL TestMemory, MEM_WIDTH_BYTES: {}, MEM_DEPTH: {}, SHOWAHEAD: {}...", MEM_WIDTH_BYTES, MEM_DEPTH, SHOWAHEAD);
#endif
        if (debugen_in) {
            std::print("\n");
        }
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "mem_test";
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
        ok &= VerilatorCompile(__FILE__, "Memory", {}, 64, 65536, 1);
        ok &= VerilatorCompile(__FILE__, "Memory", {}, 64, 65536, 0);
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Memory_64_65536_1/obj_dir/VMemory") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 1) || std::system((std::string("Memory_64_65536_0/obj_dir/VMemory") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestMemory<64,65536,1>(debug).run())
        && ((only != -1 && only != 1) || TestMemory<64,65536,0>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
