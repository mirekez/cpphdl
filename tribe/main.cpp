#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "File.h"

#define STAGES_NUM 3

#include "Decode.h"
#include "Execute.h"
#include "Writeback.h"

unsigned long sys_clock = -1;

class Tribe: public Module
{
    Decode          dec;
    Execute         exe;
    Writeback       wb;
    File<32,32>     regs;

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

    reg<u32>        pc;
    reg<u1>         valid;

    reg<u32>        alu_result_reg;

    reg<array<State,STAGES_NUM-1>> state_reg;

    // debug
    reg<u32>        debug_alu_a_reg;
    reg<u32>        debug_alu_b_reg;
    reg<u32>        debug_branch_target_reg;
    reg<u1>         debug_branch_taken_reg;


    __LAZY_COMB(stall_comb, bool)
        // hazard
        const auto& dec_state_tmp = dec.state_out();

        stall_comb = false;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex hazard
            if (state_reg[0].rd == dec_state_tmp.rs1) {
                stall_comb = true;
            }
            if (state_reg[0].rd == dec_state_tmp.rs2) {
                stall_comb = true;
            }
        }
        if ((state_reg[0].valid && state_reg[0].br_op != Br::BNONE)) {  // Ex branch
            stall_comb = true;
        }
        return stall_comb;
    }

    void forward()
    {
        const auto& dec_state_tmp = dec.state_out();

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem/Wb alu
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg.next[0].rs1_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)alu_result_reg);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg.next[0].rs2_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)alu_result_reg);
                }
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex/Mem alu
            if (dec_state_tmp.rs1 == state_reg[0].rd) {
                state_reg.next[0].rs1_val = exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)exe.alu_result_out());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg.next[0].rs2_val = exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)exe.alu_result_out());
                }
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && state_reg[1].rd != 0) {  // Mem/Wb mem
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg.next[0].rs1_val = dmem_read_data_in();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)dmem_read_data_in());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg.next[0].rs2_val = dmem_read_data_in();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)dmem_read_data_in());
                }
            }
        }
    }

