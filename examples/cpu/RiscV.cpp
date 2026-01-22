#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "Pipeline.h"
#include "File.h"

#include "DecodeFetch.h"
#include "ExecuteCalc.h"
#include "MemWB.h"

class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemWB>>
{
    File<32,32>         regs;
    reg<u32>            pc;
    reg<u1>             valid;

public:
                                          // it's better to always assign all outputs inline
    __PORT(bool)      dmem_write_out      = std::get<1>(members).mem_write_out;
    __PORT(uint32_t)  dmem_write_addr_out = std::get<1>(members).mem_write_addr_out;
    __PORT(uint32_t)  dmem_write_data_out = std::get<1>(members).mem_write_data_out;
    __PORT(uint8_t)   dmem_write_mask_out = std::get<1>(members).mem_write_mask_out;
    __PORT(bool)      dmem_read_out       = std::get<1>(members).mem_read_out;
    __PORT(uint32_t)  dmem_read_addr_out  = std::get<1>(members).mem_read_addr_out;
    __PORT(uint32_t)  dmem_read_data_in;
    __PORT(uint32_t)  imem_read_addr_out = __VAL( pc );
    __PORT(uint32_t)  imem_read_data_in;
    bool              debugen_in;

    void connect()
    {
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
        auto& wb = std::get<2>(members);

        df.__inst_name = Pipeline::__inst_name + "/decode_fetch";
        ex.__inst_name = Pipeline::__inst_name + "/execute_calc";
        wb.__inst_name = Pipeline::__inst_name + "/write_back";

        df.pc_in          = __VAL( pc );
        df.instr_valid_in = __VAL( valid );
        df.instr_in       = imem_read_data_in;
        df.regs_data0_in  = __VAL( df.rs1_out == 0 ? 0 : regs.read_data0_out() );
        df.regs_data1_in  = __VAL( df.rs2_out == 0 ? 0 : regs.read_data1_out() );
        df.alu_result_in  = ex.alu_result_out;
        df.mem_data_in    = dmem_read_data_in;

        wb.mem_data_in    = dmem_read_data_in;

        Pipeline::connect();

        regs.read_addr0_in = df.rs1_out;
        regs.read_addr1_in = df.rs2_out;

        regs.write_in = wb.regs_write_out;
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
        auto& wb = std::get<2>(members);
        if (reset) {
            pc.clr();
            valid.clr();
            return;
        }
        #ifndef SYNTHESIS
        BIG_STATE tmp;
        Instr instr = {df.instr_in()};
        if ((instr.raw&3) == 3) {
            instr.decode(tmp);
        }
        else {
            instr.decode16(tmp);
        }

        // delayed by 1 to align WB
        std::string interpret;
        if (states_comb[1].valid && states_comb[1].alu_op != Alu::ANONE) {
            interpret += std::format("r{:02d} r{:02d} {:5s}({:08x},{:08x}) ", (int)states_comb[1].rs1, (int)states_comb[1].rs2, AOPS[states_comb[1].alu_op],
                             states_comb[1].debug_alu_a, states_comb[1].debug_alu_b);
        }
        if (states_comb[1].valid && states_comb[1].br_op != Br::BNONE && states_comb[1].debug_branch_taken) {
            interpret += std::format("{}({:08x}) rd={:02d} ", BOPS[states_comb[1].br_op], states_comb[1].debug_branch_target, (int)states_comb[1].rd);
        }
        if (states_comb[1].valid && states_comb[1].mem_op == Mem::LOAD) {
            interpret += std::format("LOAD({:08x}) ", states_comb[1].alu_result);
        }
        if (states_comb[1].valid && states_comb[1].mem_op == Mem::STORE) {
            interpret += std::format("STOR({:08x}) {:08x} from r{:02d} ", states_comb[1].alu_result, states_comb[1].rs2_val, (int)states_comb[1].rs2);
        }
        if (states_comb[1].valid && states_comb[1].wb_op != Wb::WNONE && wb.regs_write_out()) {
            interpret += std::format("wb {:08x} from {} to r{:02d} ", wb.regs_data_out(), WOPS[states_comb[1].wb_op], wb.regs_wr_id_out());
        }
        //

        if (debugen_in) {
            std::print("({}/{}){}: {} rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({})ops:{:02d}/{}/{}/{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({}){:08x} => "
                       "mem({}/{}@{:08x}){:08x}/{:01x} ({})wop({:x}),r({}){:08x}@{:02d}: {}\n",
                (int)valid, (int)df.stall_out(), pc, instr.mnemonic(),
                (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
                (int)states_comb[0].valid, (uint8_t)states_comb[0].alu_op, (uint8_t)states_comb[0].mem_op, (uint8_t)states_comb[0].br_op, (uint8_t)states_comb[0].wb_op,
                (int)states_comb[0].rs1, (int)states_comb[0].rs2, states_comb[0].rs1_val, states_comb[0].rs2_val, states_comb[0].imm, ex.alu_result_comb_func(), (int)states_comb[0].rd,
                (int)ex.branch_taken_out(), ex.branch_target_out(),
                (int)ex.mem_write_out(), (int)ex.mem_read_out(),
                ex.mem_write_addr_out(), ex.mem_write_data_out(), ex.mem_write_mask_out(),
                (int)states_comb[1].valid, (uint8_t)states_comb[1].wb_op, (int)wb.regs_write_out(), wb.regs_data_out(), wb.regs_wr_id_out(),
                interpret);
        }
        #endif

        if (dmem_write_addr_out() == 0x11223344 && dmem_write_out() ) {
            FILE* out = fopen("out.txt", "a");
            fprintf(out, "%c", dmem_write_data_out()&0xFF);
            fclose(out);
        }

        regs.work(clk, reset);
        Pipeline::work(clk, reset);

        if (valid && !df.stall_out()) {
            pc.next = pc + ((df.instr_in()&3)==3?4:2);
        }
        if (states_comb[0].valid && ex.branch_taken_out()) {
            pc.next = ex.branch_target_out();
        }

        valid.next = true;
    }

    void strobe()
    {
        Pipeline::strobe();
        regs.strobe();
        pc.strobe();
        valid.strobe();
    }

    void comb()
    {
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
//        auto& ma = std::get<2>(members);
//        auto& wb = std::get<3>(members);

        Pipeline::comb();
        df.state_comb_func();
        regs.comb();
        ex.alu_result_comb_func();
        ex.branch_taken_comb_func();
        ex.branch_target_comb_func();
    }

};

// C++HDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

#include <algorithm>
#include <fstream>

#include "Ram.h"

#define RAM_SIZE 2048

class TestRiscV : public Module
{
    Ram<32,RAM_SIZE>   imem;
    Ram<32,RAM_SIZE>   dmem;
#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV riscv;
#endif

