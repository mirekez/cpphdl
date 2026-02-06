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
    STATIC logic<MEM_WIDTH_BYTES*8> data_out_comb;
    STATIC reg<logic<MEM_WIDTH_BYTES*8>> data_out_reg;
    STATIC memory<u8,MEM_WIDTH_BYTES,MEM_DEPTH> buffer;

    STATIC logic<MEM_WIDTH_BYTES*8>& data_out_comb_func()
    {
        if (SHOWAHEAD) {
            data_out_comb = buffer[read_addr_in()];
        }
        else {
            data_out_comb = data_out_reg;
        }
        return data_out_comb;
    }

    STATIC logic<MEM_WIDTH_BYTES*8> mask_comb;

public:

    void _work(bool reset)
    {
        size_t i;
        if (debugen_in) {
            std::print("{:s}: input: ({}){}@{}({}), output: ({}){}@{}\n", __inst_name,
                (int)write_in(), write_data_in(), write_addr_in(), write_mask_in(),
                (int)read_in(), read_data_out(), read_addr_in());
        }

        if (write_in()) {
            mask_comb = 0;
            for (i=0; i < MEM_WIDTH_BYTES; ++i) {
                mask_comb.bits((i+1)*8-1,i*8) = (write_mask_in())[i] ? 0xFF : 0 ;
            }
            buffer[write_addr_in()] = (buffer[write_addr_in()]&~mask_comb) | (write_data_in()&mask_comb);
        }

        if (!SHOWAHEAD) {
            data_out_reg.next = buffer[read_addr_in()];
        }
    }

    void _strobe()
    {
        buffer.apply();
        data_out_reg.strobe();
    }

    __PORT(u<clog2(MEM_DEPTH)>)       write_addr_in;
    __PORT(bool)                      write_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  write_data_in;
    __PORT(logic<MEM_WIDTH_BYTES>)    write_mask_in;

    __PORT(u<clog2(MEM_DEPTH)>)       read_addr_in;
    __PORT(bool)                      read_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  read_data_out = __VAR( data_out_comb_func() );

    bool                      debugen_in;

    void _connect() {}
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

unsigned long sys_clock = -1;

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD>
class TestMemory : Module
{
#ifdef VERILATOR
    STATIC VERILATOR_MODEL mem;
#else
    STATIC Memory<MEM_WIDTH_BYTES,MEM_DEPTH,SHOWAHEAD> mem;
#endif

    STATIC reg<u<clog2(MEM_DEPTH)>>       write_addr_reg;
    STATIC reg<logic<MEM_WIDTH_BYTES*8>>  data_reg;
    STATIC reg<u1>                        write_reg;
    STATIC reg<u<clog2(MEM_DEPTH)>>       read_addr_reg;
    STATIC reg<u1>                        read_reg;

    STATIC reg<u1>                  was_read;
    STATIC reg<u<clog2(MEM_DEPTH)>> was_read_addr;
    STATIC reg<u16>                 to_write_cnt;
    STATIC reg<u16>                 to_read_cnt;
    STATIC bool                     error = false;

    STATIC logic<MEM_WIDTH_BYTES*8> mem_read_data;  // to support Verilator

    STATIC memory<uint8_t,MEM_WIDTH_BYTES,MEM_DEPTH> mem_copy;

public:
    bool    debugen_in;

    TestMemory(bool debug)
    {
        debugen_in = debug;
        memset(mem_copy.data, 0, sizeof(*mem_copy.data)*MEM_DEPTH);
    }

    void _connect()
    {
#ifndef VERILATOR
        mem.write_addr_in = __VAR( write_addr_reg );
        mem.write_in =      __VAR( write_reg );
        mem.write_data_in = __VAR( data_reg );
        mem.write_mask_in = __VAL( logic<MEM_WIDTH_BYTES>(0xFFFFFFFFFFFFFFFFULL) );
        mem.read_addr_in =  __VAR( read_addr_reg );
        mem.read_in =       __VAR( read_reg );
        mem.__inst_name = __inst_name + "/mem";
        mem.debugen_in  = debugen_in;
        mem._connect();
#endif
    }

    void _work(bool reset)
    {
        if (reset) {
            write_reg.clr();
            read_reg.clr();
            to_write_cnt.clr();
            to_read_cnt.clr();
            was_read.clr();
            return;
        }

#ifdef VERILATOR
        // we're using this trick to update comb values of Verilator on it's outputs without strobing registers
        // the problem is that it's difficult to see 0-delayed memory output from Verilator
        // because if we write the same cycle Verilator updates combs in eval() and we see same clock written words
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
        mem.read_addr_in = read_addr_reg;
        mem.clk = 0;
        mem.reset = 0;
        mem.eval();  // so lets update Verilator's combs without strobing registers
        mem_read_data = *(logic<MEM_WIDTH_BYTES*8>*)&mem.read_data_out;
#else
        mem_read_data = mem.read_data_out();
#endif
        // checking test results
        if (to_read_cnt && (logic<MEM_WIDTH_BYTES*8>)mem_copy[SHOWAHEAD?read_addr_reg:was_read_addr] != mem_read_data && (was_read || SHOWAHEAD)) {
            std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                __inst_name, mem_read_data, (logic<MEM_WIDTH_BYTES*8>)mem_copy[read_addr_reg],
                SHOWAHEAD?read_addr_reg:was_read_addr);
            error = true;
        }

        write_reg.next = 0;
        if (to_write_cnt) {
            write_addr_reg.next = random()%MEM_DEPTH;
            write_reg.next = 1;
            for (size_t i=0; i < MEM_WIDTH_BYTES/4; ++i) {
                data_reg.next.bits(i*32+31,i*32) = random();
            }
            to_write_cnt.next = to_write_cnt - 1;

            to_read_cnt.next = std::min((unsigned)random()%30, (unsigned)to_write_cnt);
        }
        if (write_reg) {
            mem_copy[write_addr_reg] = data_reg;
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

        if (was_read) {
            was_read_addr.next = read_addr_reg;
        }

#ifndef VERILATOR
        mem._work(reset);
#else
        mem.write_addr_in = write_addr_reg;
        mem.write_in      = write_reg;
        memcpy(&mem.write_data_in, &data_reg, sizeof(mem.write_data_in));
        memcpy(&mem.write_mask_in, &__ONES1024, sizeof(mem.write_mask_in));
        mem.read_addr_in  = read_addr_reg;
        mem.read_in       = read_reg;
        mem.debugen_in    = debugen_in;

        mem.clk = 1;
        mem.reset = reset;
        mem.eval(); // eval of verilator should be in the end in 0-delay test
#endif
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

        was_read.strobe();
        was_read_addr.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();

        mem_copy.apply();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        mem.clk = 0;
        mem.reset = reset;
        mem.eval();
#endif
    }

    void _strobe_neg()
    {
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
        std::print(" {} ({} us)\n", !error?"PASSED":"FAILED",
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
        ok &= VerilatorCompile(__FILE__, "Memory", {}, 64, 65536, 1);
        ok &= VerilatorCompile(__FILE__, "Memory", {}, 64, 65536, 0);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Memory_64_65536_1/obj_dir/VMemory") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 1) || std::system((std::string("Memory_64_65536_0/obj_dir/VMemory") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: {} " << compile_us/2 << " microseconds\n";
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
