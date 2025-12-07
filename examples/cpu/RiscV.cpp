#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "Pipeline.h"
#include "File.h"
#include "../basic/Memory.cpp"

#include "DecodeFetch.h"
#include "ExecuteCalc.h"
#include "MemoryAccess.h"
#include "WriteBack.h"

template<size_t WIDTH>
class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemoryAccess,WriteBack>>
{
    File<32/8,32>       regs;
    reg<u32>            pc;
    reg<u1>             valid;

public:

    bool     *dmem_read_out;
    uint32_t *dmem_read_addr_out;
    uint32_t *dmem_read_data_in;
    bool     *dmem_write_out;
    uint32_t *dmem_write_addr_out;
    uint32_t *dmem_write_data_out;
    uint8_t  *dmem_write_mask_out;
    uint32_t *imem_read_addr_out;
    uint32_t *imem_read_data_in;
    bool      debugen_in;


    void connect()
    {
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
        auto& ma = std::get<2>(members);
        auto& wb = std::get<3>(members);

        df.__inst_name = Pipeline::__inst_name + "/decode_fetch";
        ex.__inst_name = Pipeline::__inst_name + "/execute_calc";
        ma.__inst_name = Pipeline::__inst_name + "/memory_access";
        wb.__inst_name = Pipeline::__inst_name + "/write_back";

        df.pc_in = &pc;
        df.instr_valid_in = &valid;
        df.instr_in = (Instr*) imem_read_data_in;
        df.regs_data0_in = (uint32_t *) regs.read_data0_out;
        df.regs_data1_in = (uint32_t *) regs.read_data1_out;
        df.alu_result_in = ex.alu_result_out;
        df.mem_data_in = dmem_read_data_in;

        Pipeline::connect();

        regs.read_addr0_in = &(*df.regs_rd_id_out)[0];
        regs.read_addr1_in = &(*df.regs_rd_id_out)[1];

        imem_read_addr_out = &pc;

        dmem_write_out = ma.mem_write_out;
        dmem_write_mask_out = ma.mem_write_mask_out;
        dmem_write_addr_out = ma.mem_write_addr_out;
        dmem_write_data_out = ma.mem_write_data_out;
        dmem_read_out = ma.mem_read_out;
        dmem_read_addr_out = ma.mem_read_addr_out;

        regs.write_in = wb.regs_write_out;
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = (logic<4UL*8>*) wb.regs_data_out;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        auto& df = std::get<0>(members);
        auto& ex = std::get<1>(members);
        auto& ma = std::get<2>(members);
        auto& wb = std::get<3>(members);
        if (reset) {
            pc.clr();
            valid.clr();
            return;
        }

        #ifndef SYNTHESIS
            std::print("({}/{}){}: {} r(:02d)/(:02d):{:08x}/{:08x}, reg1/2:{:08x}/{:08x} => aop({:x}),alu:{:08x} => mop{:x},{:08x}@({}/{}){:08x}/{:08x}, bop{},({}){:08x} => wop({:x}),rd:({:02d}){}\n",
                (int)valid, (int)*df.stall_out, pc, df.instr_in->format(), (*df.regs_rd_id_out)[0], (*df.regs_rd_id_out)[1],
                state_comb[0].rs1_val, state_comb[0].rs2_val, *df.regs_data0_in, *df.regs_data1_in,
                (uint8_t)state_comb[1].alu_op, ex.alu_result_comb_func(),
                (uint8_t)state_comb[2].mem_op, *ma.mem_write_addr_out, (int)*ma.mem_read_out, (int)*ma.mem_write_out, *ma.mem_write_data_out, *ma.mem_write_mask_out,
                (uint8_t)state_comb[2].br_op, (int)*ex.branch_taken_out, *ex.branch_target_out,
                (uint8_t)state_comb[3].wb_op, *wb.regs_wr_id_out, *wb.regs_data_out);
        #endif


        regs.work(clk, reset);
        Pipeline::work(clk, reset);

        if (!*df.stall_out) {
            if (*ex.branch_taken_out) {
                pc.next = *ex.branch_target_out/4;
            }
            else {
                pc.next = pc + 1;
            }
        }

        valid.next = !*df.stall_out;
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

        regs.comb();
        df.regs_rd_id_comb_func();
        df.do_decode_fetch();
        ex.alu_result_comb_func();
        ex.branch_taken_comb_func();
        ex.branch_target_comb_func();
        Pipeline::comb();
    }

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

#define PROG_SIZE 128

template<size_t WIDTH>
class TestRiscV : public Module
{
    Memory<32/8,PROG_SIZE>   imem;
    Memory<32/8,PROG_SIZE>   dmem;
#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV<WIDTH> riscv;
#endif

