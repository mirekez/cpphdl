#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "Memory.cpp"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD = true, bool OUTPUT_REG = false>
class Fifo : public Module
{
    Memory<FIFO_WIDTH_BYTES,FIFO_DEPTH,OUTPUT_REG ? true : SHOWAHEAD> mem;
public:
    _PORT(bool)                         write_in;
    _PORT(logic<FIFO_WIDTH_BYTES*8>)    write_data_in;

    _PORT(bool)                         read_in;
    _PORT(logic<FIFO_WIDTH_BYTES*8>)    read_data_out  = _ASSIGN_COMB( read_data_comb_func() );

    _PORT(bool)                         empty_out      = _ASSIGN_COMB( empty_comb_func() );
    _PORT(bool)                         full_out       = _ASSIGN_COMB( full_comb_func() );
    _PORT(bool)                         clear_in       = _ASSIGN( false );
    _PORT(bool)                         afull_out      = _ASSIGN_REG( afull_reg );

    bool                         debugen_in;

private:
    reg<u<clog2(FIFO_DEPTH)>> wp_reg;
    reg<u<clog2(FIFO_DEPTH)>> rp_reg;
    reg<u1> full_reg;
    reg<u1> afull_reg;
    reg<u1> read_valid_reg;
    reg<logic<FIFO_WIDTH_BYTES*8>> read_data_reg;

    bool full_comb;
    bool& full_comb_func()
    {
        if (OUTPUT_REG) {
            full_comb = (wp_reg == rp_reg) && full_reg && read_valid_reg;
        }
        else {
            full_comb = (wp_reg == rp_reg) && full_reg;
        }
        return full_comb;
    }

    bool empty_comb;
    bool& empty_comb_func()
    {
        if (OUTPUT_REG) {
            empty_comb = !read_valid_reg;
        }
        else {
            empty_comb = (wp_reg == rp_reg) && !full_reg;
        }
        return empty_comb;
    }

    logic<FIFO_WIDTH_BYTES*8> read_data_comb;
    logic<FIFO_WIDTH_BYTES*8>& read_data_comb_func()
    {
        if (OUTPUT_REG) {
            read_data_comb = read_data_reg;
        }
        else {
            read_data_comb = mem.read_data_out();
        }
        return read_data_comb;
    }

    bool mem_read_comb;
    bool& mem_read_comb_func()
    {
        if (OUTPUT_REG) {
            bool mem_empty;
            bool output_needs_word;
            mem_empty = (wp_reg == rp_reg) && !full_reg;
            output_needs_word = !read_valid_reg || read_in();
            mem_read_comb = output_needs_word && !mem_empty;
        }
        else {
            mem_read_comb = read_in();
        }
        return mem_read_comb;
    }

    bool mem_write_comb;
    bool& mem_write_comb_func()
    {
        if (OUTPUT_REG) {
            bool mem_full;
            mem_full = (wp_reg == rp_reg) && full_reg;
            mem_write_comb = write_in() && (!mem_full || mem_read_comb_func());
        }
        else {
            mem_write_comb = write_in();
        }
        return mem_write_comb;
    }

public:

