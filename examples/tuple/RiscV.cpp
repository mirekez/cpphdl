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
public:
    __PORT(bool)      dmem_write_out;
    __PORT(uint32_t)  dmem_write_addr_out;
    __PORT(uint32_t)  dmem_write_data_out;
    __PORT(uint8_t)   dmem_write_mask_out;
    __PORT(bool)      dmem_read_out;
    __PORT(uint32_t)  dmem_read_addr_out;
    __PORT(uint32_t)  dmem_read_data_in;
    __PORT(uint32_t)  imem_read_addr_out = __VAR( pc );
    __PORT(uint32_t)  imem_read_data_in;
    bool              debugen_in;

private:
    File<32,32>         regs;

    reg<u32>            pc;
    reg<u1>             valid;

public:
    void _work(bool reset)
    {
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);  // only refs can be declared in any place of function, vars must be declared in the beginning (like in C)
//        auto& wb = std::get<2>(members);

        if (reset) {
            pc.clr();
            valid.clr();
            return;
        }

        if (debugen_in) {
            debug();
        }

        if (dmem_write_addr_out() == 0x11223344 && dmem_write_out()) {
            FILE* out = fopen("out.txt", "a");
            fprintf(out, "%c", dmem_write_data_out()&0xFF);
            fclose(out);
        }

        regs._work(reset);
        Pipeline::_work(reset);

        if (valid && !df.stall_out()) {
            pc.next = pc + ((df.instr_in()&3)==3?4:2);
        }
        if (states_comb_func()[0].valid && ex.branch_taken_out()) {
            pc.next = ex.branch_target_out();
        }

        valid.next = true;
    }

    void debug()
    {
        BIG_STATE tmp;
        Instr instr;
        auto state_comb_tmp = states_comb_func();  // we use it too often, lets cache it. It must not change during _work() function
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);  // only refs can be declared in any place of function, vars must be declared in the beginning (like in C)
        auto& wb = std::get<2>(members);

        instr = {imem_read_data_in()};
        if ((instr.raw&3) == 3) {
            instr.decode(tmp);
        }
        else {
            instr.decode16(tmp);
        }

        std::print("({:d}/{:d}){}: {:s} rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({:d})ops:{:02d}/{}/{}/{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({:d}){:08x} => mem({:d}/{:d}@{:08x}){:08x}/{:01x} ({:d})wop({:x}),r({:d}){:08x}@{:02d}",
            (bool)valid, (bool)df.stall_out(), pc, instr.mnemonic(),
            (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
            (bool)state_comb_tmp[0].valid, (uint8_t)state_comb_tmp[0].alu_op, (uint8_t)state_comb_tmp[0].mem_op, (uint8_t)state_comb_tmp[0].br_op, (uint8_t)state_comb_tmp[0].wb_op,
            (int)state_comb_tmp[0].rs1, (int)state_comb_tmp[0].rs2, state_comb_tmp[0].rs1_val, state_comb_tmp[0].rs2_val, state_comb_tmp[0].imm, ex.alu_result_out(), (int)state_comb_tmp[0].rd,
            (bool)ex.branch_taken_out(), ex.branch_target_out(),
            (bool)ex.mem_write_out(), (bool)ex.mem_read_out(),
            ex.mem_write_addr_out(), ex.mem_write_data_out(), ex.mem_write_mask_out(),
            (bool)state_comb_tmp[1].valid, (uint8_t)state_comb_tmp[1].wb_op, (bool)wb.regs_write_out(), wb.regs_data_out(), wb.regs_wr_id_out());

#ifndef SYNTHESIS
            // delayed by 1 to align EX to WB
        std::string interpret;
        if (state_comb_tmp[1].valid && state_comb_tmp[1].alu_op != Alu::ANONE) {
            interpret += std::format("r{:02d} r{:02d} {:5s}({:08x},{:08x}) ", (int)state_comb_tmp[1].rs1, (int)state_comb_tmp[1].rs2, AOPS[state_comb_tmp[1].alu_op],
                             state_comb_tmp[1].debug_alu_a, state_comb_tmp[1].debug_alu_b);
        }
        if (state_comb_tmp[1].valid && state_comb_tmp[1].br_op != Br::BNONE && state_comb_tmp[1].debug_branch_taken) {
            interpret += std::format("{}({:08x}) rd={:02d} ", BOPS[state_comb_tmp[1].br_op], state_comb_tmp[1].debug_branch_target, (int)state_comb_tmp[1].rd);
        }
        if (state_comb_tmp[1].valid && state_comb_tmp[1].mem_op == Mem::LOAD) {
            interpret += std::format("LOAD({:08x}) ", state_comb_tmp[1].alu_result);
        }
        if (state_comb_tmp[1].valid && state_comb_tmp[1].mem_op == Mem::STORE) {
            interpret += std::format("STOR({:08x}) {:08x} from r{:02d} ", state_comb_tmp[1].alu_result, state_comb_tmp[1].rs2_val, (int)state_comb_tmp[1].rs2);
        }
        if (state_comb_tmp[1].valid && state_comb_tmp[1].wb_op != Wb::WNONE && wb.regs_write_out()) {
            interpret += std::format("wb {:08x} from {} to r{:02d} ", wb.regs_data_out(), WOPS[state_comb_tmp[1].wb_op], wb.regs_wr_id_out());
        }
            //
        std::print(": {}\n", interpret);
#else
        std::print("\n");
#endif
    }

    void _strobe()
    {
        Pipeline::_strobe();
        regs._strobe();
        pc.strobe();
        valid.strobe();
    }


    void _connect()
    {
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
        auto& wb = std::get<2>(members);
        df.__inst_name = Pipeline::__inst_name + "/decode_fetch";
        ex.__inst_name = Pipeline::__inst_name + "/execute_calc";
        wb.__inst_name = Pipeline::__inst_name + "/write_back";

        df.pc_in          = __VAR( pc );
        df.instr_valid_in = __VAR( valid );
        df.instr_in       = imem_read_data_in;
        df.regs_data0_in  = __EXPR( df.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        df.regs_data1_in  = __EXPR( df.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        df.alu_result_in  = ex.alu_result_out;
        df.mem_data_in    = dmem_read_data_in;
        df._connect();  // outputs are ready
        // ex has no own inputs
        ex._connect();  // outputs are ready
        dmem_write_out      = ex.mem_write_out;
        dmem_write_addr_out = ex.mem_write_addr_out;
        dmem_write_data_out = ex.mem_write_data_out;
        dmem_write_mask_out = ex.mem_write_mask_out;
        dmem_read_out       = ex.mem_read_out;
        dmem_read_addr_out  = ex.mem_read_addr_out;
        wb.mem_data_in    = dmem_read_data_in;
        wb._connect();  // outputs are ready

        Pipeline::_connect();

        regs.read_addr0_in = df.rs1_out;
        regs.read_addr1_in = df.rs2_out;
        regs.write_in = wb.regs_write_out;
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.debugen_in = debugen_in;
        regs._connect();
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

#include <tuple>
#include <utility>

unsigned long sys_clock = -1;

class TestRiscV : public Module
{
    Ram<32,RAM_SIZE> imem;
    Ram<32,RAM_SIZE> dmem;

#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV riscv;
#endif

    bool imem_write = false;
    uint32_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

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

    void _connect()
    {
#ifndef VERILATOR
        riscv._connect();

        riscv.dmem_read_data_in = dmem.read_data_out;
        riscv.imem_read_data_in = imem.read_data_out;
        riscv.debugen_in = debugen_in;
        riscv.__inst_name = __inst_name + "/riscv";
        riscv._connect();

        dmem.read_in = riscv.dmem_read_out;
        dmem.read_addr_in = riscv.dmem_read_addr_out;
        dmem.write_in = riscv.dmem_write_out;
        dmem.write_addr_in = riscv.dmem_write_addr_out;
        dmem.write_data_in = riscv.dmem_write_data_out;
        dmem.write_mask_in = riscv.dmem_write_mask_out;
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._connect();

        imem.read_in = __EXPR( !imem_write );
        imem.read_addr_in = riscv.imem_read_addr_out;
        imem.write_in = __VAR( imem_write );
        imem.write_addr_in = __VAR( imem_write_addr );
        imem.write_data_in = __VAR( imem_write_data );
        imem.write_mask_in = __EXPR( (uint8_t)0xF );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem._connect();
#else  // connecting Verilator to C++HDL
        dmem.read_in = __EXPR( (bool)riscv.dmem_read_out );
        dmem.read_addr_in = __EXPR( (uint32_t)riscv.dmem_read_addr_out );
        dmem.write_in = __EXPR( (bool)riscv.dmem_write_out );
        dmem.write_addr_in = __EXPR( (uint32_t)riscv.dmem_write_addr_out );
        dmem.write_data_in = __VAR( riscv.dmem_write_data_out );
        dmem.write_mask_in = __EXPR( (uint8_t)riscv.dmem_write_mask_out );
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._connect();

        imem.read_in = __EXPR( !imem_write );
        imem.read_addr_in = __EXPR( (uint32_t)riscv.imem_read_addr_out );
        imem.write_in = __VAR( imem_write );
        imem.write_addr_in = __EXPR( (uint32_t)imem_write_addr );
        imem.write_data_in = __EXPR( (uint32_t)imem_write_data );
        imem.write_mask_in = __EXPR( (uint8_t)0xF );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem._connect();
#endif
    }

    void _work(bool reset)
    {
#ifndef VERILATOR
        riscv._work(reset);
#else
//        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        riscv.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

        riscv.clk = 1;
        riscv.reset = reset;
        riscv.eval();  // eval of verilator should be in the end
        riscv.dmem_read_data_in = dmem.read_data_out();
        riscv.imem_read_data_in = imem.read_data_out();
#endif
        dmem._work(reset);
        imem._work(reset);

        if (reset) {
            error = false;
            return;
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        riscv._strobe();
#endif
        dmem._strobe();  // we use these modules in Verilator test
        imem._strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        riscv.clk = 0;
        riscv.reset = reset;
        riscv.eval();  // eval of verilator should be in the end
#else
        riscv._work_neg(reset);
#endif

        if (debugen_in) {
            printf("----- %lx\n", sys_clock);
        }
    }

    void _strobe_neg()
    {
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
        _connect();
        _strobe();
        ++sys_clock;
        _work(1);
        _strobe_neg();
        _work_neg(1);

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
        imem._work(1);
        for (size_t addr = 0; addr < RAM_SIZE; ++addr) {
            imem_write_addr = addr*4;
            imem_write_data = ram[addr];
            imem._strobe();
            ++sys_clock;
            imem._work(0);
            if (debugen_in) {
                std::print("{:04x}: {:08x}\n", addr, ram[addr]);
            }
        }
        imem_write = false;
        fclose(fbin);
        dmem.ram.buffer = imem.ram.buffer;  // we need data from binary
        ///////////////////////////////////////

        auto start = std::chrono::high_resolution_clock::now();
        int cycles = 200000;
        while (--cycles && !error) {
            _strobe();
            ++sys_clock;
            _work(0);
            _strobe_neg();
            _work_neg(0);
        }

        std::ifstream a("rv32i.log", std::ios::binary), b("out.txt", std::ios::binary);
        error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));

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
        ok &= VerilatorCompile(__FILE__, "RiscV", {"File", "Memory",
                  "DecodeFetchint_int_0_0_State_pkg",
                  "ExecuteCalcint_int_0_0_State_pkg",
                  "MemWBint_int_0_0_State_pkg",
                  "Instr_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg",
                  "DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State",
                  "ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State",
                  "MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State"}, {"../../../../include"});
        std::cout << "Executing tests... ===========================================================================\n";
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("RiscV/obj_dir/VRiscV") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
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
