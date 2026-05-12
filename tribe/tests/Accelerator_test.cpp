#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path tribe_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path riscv_home_dir()
{
    if (const char* env = std::getenv("RISCV_HOME")) {
        return env;
    }
    if (const char* env = std::getenv("RISCV")) {
        return env;
    }
    return "/home/me/riscv";
}

static std::string shell_quote(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

static bool build_accelerator_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "accelerator.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin -msmall-data-limit=0 -mno-relax";
    cmd += " -nostdlib -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "c_start.S");
    cmd += " " + shell_quote(code_dir / "accelerator.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building accelerator bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

class DirectAcceleratorTest : public Module
{
    static constexpr uint32_t REG_SRC_ADDR = Accelerator<16, 4, 32, 64>::REG_SRC_ADDR;
    static constexpr uint32_t REG_DST_ADDR = Accelerator<16, 4, 32, 64>::REG_DST_ADDR;
    static constexpr uint32_t REG_LEN_WORDS = Accelerator<16, 4, 32, 64>::REG_LEN_WORDS;
    static constexpr uint32_t REG_CONTROL = Accelerator<16, 4, 32, 64>::REG_CONTROL;
    static constexpr uint32_t REG_STATUS = Accelerator<16, 4, 32, 64>::REG_STATUS;
    static constexpr uint32_t REG_PRBS_SEED = Accelerator<16, 4, 32, 64>::REG_PRBS_SEED;
    static constexpr uint32_t CTRL_START = Accelerator<16, 4, 32, 64>::CTRL_START;
    static constexpr uint32_t CTRL_DIR_A2M = Accelerator<16, 4, 32, 64>::CTRL_DIR_A2M;
    static constexpr uint32_t CTRL_PRBS = Accelerator<16, 4, 32, 64>::CTRL_PRBS;

    Accelerator<16, 4, 32, 64> dut;
    Axi4Ram<16, 4, 32, 128> dma_mem;
    bool awvalid = false;
    u<16> awaddr = 0;
    u<4> awid = 0;
    bool wvalid = false;
    logic<32> wdata = 0;
    bool wlast = false;
    bool bready = false;
    bool arvalid = false;
    u<16> araddr = 0;
    u<4> arid = 0;
    bool rready = false;
    bool error = false;

    void fail(const char* message)
    {
        std::print("\n{}\n", message);
        error = true;
    }

public:
    void _assign()
    {
        dut.axi_in.awvalid_in = _ASSIGN_REG(awvalid);
        dut.axi_in.awaddr_in = _ASSIGN_REG(awaddr);
        dut.axi_in.awid_in = _ASSIGN_REG(awid);
        dut.axi_in.wvalid_in = _ASSIGN_REG(wvalid);
        dut.axi_in.wdata_in = _ASSIGN_REG(wdata);
        dut.axi_in.wlast_in = _ASSIGN_REG(wlast);
        dut.axi_in.bready_in = _ASSIGN_REG(bready);
        dut.axi_in.arvalid_in = _ASSIGN_REG(arvalid);
        dut.axi_in.araddr_in = _ASSIGN_REG(araddr);
        dut.axi_in.arid_in = _ASSIGN_REG(arid);
        dut.axi_in.rready_in = _ASSIGN_REG(rready);
        dut.__inst_name = "accelerator";
        dut._assign();
        AXI4_DRIVER_FROM(dma_mem.axi_in, dut.dma_out);
        dma_mem.debugen_in = false;
        dma_mem.__inst_name = "dma_mem";
        dma_mem._assign();
        AXI4_RESPONDER_FROM(dut.dma_out, dma_mem.axi_in);
    }

    void cycle(bool reset = false)
    {
        dut._work(reset);
        dma_mem._work(reset);
        dut._strobe();
        dma_mem._strobe();
        ++sys_clock;
    }

    void write32(uint32_t addr, uint32_t data)
    {
        awvalid = true;
        awaddr = u<16>(addr);
        awid = 1;
        wvalid = false;
        bready = true;
        if (!dut.axi_in.awready_out()) {
            fail("accelerator write address not ready");
            return;
        }
        cycle();

        awvalid = false;
        wvalid = true;
        wdata = data;
        wlast = true;
        if (!dut.axi_in.wready_out()) {
            fail("accelerator write data not ready");
            return;
        }
        cycle();

        wvalid = false;
        if (!dut.axi_in.bvalid_out()) {
            fail("accelerator write response not valid");
            return;
        }
        cycle();
        bready = false;
    }

    uint32_t read32(uint32_t addr)
    {
        arvalid = true;
        araddr = u<16>(addr);
        arid = 2;
        rready = false;
        if (!dut.axi_in.arready_out()) {
            fail("accelerator read address not ready");
            return 0;
        }
        cycle();

        arvalid = false;
        if (!dut.axi_in.rvalid_out()) {
            fail("accelerator read data not valid");
            return 0;
        }
        uint32_t data = (uint32_t)dut.axi_in.rdata_out();
        rready = true;
        cycle();
        rready = false;
        return data;
    }

    bool wait_done()
    {
        for (int i = 0; i < 200; ++i) {
            uint32_t status = read32(REG_STATUS);
            if (status & 4u) {
                fail("accelerator reported error");
                return false;
            }
            if (status & 2u) {
                return true;
            }
            cycle();
        }
        fail("accelerator timed out");
        return false;
    }

    bool run()
    {
        std::print("CppHDL TestAccelerator direct...");
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "accelerator_direct_test";
        _assign();
        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }

        for (uint32_t i = 0; i < 8; ++i) {
            dma_mem.ram.buffer[16 + i] = (logic<32>)(0xa5000000u + i);
            dma_mem.ram.buffer[48 + i] = (logic<32>)0;
        }
        write32(REG_SRC_ADDR, 16 * 4);
        write32(REG_DST_ADDR, 4);
        write32(REG_LEN_WORDS, 8);
        write32(REG_CONTROL, CTRL_START);
        wait_done();

        write32(REG_SRC_ADDR, 4);
        write32(REG_DST_ADDR, 48 * 4);
        write32(REG_LEN_WORDS, 8);
        write32(REG_CONTROL, CTRL_START | CTRL_DIR_A2M);
        wait_done();
        for (uint32_t i = 0; i < 8; ++i) {
            if ((uint32_t)dma_mem.ram.buffer[48 + i] != (uint32_t)dma_mem.ram.buffer[16 + i]) {
                fail("DMA copy-back mismatch");
            }
        }

        write32(REG_PRBS_SEED, 0x12345678u);
        write32(REG_CONTROL, CTRL_START | CTRL_PRBS);
        wait_done();
        write32(REG_SRC_ADDR, 0);
        write32(REG_DST_ADDR, 64 * 4);
        write32(REG_LEN_WORDS, 4);
        write32(REG_CONTROL, CTRL_START | CTRL_DIR_A2M);
        wait_done();
        uint32_t first = 0x12345678u;
        first ^= first << 13;
        first ^= first >> 17;
        first ^= first << 5;
        for (uint32_t i = 0; i < 4; ++i) {
            if ((uint32_t)dma_mem.ram.buffer[64 + i] != first + i) {
                fail("PRBS DMA mismatch");
            }
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }

    bool ok = DirectAcceleratorTest().run();
    ok = ok && build_accelerator_elf();
    ok = ok && TestTribe(debug).run((std::filesystem::current_path() / "accelerator.elf").string(),
        0, (tribe_code_dir() / "accelerator.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building Accelerator Tribe Verilator ELF simulation...\n");
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
                  "Amo_pkg",
                  "Trap_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
                  "Rv32im_pkg",
                  "Rv32ia_pkg",
                  "Zicsr_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Sys_pkg",
                  "Csr_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "L1CachePerf_pkg",
                  "TribePerf_pkg",
                  "File",
                  "RAM1PORT",
                  "L1Cache",
                  "L2Cache",
                  "BranchPredictor",
                  "InterruptController",
                  "Decode",
                  "Execute",
                  "ExecuteMem",
                  "CSR",
                  "MMU_TLB",
                  "Writeback",
                  "WritebackMem"}, {
                      (source_root / "include").string(),
                      (source_root / "tribe").string(),
                      (source_root / "tribe" / "common").string(),
                      (source_root / "tribe" / "spec").string(),
                      (source_root / "tribe" / "cache").string(),
                      (source_root / "tribe" / "devices").string()});
        ok &= std::system((std::string("Tribe/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
}

#endif