    void _work(bool reset)
    {
        bool mem_read;
        bool mem_write;
        bool output_read;
        u<clog2(FIFO_DEPTH)> wp_next_value;
        size_t mem_count;

        if (debugen_in) {
            std::print("{:s}: input: ({}){}, output: ({}){}, wp_reg: {}, rp_reg: {}, full: {}, empty: {}, out_valid: {}, reset: {}\n", __inst_name,
                (int)write_in(), write_data_in(), (int)read_in(), read_data_out(), wp_reg, rp_reg,
                (int)full_reg, (int)empty_out(), (int)read_valid_reg, reset);
        }

        mem._work(reset);

        if (reset) {
            wp_reg.clr();
            rp_reg.clr();
            full_reg.clr();
            afull_reg.clr();
            read_valid_reg.clr();
            read_data_reg.clr();
            return;
        }

        if (OUTPUT_REG) {
            mem_read = mem_read_comb_func();
            mem_write = mem_write_comb_func();
            output_read = read_in() && read_valid_reg;
            wp_next_value = wp_reg + 1;

            if (write_in() && !mem_write) {
                std::print("{:s}: writing to a full fifo\n", __inst_name);
                exit(1);
            }
            if (read_in() && !read_valid_reg) {
                std::print("{:s}: reading from an empty fifo\n", __inst_name);
                exit(1);
            }

            if (mem_write) {
                wp_reg._next = wp_reg + 1;
            }
            if (mem_read) {
                rp_reg._next = rp_reg + 1;
                read_data_reg._next = mem.read_data_out();
                read_valid_reg._next = 1;
            }
            else if (output_read) {
                read_valid_reg._next = 0;
            }

            if (mem_write && !mem_read && wp_next_value == rp_reg) {
                full_reg._next = 1;
            }
            if (mem_read && !mem_write) {
                full_reg._next = 0;
            }

            mem_count = full_reg ? FIFO_DEPTH : (wp_reg >= rp_reg ? (size_t)(wp_reg - rp_reg) : FIFO_DEPTH - (size_t)rp_reg + (size_t)wp_reg);
            afull_reg._next = (mem_count + (read_valid_reg ? 1 : 0)) >= FIFO_DEPTH/2;
        }
        else {
            if (write_in()) {

                if (full_out() && !read_in()) {
                    std::print("{:s}: writing to a full fifo\n", __inst_name);
                    exit(1);
                }
                if (!full_out() || read_in()) {
                    wp_reg._next = wp_reg + 1;
                }
                if (wp_reg._next == rp_reg) {
                    full_reg._next = 1;
                }
            }

            if (read_in()) {

                if (empty_out()) {
                    std::print("{:s}: reading from an empty fifo\n", __inst_name);
                    exit(1);
                }
                if (!empty_out()) {
                    rp_reg._next = rp_reg + 1;
                }
                if (!write_in()) {
                    full_reg._next = 0;
                }
            }

            afull_reg._next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;
        }

        if (clear_in()) {
            wp_reg._next = 0;
            rp_reg._next = 0;
            full_reg._next = 0;
            read_valid_reg._next = 0;
        }
    }

    void _strobe()
    {
        mem._strobe();
        wp_reg.strobe();
        rp_reg.strobe();
        full_reg.strobe();
        afull_reg.strobe();
        read_valid_reg.strobe();
        read_data_reg.strobe();
    }


    void _assign()
    {
        mem.write_data_in = write_data_in;
        mem.write_in      = _ASSIGN_COMB( mem_write_comb_func() );
        mem.write_mask_in = _ASSIGN( logic<FIFO_WIDTH_BYTES>(0xFFFFFFFFFFFFFFFFULL) );
        mem.write_addr_in = _ASSIGN_REG( wp_reg );
        mem.read_in       = _ASSIGN_COMB( mem_read_comb_func() );
        mem.read_addr_in  = _ASSIGN_REG( rp_reg );
        mem.__inst_name = __inst_name + "/mem";
        mem.debugen_in  = debugen_in;
        mem._assign();
    }

#if !defined(SYNTHESIS)
    void add_vcd_signals(VcdFile& vcd, const std::string& prefix)
    {
        vcd.signals.push_back({prefix + "wp_reg", clog2(FIFO_DEPTH), &wp_reg});
        vcd.signals.push_back({prefix + "rp_reg", clog2(FIFO_DEPTH), &rp_reg});
        vcd.signals.push_back({prefix + "full_reg", 1, &full_reg});
        vcd.signals.push_back({prefix + "afull_reg", 1, &afull_reg});
        vcd.signals.push_back({prefix + "read_valid_reg", 1, &read_valid_reg});
        vcd.signals.push_back({prefix + "read_data_reg", FIFO_WIDTH_BYTES * 8, &read_data_reg});
        mem.add_vcd_signals(vcd, prefix + "mem.");
    }
#endif
};
/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

template class Fifo<64,65536,1,0>;
template class Fifo<64,65536,0,0>;
template class Fifo<64,65536,1,1>;
template class Fifo<64,65536,0,1>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include <vector>
#include "../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD, bool OUTPUT_REG = false>
class TestFifo : public Module
{
    static constexpr long VCD_MAX_SAMPLES = 4096;
    static constexpr bool DATA_VISIBLE_ON_READ = SHOWAHEAD || OUTPUT_REG;

#ifdef VERILATOR
    VERILATOR_MODEL fifo;
#else
    Fifo<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD,OUTPUT_REG> fifo;
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
    bool fifo_empty;
    bool fifo_full;
    VcdFile vcd;

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

