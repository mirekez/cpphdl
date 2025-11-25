#include "pipeline.h"

using namespace cpphdl;

template<typename STATE, size_t ID, size_t LENGTH>
class DecodeFetch: public PipelineStage
{
public:
    struct State
    {
        int what_to_do;
        int reg0;
        int reg1;
    };
    array<State,LENGTH-ID> state_reg;

public:
    STATE                     *prev_in;
    STATE                     *diagonal_in;
    array<State,LENGTH-ID>    *state_out = &state_reg;


    void connect()
    {
        std::print("DecodeFetch: {} of {}\n", ID, LENGTH);
    }

};

template<typename STATE, size_t ID, size_t LENGTH>
class ExecuteCalc
{
public:
    struct State
    {
        int res0;
        int res1;
    };
    array<State,LENGTH-ID> state_reg;

public:
    STATE                     *prev_in;
    STATE                     *diagonal_in;
    array<State,LENGTH-ID>    *state_out = &state_reg;


    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }
};

template<typename STATE, size_t ID, size_t LENGTH>
class MemoryAccess: public PipelineStage
{
public:
    struct State
    {
        uint64_t addr;
        uint64_t result;
        bool write;
        bool read;
    };
    array<State,LENGTH-ID> state_reg;

public:
    STATE                     *prev_in;
    STATE                     *diagonal_in;
    array<State,LENGTH-ID>    *state_out = &state_reg;


    void connect()
    {
        std::print("MemoryAccess: {} of {}\n", ID, LENGTH);
    }
};

template<typename STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage
{
public:
    struct State
    {
        uint64_t data;
        int write_to_reg;
    };
    array<State,LENGTH-ID> state_reg;

public:
    STATE                     *prev_in;
    STATE                     *diagonal_in;
    array<State,LENGTH-ID>    *state_out = &state_reg;


    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }
};


template<size_t WIDTH>
class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemoryAccess,WriteBack>>
{



public:


    bool             debugen_in;

//    void connect()
//    {
//    }

//    void work(bool clk, bool reset)
//    {
//    }

//    void strobe()
//    {
//    }

//    void comb()
//    {
//    }

};

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class RiscV<32>;
template class RiscV<64>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

template<size_t WIDTH>
class TestRiscV : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV<WIDTH> riscv;
#endif

    bool error;

    size_t i;

public:
//    array<DTYPE,LENGTH>    *data_in  = nullptr;
//    array<STYPE,LENGTH>    *data_out = &out_reg;

    bool             debugen_in;

    TestRiscV(bool debug)
    {
        debugen_in = debug;
    }

    ~TestRiscV()
    {
    }

    void connect()
    {
#ifndef VERILATOR
//        riscv.__inst_name = __inst_name + "/riscv";

//        riscv.data_in      = data_out;
        riscv.debugen_in   = debugen_in;
        riscv.connect();

//        data_in           = riscv.data_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        riscv.work(clk, reset);
#else
        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        riscv.debugen_in    = debugen_in;

        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

        riscv.clk = clk;
        riscv.reset = reset;
        riscv.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            error = false;
            return;
        }

        if (!clk) {  // all checks on negedge edge
//            for (i=0; i < LENGTH; ++i) {
//                if (!reset && ((!USE_REG && can_check1 && !(*data_in)[i].cmp(was_refs1[i], 0.1))
//                             || (USE_REG && can_check2 && !(*data_in)[i].cmp(was_refs2[i], 0.1))) ) {
//                    std::print("{:s} ERROR: {}({}) was read instead of {}\n",
//                        __inst_name,
//                        (*data_in)[i].to_double(),
//                        (*data_in)[i],
//                        USE_REG?was_refs2[i]:was_refs1[i]);
//                    error = true;
//                }
//            }
            return;
        }

//        for (i=0; i < LENGTH; ++i) {
//            refs[i] = ((double)random() - RAND_MAX/2) / (RAND_MAX/2);
//            out_reg.next[i].from_double(refs[i]);
//        }
//        was_refs1.next = refs;
//        was_refs2.next = was_refs1;
//        can_check1.next = 1;
//        can_check2.next = can_check1;
    }

    void strobe()
    {
#ifndef VERILATOR
        riscv.strobe();
#endif

    }

    void comb()
    {
#ifndef VERILATOR
        riscv.comb();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestRiscV, WIDTH: {}...", WiDTH);
#else
        std::print("C++HDL TestRiscV, WIDTH: {}...", WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "riscv_test";
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
    bool noveril = false;
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }
    if (argc > 2 && strcmp(argv[2], "--noveril") == 0) {
        noveril = true;
    }
    int only = -1;
    if (argc > 1 && argv[argc-1][0] != '-') {
        only = atoi(argv[argc-1]);
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
/*    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 32);
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 64);
        std::cout << "Executing tests... ===========================================================================\n";
        std::system((std::string("RiscV_32/obj_dir/VRiscV") + (debug?" --debug":"") + " 0").c_str());
        std::system((std::string("RiscV_64/obj_dir/VRiscV") + (debug?" --debug":"") + " 1").c_str());
    }*/
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
    && ((only != -1 && only != 0) || TestRiscV<32>(debug).run())
//    && ((only != -1 && only != 1) || TestRiscV<64>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