public:

    void _work(bool reset)
    {
        if (debugen_in) {
            debug();
        }

        if (dmem_write_addr_out() == 0x11223344 && dmem_write_out()) {
            FILE* out = fopen("out.txt", "a");
            fprintf(out, "%c", dmem_write_data_out()&0xFF);
            fclose(out);
        }

        if (valid && !stall_comb_func()) {
            pc.next = pc + ((dec.instr_in()&3)==3?4:2);
        }
        if (state_reg[0].valid && exe.branch_taken_out()) {
            pc.next = exe.branch_target_out();
        }

        valid.next = true;

        state_reg.next[0] = dec.state_out();
        state_reg.next[0].valid = dec.instr_valid_in() && !stall_comb_func();
        forward();
        state_reg.next[1] = state_reg[0];
        alu_result_reg.next = exe.alu_result_out();
        debug_branch_target_reg = exe.branch_target_out();
        debug_branch_taken_reg = exe.branch_taken_out();

        regs._work(reset);
        dec._work(reset);
        exe._work(reset);
        wb._work(reset);

        if (reset) {
            state_reg.next[0].valid = 0;
            state_reg.next[1].valid = 0;
            pc.clr();
            valid.clr();
        }
    }

    void _work_neg(bool reset)
    {
    }

    void debug()
    {
        State tmp;
        Rv32im instr = {{{imem_read_data_in()}}};
        instr.decode(tmp);

        std::print("({:d}/{:d}){}: [{:s}]{:08x}  rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({:d})ops:{:02d}/{}/{}/{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({:d}){:08x} => mem({:d}/{:d}@{:08x}){:08x}/{:01x} ({:d})wop({:x}),r({:d}){:08x}@{:02d}",
            (bool)valid, (bool)stall_comb_func(), pc, instr.mnemonic(), (instr.raw&3)==3?instr.raw:(instr.raw|0xFFFF0000),
            (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
            (bool)state_reg[0].valid, (uint8_t)state_reg[0].alu_op, (uint8_t)state_reg[0].mem_op, (uint8_t)state_reg[0].br_op, (uint8_t)state_reg[0].wb_op,
            (int)state_reg[0].rs1, (int)state_reg[0].rs2, state_reg[0].rs1_val, state_reg[0].rs2_val, state_reg[0].imm, exe.alu_result_out(), (int)state_reg[0].rd,
            (bool)exe.branch_taken_out(), exe.branch_target_out(),
            (bool)exe.mem_write_out(), (bool)exe.mem_read_out(), exe.mem_write_addr_out(), exe.mem_write_data_out(), exe.mem_write_mask_out(),
            (bool)state_reg[1].valid, (uint8_t)state_reg[1].wb_op, (bool)wb.regs_write_out(), wb.regs_data_out(), wb.regs_wr_id_out());

#ifndef SYNTHESIS
            // delayed by 1 to align EX to WB
        std::string interpret;
        if (state_reg[1].valid && state_reg[1].alu_op != Alu::ANONE) {
            interpret += std::format("r{:02d} r{:02d} {:5s}({:08x},{:08x}) ", (int)state_reg[1].rs1, (int)state_reg[1].rs2, AOPS[state_reg[1].alu_op],
                             (uint32_t)debug_alu_a_reg, (uint32_t)debug_alu_b_reg);
        }
        if (state_reg[1].valid && state_reg[1].br_op != Br::BNONE && debug_branch_taken_reg) {
            interpret += std::format("{}({:08x}) rd={:02d} ", BOPS[state_reg[1].br_op], (uint32_t)debug_branch_target_reg, (int)state_reg[1].rd);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::LOAD) {
            interpret += std::format("LOAD({:08x}) ", (uint32_t)alu_result_reg);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE) {
            interpret += std::format("STOR({:08x}) {:08x} from r{:02d} ", (uint32_t)alu_result_reg, state_reg[1].rs2_val, (int)state_reg[1].rs2);
        }
        if (state_reg[1].valid && state_reg[1].wb_op != Wb::WNONE && wb.regs_write_out()) {
            interpret += std::format("wb {:08x} from {} to r{:02d} ", wb.regs_data_out(), WOPS[state_reg[1].wb_op], wb.regs_wr_id_out());
        }
            //
        std::print(": {}\n", interpret);
        debug_alu_a_reg.next = exe.debug_alu_a_out();
        debug_alu_b_reg.next = exe.debug_alu_b_out();
        debug_branch_target_reg = exe.branch_target_out();
#else
        std::print("\n");
#endif
    }

    void _strobe()
    {
        pc.strobe();
        valid.strobe();
        state_reg.strobe();
        alu_result_reg.strobe();
        debug_alu_a_reg.strobe();
        debug_alu_b_reg.strobe();
        debug_branch_target_reg.strobe();
        debug_branch_taken_reg.strobe();

        regs._strobe();
        dec._strobe();
        exe._strobe();
        wb._strobe();
    }

    void _connect()
    {
//        dec.state_in       = __VAR( state_reg[0] );  // execute stage input is same
        dec.pc_in          = __VAR( pc );
        dec.instr_valid_in = __VAR( valid );
        dec.instr_in       = imem_read_data_in;
        dec.regs_data0_in  = __EXPR( dec.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        dec.regs_data1_in  = __EXPR( dec.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        dec._connect();  // outputs are ready

        exe.state_in       = __VAR( state_reg[0] );
        exe._connect();  // outputs are ready

        wb.state_in       = __VAR( state_reg[1] );
        wb.mem_data_in    = dmem_read_data_in;
        wb.alu_result_in  = __VAR( alu_result_reg );
        wb._connect();  // outputs are ready

        regs.read_addr0_in = __EXPR( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = __EXPR( (uint8_t)dec.rs2_out() );
        regs.write_in = wb.regs_write_out;
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._connect();

        dmem_write_out      = exe.mem_write_out;
        dmem_write_addr_out = exe.mem_write_addr_out;
        dmem_write_data_out = exe.mem_write_data_out;
        dmem_write_mask_out = exe.mem_write_mask_out;
        dmem_read_out       = exe.mem_read_out;
        dmem_read_addr_out  = exe.mem_read_addr_out;
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

class TestTribe : public Module
{
    Ram<32,RAM_SIZE,0> imem;
    Ram<32,RAM_SIZE,1> dmem;

#ifdef VERILATOR
    VERILATOR_MODEL tribe;
#else
    Tribe tribe;
#endif

    bool imem_write = false;
    uint32_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

//    size_t i;

public:

    bool      debugen_in;

    TestTribe(bool debug)
    {
        debugen_in = debug;
    }

    ~TestTribe()
    {
    }

    void _connect()
    {
#ifndef VERILATOR
        tribe._connect();

        tribe.dmem_read_data_in = dmem.read_data_out;
        tribe.imem_read_data_in = imem.read_data_out;
        tribe.debugen_in = debugen_in;
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._connect();

        dmem.read_in = tribe.dmem_read_out;
        dmem.read_addr_in = tribe.dmem_read_addr_out;
        dmem.write_in = tribe.dmem_write_out;
        dmem.write_addr_in = tribe.dmem_write_addr_out;
        dmem.write_data_in = tribe.dmem_write_data_out;
        dmem.write_mask_in = tribe.dmem_write_mask_out;
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._connect();

        imem.read_in = __EXPR( !imem_write );
        imem.read_addr_in = tribe.imem_read_addr_out;
        imem.write_in = __VAR( imem_write );
        imem.write_addr_in = __VAR( imem_write_addr );
        imem.write_data_in = __VAR( imem_write_data );
        imem.write_mask_in = __EXPR( (uint8_t)0xF );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem._connect();
#else  // connecting Verilator to C++HDL
        dmem.read_in = __EXPR( (bool)tribe.dmem_read_out );
        dmem.read_addr_in = __EXPR( (uint32_t)tribe.dmem_read_addr_out );
        dmem.write_in = __EXPR( (bool)tribe.dmem_write_out );
        dmem.write_addr_in = __EXPR( (uint32_t)tribe.dmem_write_addr_out );
        dmem.write_data_in = __EXPR( (uint32_t)tribe.dmem_write_data_out );
        dmem.write_mask_in = __EXPR( (uint8_t)tribe.dmem_write_mask_out );
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._connect();

        imem.read_in = __EXPR( (bool)!imem_write );
        imem.read_addr_in = __EXPR( (uint32_t)tribe.imem_read_addr_out );
        imem.write_in = __EXPR( (bool)imem_write );
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
        tribe._work(reset);
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &tribe.data_out.m_storage;

        tribe.clk = 1;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
        tribe.dmem_read_data_in = dmem.read_data_out();
        tribe.imem_read_data_in = imem.read_data_out();
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
        tribe._strobe();
#endif
        dmem._strobe();  // we use these modules in Verilator test
        imem._strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
#else
        tribe._work_neg(reset);
#endif

        if (debugen_in) {
            printf("----------- %ld\n", sys_clock);
        }
    }

    void _strobe_neg()
    {
    }

    bool run(std::string filename, size_t start_offset)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTribe...");
#else
        std::print("C++HDL TestTribe...");
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", "w");
        fclose(out);

        __inst_name = "tribe_test";
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
            imem._strobe();
            ++sys_clock;
            imem_write_addr = addr*4;
            imem_write_data = ram[addr];
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
        ok &= VerilatorCompile(__FILE__, "Tribe", {
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
                  "Rv32im_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "File",
                  "Decode",
                  "Execute",
                  "Writeback"}, {"../../../../include", "../../../../tribe", "../../../../tribe/common", "../../../../tribe/spec"});
        std::cout << "Executing tests... ===========================================================================\n";
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Tribe/obj_dir/VTribe") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestTribe(debug).run("rv32i.bin", 0x37c))
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