    void _assign()
    {
#ifndef VERILATOR
        fifo.write_in        = _ASSIGN_REG( write_reg );
        fifo.write_data_in   = _ASSIGN_REG( data_reg );
        fifo.read_in         = _ASSIGN_REG( read_reg );
        fifo.clear_in        = _ASSIGN_REG( clear_reg );

        fifo.__inst_name = __inst_name + "/fifo";
        fifo.debugen_in  = debugen_in;
        fifo._assign();
#endif
    }

    void _work(bool reset)
    {
#ifndef VERILATOR
        fifo_read_data = fifo.read_data_out();
        fifo_empty = fifo.empty_out();
        fifo_full = fifo.full_out();
        fifo._work(reset);
#else
        fifo.write_in      = write_reg;
        memcpy(&fifo.write_data_in, &data_reg, sizeof(fifo.write_data_in));
        fifo.read_in       = read_reg;
        fifo.clear_in      = clear_reg;
        fifo.debugen_in    = debugen_in;

        fifo.clk = 0;
        fifo.reset = reset;
        fifo.eval();
        fifo_read_data = *(logic<FIFO_WIDTH_BYTES*8>*)&fifo.read_data_out;
        fifo_empty = fifo.empty_out;
        fifo_full = fifo.full_out;

        fifo.clk = 1;
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

        // check result
        if (!reset && to_read_cnt && memcmp(&fifo_read_data, &mem_ref[read_addr], sizeof(fifo_read_data)) != 0
            && ((DATA_VISIBLE_ON_READ && read_reg) || (!DATA_VISIBLE_ON_READ && was_read))) {
            std::print("{:s} ERROR: {} was read instead of {} from address {}\n",
                __inst_name,
                fifo_read_data,
                *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[read_addr],
                read_addr);
            error = true;
        }

        write_reg._next = 0;
        if (to_write_cnt && !fifo_full) {
            write_addr._next = write_addr + 1;
            write_reg._next = 1;
            data_reg._next = *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[write_addr];
            to_write_cnt._next = to_write_cnt - 1;

            to_read_cnt._next = std::min((unsigned)random()%30, (unsigned)to_write_cnt);
        }
        read_reg._next = 0;
        if (to_read_cnt && !fifo_empty) {
            to_read_cnt._next = to_read_cnt - 1;
            read_reg._next = 1;
        }
        if (!to_write_cnt) {
            to_write_cnt._next = random()%100;
        }
        was_read._next = 0;
        if (read_reg && !DATA_VISIBLE_ON_READ) {
            was_read._next = 1;
        }
        if ((read_reg && DATA_VISIBLE_ON_READ) || (was_read && !DATA_VISIBLE_ON_READ)) {
            read_addr._next = read_addr + 1;
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

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        fifo.clk = 0;
        fifo.reset = reset;
        fifo.eval();  // eval of verilator should be in the end
#endif
    }

    void _strobe_neg()
    {
    }

    void setup_vcd()
    {
        vcd.signals.clear();
        vcd.signals.push_back({"read_addr", clog2(FIFO_DEPTH), &read_addr});
        vcd.signals.push_back({"write_addr", clog2(FIFO_DEPTH), &write_addr});
        vcd.signals.push_back({"data_reg", FIFO_WIDTH_BYTES * 8, &data_reg});
        vcd.signals.push_back({"write_reg", 1, &write_reg});
        vcd.signals.push_back({"read_reg", 1, &read_reg});
        vcd.signals.push_back({"was_read", 1, &was_read});
        vcd.signals.push_back({"fifo_empty", 1, &fifo_empty});
        vcd.signals.push_back({"fifo_full", 1, &fifo_full});
        vcd.signals.push_back({"clear_reg", 1, &clear_reg});
        vcd.signals.push_back({"to_write_cnt", 16, &to_write_cnt});
        vcd.signals.push_back({"to_read_cnt", 16, &to_read_cnt});
        vcd.signals.push_back({"fifo_read_data", FIFO_WIDTH_BYTES * 8, &fifo_read_data});
#ifndef VERILATOR
        fifo.add_vcd_signals(vcd, "dut.");
#endif
        vcd.create("output.vcd");
    }

    bool run()
    {
        for (size_t i=0; i < FIFO_DEPTH; ++i) {
            for (size_t j=0; j < FIFO_WIDTH_BYTES; ++j) {
                mem_ref[i][j] = random();
            }
        }
#ifdef VERILATOR
        std::print("VERILATOR TestFifo, FIFO_WIDTH_BYTES: {}, FIFO_DEPTH: {}, SHOWAHEAD: {}, OUTPUT_REG: {}...", FIFO_WIDTH_BYTES, FIFO_DEPTH, SHOWAHEAD, OUTPUT_REG);
#else
        std::print("CppHDL TestFifo, FIFO_WIDTH_BYTES: {}, FIFO_DEPTH: {}, SHOWAHEAD: {}, OUTPUT_REG: {}...", FIFO_WIDTH_BYTES, FIFO_DEPTH, SHOWAHEAD, OUTPUT_REG);
#endif
        if (debugen_in) {
            std::print("\n");
        }
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "fifo_test";
        _assign();
        fifo_empty = true;
        fifo_full = false;
        _work(1);
        _work_neg(1);
        setup_vcd();
        vcd.sample(0);
        int cycles = 100000;
        while (--cycles) {
            _strobe();
            ++_system_clock;
            _work(0);
            _strobe_neg();
            _work_neg(0);
            if (_system_clock <= VCD_MAX_SAMPLES) {
                vcd.sample(_system_clock);
            }

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
    std::vector<std::string> positional;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Predef_pkg","Memory"}, {"../../../../include"}, 64, 65536, 1, 0);
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Predef_pkg","Memory"}, {"../../../../include"}, 64, 65536, 0, 0);
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Predef_pkg","Memory"}, {"../../../../include"}, 64, 65536, 1, 1);
        ok &= VerilatorCompile(__FILE__, "Fifo", {"Predef_pkg","Memory"}, {"../../../../include"}, 64, 65536, 0, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && std::system((std::string("Fifo_64_65536_1_0/obj_dir/VFifo 64 65536 1 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("Fifo_64_65536_0_0/obj_dir/VFifo 64 65536 0 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("Fifo_64_65536_1_1/obj_dir/VFifo 64 65536 1 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("Fifo_64_65536_0_1/obj_dir/VFifo 64 65536 0 1") + (debug?" --debug":"")).c_str()) == 0
        );
        std::cout << "Verilator compilation time: " << compile_us/4 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 3) {
        size_t width = std::stoull(positional[0]);
        size_t depth = std::stoull(positional[1]);
        size_t showahead = std::stoull(positional[2]);
        size_t output_reg = positional.size() >= 4 ? std::stoull(positional[3]) : 0;
        if (width == 64 && depth == 65536 && showahead == 1 && output_reg == 0) {
            return !(ok && TestFifo<64,65536,1,0>(debug).run());
        }
        if (width == 64 && depth == 65536 && showahead == 0 && output_reg == 0) {
            return !(ok && TestFifo<64,65536,0,0>(debug).run());
        }
        if (width == 64 && depth == 65536 && showahead == 1 && output_reg == 1) {
            return !(ok && TestFifo<64,65536,1,1>(debug).run());
        }
        if (width == 64 && depth == 65536 && showahead == 0 && output_reg == 1) {
            return !(ok && TestFifo<64,65536,0,1>(debug).run());
        }
        std::print("Unsupported Fifo test parameters: WIDTH={} DEPTH={} SHOWAHEAD={} OUTPUT_REG={}\n", width, depth, showahead, output_reg);
        return 1;
    }

    ok = ok && TestFifo<64,65536,1,0>(debug).run();
    ok = ok && TestFifo<64,65536,0,0>(debug).run();
    ok = ok && TestFifo<64,65536,1,1>(debug).run();
    ok = ok && TestFifo<64,65536,0,1>(debug).run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
