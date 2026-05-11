#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "CLINT.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

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

static std::filesystem::path build_root_dir()
{
    std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd.parent_path().parent_path() / "tribe64" / "tribe64")) {
        return cwd.parent_path().parent_path();
    }
    if (std::filesystem::exists(cwd / "tribe64" / "tribe64")) {
        return cwd;
    }
    return cwd.parent_path().parent_path();
}

static bool build_clint_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "clint.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin";
    cmd += " -nostdlib -nostartfiles -Wl,-Ttext=0";
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "clint.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CLINT bare-metal ELF...\n");
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::print("CLINT ELF build failed with status {}\n", rc);
        return false;
    }
    return true;
}

static bool run_clint_elf_cpp(bool debug)
{
    const auto build_root = build_root_dir();
    const auto runner = build_root / "tribe64" / "tribe64";
    const auto code_dir = tribe_code_dir();
    const auto elf = std::filesystem::current_path() / "clint.elf";
    if (!std::filesystem::exists(runner)) {
        std::print("missing Tribe C++ runner: {}\n", runner.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(runner);
    cmd += " --noveril";
    if (debug) {
        cmd += " --debug";
    }
    cmd += " --program " + shell_quote(elf);
    cmd += " --log " + shell_quote(code_dir / "clint.log");
    cmd += " --cycles 100000";
    return std::system(cmd.c_str()) == 0;
}

static bool run_clint_elf_verilator(bool debug)
{
    const auto build_root = build_root_dir();
    const auto runner = build_root / "tribe64" / "tribe64";
    const auto verilator_runner = build_root / "tribe64" / "Tribe" / "obj_dir" / "VTribe";
    const auto code_dir = tribe_code_dir();
    const auto elf = std::filesystem::current_path() / "clint.elf";
    if (!std::filesystem::exists(runner)) {
        std::print("missing Tribe C++ runner: {}\n", runner.string());
        return false;
    }

    std::string build_cmd = "cd " + shell_quote(runner.parent_path()) + " && " + shell_quote(runner) + " 1 >/dev/null";
    if (!std::filesystem::exists(verilator_runner)
        && (std::system(build_cmd.c_str()) != 0 || !std::filesystem::exists(verilator_runner))) {
        std::print("failed to build Tribe Verilator runner: {}\n", verilator_runner.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(verilator_runner);
    if (debug) {
        cmd += " --debug";
    }
    cmd += " --program " + shell_quote(elf);
    cmd += " --log " + shell_quote(code_dir / "clint.log");
    cmd += " --cycles 100000";
    return std::system(cmd.c_str()) == 0;
}

class CLINTTest : public Module
{
public:
    __PORT(bool) awvalid_in;
    __PORT(u<16>) awaddr_in;
    __PORT(u<4>) awid_in;
    __PORT(bool) awready_out;

    __PORT(bool) wvalid_in;
    __PORT(logic<32>) wdata_in;
    __PORT(bool) wlast_in;
    __PORT(bool) wready_out;

    __PORT(bool) bready_in;
    __PORT(bool) bvalid_out;
    __PORT(u<4>) bid_out;

    __PORT(bool) arvalid_in;
    __PORT(u<16>) araddr_in;
    __PORT(u<4>) arid_in;
    __PORT(bool) arready_out;

    __PORT(bool) rready_in;
    __PORT(bool) rvalid_out;
    __PORT(logic<32>) rdata_out;
    __PORT(bool) rlast_out;
    __PORT(u<4>) rid_out;

private:
    CLINT<16, 4, 32> clint;

public:
    void _assign()
    {
        clint.axi_in.awvalid_in = awvalid_in;
        clint.axi_in.awaddr_in = awaddr_in;
        clint.axi_in.awid_in = awid_in;
        clint.axi_in.wvalid_in = wvalid_in;
        clint.axi_in.wdata_in = wdata_in;
        clint.axi_in.wlast_in = wlast_in;
        clint.axi_in.bready_in = bready_in;
        clint.axi_in.arvalid_in = arvalid_in;
        clint.axi_in.araddr_in = araddr_in;
        clint.axi_in.arid_in = arid_in;
        clint.axi_in.rready_in = rready_in;
        clint._assign();

        awready_out = clint.axi_in.awready_out;
        wready_out = clint.axi_in.wready_out;
        bvalid_out = clint.axi_in.bvalid_out;
        bid_out = clint.axi_in.bid_out;
        arready_out = clint.axi_in.arready_out;
        rvalid_out = clint.axi_in.rvalid_out;
        rdata_out = clint.axi_in.rdata_out;
        rlast_out = clint.axi_in.rlast_out;
        rid_out = clint.axi_in.rid_out;
    }

    void _work(bool reset)
    {
        clint._work(reset);
    }

    void _strobe()
    {
        clint._strobe();
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

class TestCLINT : public Module
{
    static constexpr uint32_t REG_MSIP = 0x0000;
    static constexpr uint32_t REG_MTIMECMP_LO = 0x4000;
    static constexpr uint32_t REG_MTIMECMP_HI = 0x4004;
    static constexpr uint32_t REG_MTIME_LO = 0xBFF8;
    static constexpr uint32_t REG_MTIME_HI = 0xBFFC;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    CLINTTest dut;
#endif

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

    void fail(const std::string& message)
    {
        std::print("\n{}\n", message);
        error = true;
    }

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.awvalid_in = __VAR(awvalid);
        dut.awaddr_in = __VAR(awaddr);
        dut.awid_in = __VAR(awid);
        dut.wvalid_in = __VAR(wvalid);
        dut.wdata_in = __VAR(wdata);
        dut.wlast_in = __VAR(wlast);
        dut.bready_in = __VAR(bready);
        dut.arvalid_in = __VAR(arvalid);
        dut.araddr_in = __VAR(araddr);
        dut.arid_in = __VAR(arid);
        dut.rready_in = __VAR(rready);
        dut.__inst_name = "clint";
        dut._assign();
#endif
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
        dut.awvalid_in = awvalid;
        dut.awaddr_in = (uint16_t)awaddr;
        dut.awid_in = (uint8_t)awid;
        dut.wvalid_in = wvalid;
        dut.wdata_in = (uint32_t)wdata;
        dut.wlast_in = wlast;
        dut.bready_in = bready;
        dut.arvalid_in = arvalid;
        dut.araddr_in = (uint16_t)araddr;
        dut.arid_in = (uint8_t)arid;
        dut.rready_in = rready;
        dut.reset = reset;
        dut.eval();
    }
#endif

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        dut.clk = 0;
        eval(reset);
        dut.clk = 1;
        eval(reset);
        dut.clk = 0;
        eval(reset);
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++sys_clock;
    }

    bool awready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.awready_out;
#else
        return dut.awready_out();
#endif
    }

    bool wready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.wready_out;
#else
        return dut.wready_out();
#endif
    }

    bool bvalid()
    {
#ifdef VERILATOR
        eval(false);
        return dut.bvalid_out;
#else
        return dut.bvalid_out();
#endif
    }

    bool arready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.arready_out;
#else
        return dut.arready_out();
#endif
    }

    bool rvalid()
    {
#ifdef VERILATOR
        eval(false);
        return dut.rvalid_out;
#else
        return dut.rvalid_out();
#endif
    }

    uint32_t rdata()
    {
#ifdef VERILATOR
        eval(false);
        return dut.rdata_out;
#else
        return (uint32_t)dut.rdata_out();
#endif
    }

    void write32(uint32_t addr, uint32_t data)
    {
        awvalid = true;
        awaddr = u<16>(addr);
        awid = 3;
        wvalid = false;
        bready = true;
        if (!awready()) {
            fail("CLINT write address channel was not ready");
            return;
        }
        cycle();

        awvalid = false;
        wvalid = true;
        wdata = data;
        wlast = true;
        if (!wready()) {
            fail("CLINT write data channel was not ready");
            return;
        }
        cycle();

        wvalid = false;
        if (!bvalid()) {
            fail("CLINT write response was not valid");
            return;
        }
        cycle();
        bready = false;
    }

    uint32_t read32(uint32_t addr)
    {
        arvalid = true;
        araddr = u<16>(addr);
        arid = 5;
        rready = false;
        if (!arready()) {
            fail("CLINT read address channel was not ready");
            return 0;
        }
        cycle();

        arvalid = false;
        if (!rvalid()) {
            fail("CLINT read data channel was not valid");
            return 0;
        }
        uint32_t data = rdata();
        rready = true;
        cycle();
        rready = false;
        return data;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestCLINT...");
#else
        std::print("CppHDL TestCLINT...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "clint_test";
        _assign();

        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }
        if (read32(REG_MSIP) != 0) {
            fail("MSIP register was nonzero after reset");
        }

        write32(REG_MSIP, 1);
        if (read32(REG_MSIP) != 1) {
            fail("MSIP write/read failed");
        }
        write32(REG_MSIP, 0);
        if (read32(REG_MSIP) != 0) {
            fail("MSIP clear failed");
        }

        write32(REG_MTIMECMP_HI, 0);
        write32(REG_MTIMECMP_LO, 100);
        write32(REG_MTIME_HI, 0);
        write32(REG_MTIME_LO, 0);
        uint32_t first_mtime = read32(REG_MTIME_LO);
        for (int i = 0; i < 130; ++i) {
            cycle();
        }
        uint32_t later_mtime = read32(REG_MTIME_LO);
        if (later_mtime <= first_mtime) {
            fail("mtime did not advance");
        }
        if (later_mtime < 100) {
            fail("mtime did not reach programmed mtimecmp value");
        }
        write32(REG_MTIMECMP_LO, 0xffffffffu);
        write32(REG_MTIMECMP_HI, 0xffffffffu);
        if (read32(REG_MTIMECMP_LO) != 0xffffffffu || read32(REG_MTIMECMP_HI) != 0xffffffffu) {
            fail("mtimecmp readback failed");
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

#if defined(VERILATOR) && defined(CLINT_DIRECT_VERILATOR)
    Verilated::commandArgs(argc, argv);
    return TestCLINT().run() ? 0 : 1;
#else
    bool ok = build_clint_elf();
    ok = ok && TestCLINT().run();
    ok = ok && run_clint_elf_cpp(debug);
#ifndef VERILATOR
    if (ok && !noveril) {
        std::cout << "Building CLINT Verilator simulation...\n";
        const auto source_root = source_root_dir();
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DCLINT_DIRECT_VERILATOR", 1);
        ok &= VerilatorCompileInFolder(__FILE__, "CLINT", "CLINT",
            {"Predef_pkg", "CLINT"},
            {(source_root / "include").string(),
             (source_root / "tribe" / "common").string(),
             (source_root / "tribe" / "devices").string()});
        ok &= std::system("CLINT/obj_dir/VCLINT") == 0;
        ok &= run_clint_elf_verilator(debug);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
#endif
}

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