    bool imem_write;
    uint32_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

    size_t i;

public:
    uint32_t *dmem_read_data_out = (uint32_t*) dmem.read_data_out;
    uint32_t *imem_read_data_out = (uint32_t*) imem.read_data_out;

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
        riscv.__inst_name = __inst_name + "/riscv";

        riscv.dmem_read_data_in = (uint32_t*)dmem.read_data_out;
        riscv.imem_read_data_in = (uint32_t*)imem.read_data_out;
        riscv.debugen_in = debugen_in;
        riscv.connect();

        dmem.read_in = riscv.dmem_read_out;
        dmem.read_addr_in = (u<clog2(PROG_SIZE)>*) riscv.dmem_read_addr_out;
        dmem.write_in = riscv.dmem_write_out;
        dmem.write_mask_in = (logic<4>*) riscv.dmem_write_mask_out;
        dmem.write_addr_in = (u<clog2(PROG_SIZE)>*)riscv.dmem_write_addr_out;
        dmem.write_data_in = (logic<32>*)riscv.dmem_write_data_out;
        dmem.connect();

        imem.read_in = __ONE;
        imem.read_addr_in = (u<clog2(PROG_SIZE)>*) riscv.imem_read_addr_out;
        imem.write_in = &imem_write;
        imem.write_mask_in = (logic<4>*)&__ONES1024;
        imem.write_addr_in = (u<clog2(PROG_SIZE)>*)&imem_write_addr;
        imem.write_data_in = (logic<32>*)&imem_write_data;
        imem.connect();

#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        riscv.work(clk, reset);
#else
//        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        riscv.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

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
        dmem.strobe();
        imem.strobe();
#endif

    }

    void comb()
    {
#ifndef VERILATOR
        riscv.comb();
        dmem.read_data_out_comb_func();
        imem.read_data_out_comb_func();
#endif
    }

    bool run(std::string filename, size_t start_offset)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestRiscV, WIDTH: {}...", WiDTH);
#else
        std::print("C++HDL TestRiscV, WIDTH: {}...", WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        __inst_name = "riscv_test";
        connect();
        work(0, 1);
        work(1, 1);

        /////////////// read program to memory
        uint32_t ram[PROG_SIZE];
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        fseek(fbin, start_offset, SEEK_SET);
        int read_bytes = fread(ram, 1, 4*PROG_SIZE, fbin);
        std::print("Reading program into memory (size: {}, offset: {})\n", read_bytes, start_offset);

        imem_write = true;
        imem.work(1, 1);
        for (size_t addr = 0; addr < PROG_SIZE; ++addr) {
            imem_write_addr = addr;
            imem_write_data = ram[addr];
            imem.work(1, 0);
//            std::print("{:04x}: {:08x}\n", addr, ram[addr]);
        }
        imem_write = false;
        fclose(fbin);
        ///////////////////////////////////////

        auto start = std::chrono::high_resolution_clock::now();
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
/*    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 32);
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 64);
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("RiscV_32/obj_dir/VRiscV") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 0) || std::system((std::string("RiscV_64/obj_dir/VRiscV") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
    }*/
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestRiscV<32>(debug).run("rv32i.bin", 0x37c))
//        && ((only != -1 && only != 1) || TestRiscV<64>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