    bool imem_write = false;
    uint32_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

    uint32_t dmem_read_data;
    uint32_t imem_read_data;

//    size_t i;

public:

    bool      debugen_in;

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
        dmem.read_in = riscv.dmem_read_out;
        dmem.read_addr_in = riscv.dmem_read_addr_out;
        dmem.write_in = riscv.dmem_write_out;
        dmem.write_addr_in = riscv.dmem_write_addr_out;
        dmem.write_data_in = riscv.dmem_write_data_out;
        dmem.write_mask_in = riscv.dmem_write_mask_out;
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem.connect();

        imem.read_in = __VAL( !imem_write );
        imem.read_addr_in = riscv.imem_read_addr_out;
        imem.write_in = __VAL( imem_write );
        imem.write_addr_in = __VAL( imem_write_addr );
        imem.write_data_in = __VAL( imem_write_data );
        imem.write_mask_in = __VAL( 0xFFFFFFFFu );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem.connect();

        riscv.dmem_read_data_in = dmem.read_data_out;
        riscv.imem_read_data_in = imem.read_data_out;
        riscv.debugen_in = debugen_in;
        riscv.__inst_name = __inst_name + "/riscv";
        riscv.connect();
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        dmem_read_data = dmem.read_data_out();
        imem_read_data = imem.read_data_out();
        riscv.work(clk, reset);
        dmem.work(clk, reset);
        imem.work(clk, reset);
#else
//        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        dmem_read_data = dmem.read_data_out;
        imem_read_data = imem.read_data_out;
        riscv.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

        riscv.clk = clk;
        riscv.reset = reset;
        riscv.eval();  // eval of verilator should be in the end
        dmem.work(clk, reset);
        imem.work(clk, reset);
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
        dmem.strobe();
        imem.strobe();
#endif

    }

    void comb()
    {
#ifndef VERILATOR
        riscv.comb();
        dmem.comb();
        imem.comb();
#endif
    }

    bool run(std::string filename, size_t start_offset)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestRiscV...");
#else
        std::print("C++HDL TestRiscV...");
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", "w");
        fclose(out);

        __inst_name = "riscv_test";
        connect();
        work(0, 1);
        work(1, 1);

        /////////////// read program to memory
        uint32_t ram[RAM_SIZE];
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        fseek(fbin, start_offset, SEEK_SET);
        int read_bytes = fread(ram, 1, 4*RAM_SIZE, fbin);
        std::print("Reading program into memory (size: {}, offset: {})\n", read_bytes, start_offset);

        imem_write = true;
        imem.work(1, 1);
        for (size_t addr = 0; addr < RAM_SIZE; ++addr) {
            imem_write_addr = addr*4;
            imem_write_data = ram[addr];
            imem.work(1, 0);
            imem.strobe();
            if (debugen_in) {
                std::print("{:04x}: {:08x}\n", addr, ram[addr]);
            }
        }
        imem_write = false;
        fclose(fbin);
        dmem.ram.buffer = imem.ram.buffer;  // we need data from binary
        ///////////////////////////////////////

        auto start = std::chrono::high_resolution_clock::now();
        int cycles = 1000000;
        int clk = 0;
        while (--cycles && !error) {
            comb();
            work(clk, 0);
            strobe();
            clk = !clk;
        }

        std::ifstream a("rv32i.log", std::ios::binary), b("out.txt", std::ios::binary);
        error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));

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
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {"File", "Memory",
                  "DecodeFetchint_int_0_0_State_pkg",
                  "ExecuteCalcint_int_0_0_State_pkg",
                  "MemWBint_int_0_0_State_pkg",
                  "Instr_pkg",
                  "MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg",
                  "DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State",
                  "ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State",
                  "MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State"});
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("RiscV/obj_dir/VRiscV") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestRiscV(debug).run("rv32i.bin", 0x37c))
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
