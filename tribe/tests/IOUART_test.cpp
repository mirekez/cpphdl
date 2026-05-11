#if defined(IOUART_DIRECT_VERILATOR)
#include "cpphdl.h"
#include "IOUART.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <sstream>

#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

#else
#define MAIN_FILE_INCLUDED
#include "../main.cpp"
#endif

#if !defined(SYNTHESIS)

long iouart_test_clock = -1;

static std::filesystem::path tribe_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path build_root_dir()
{
    std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd.parent_path().parent_path() / "cpphdl")) {
        return cwd.parent_path().parent_path();
    }
    if (std::filesystem::exists(cwd / "cpphdl")) {
        return cwd;
    }
    return cwd.parent_path().parent_path();
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

class IOUARTSmoke : public Module
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

    __PORT(bool) uart_valid_out;
    __PORT(uint8_t) uart_data_out;

private:
    IOUART<16, 4, 32> uart;

public:
    void _assign()
    {
        uart.axi_in.awvalid_in = awvalid_in;
        uart.axi_in.awaddr_in = awaddr_in;
        uart.axi_in.awid_in = awid_in;
        uart.axi_in.wvalid_in = wvalid_in;
        uart.axi_in.wdata_in = wdata_in;
        uart.axi_in.wlast_in = wlast_in;
        uart.axi_in.bready_in = bready_in;
        uart.axi_in.arvalid_in = arvalid_in;
        uart.axi_in.araddr_in = araddr_in;
        uart.axi_in.arid_in = arid_in;
        uart.axi_in.rready_in = rready_in;
        uart._assign();

        awready_out = uart.axi_in.awready_out;
        wready_out = uart.axi_in.wready_out;
        bvalid_out = uart.axi_in.bvalid_out;
        bid_out = uart.axi_in.bid_out;
        arready_out = uart.axi_in.arready_out;
        rvalid_out = uart.axi_in.rvalid_out;
        rdata_out = uart.axi_in.rdata_out;
        rlast_out = uart.axi_in.rlast_out;
        rid_out = uart.axi_in.rid_out;
        uart_valid_out = uart.uart_valid_out;
        uart_data_out = uart.uart_data_out;
    }

    void _work(bool reset)
    {
        uart._work(reset);
    }

    void _strobe()
    {
        uart._strobe();
    }
};

#if !defined(VERILATOR) || defined(IOUART_DIRECT_VERILATOR)
class TestIOUARTDirect : public Module
{
    static constexpr uint32_t REG_TXDATA = 0x00;
    static constexpr uint32_t REG_STATUS = 0x04;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    IOUARTSmoke dut;
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
    std::string captured;

    void fail(const char* message)
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
        dut.__inst_name = "iouart_smoke";
        dut._assign();
#endif
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
#ifdef IOUART_TOP_VERILATOR
        dut.axi_in___05Fawvalid_in = awvalid;
        dut.axi_in___05Fawaddr_in = (uint16_t)awaddr;
        dut.axi_in___05Fawid_in = (uint8_t)awid;
        dut.axi_in___05Fwvalid_in = wvalid;
        dut.axi_in___05Fwdata_in = (uint32_t)wdata;
        dut.axi_in___05Fwlast_in = wlast;
        dut.axi_in___05Fbready_in = bready;
        dut.axi_in___05Farvalid_in = arvalid;
        dut.axi_in___05Faraddr_in = (uint16_t)araddr;
        dut.axi_in___05Farid_in = (uint8_t)arid;
        dut.axi_in___05Frready_in = rready;
#else
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
#endif
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
        if (uart_valid()) {
            captured.push_back((char)uart_data());
        }
        ++iouart_test_clock;
    }

    bool awready()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Fawready_out;
#else
        return dut.awready_out;
#endif
#else
        return dut.awready_out();
#endif
    }

    bool wready()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Fwready_out;
#else
        return dut.wready_out;
#endif
#else
        return dut.wready_out();
#endif
    }

    bool bvalid()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Fbvalid_out;
#else
        return dut.bvalid_out;
#endif
#else
        return dut.bvalid_out();
#endif
    }

    bool arready()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Farready_out;
#else
        return dut.arready_out;
#endif
#else
        return dut.arready_out();
#endif
    }

    bool rvalid()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Frvalid_out;
#else
        return dut.rvalid_out;
#endif
#else
        return dut.rvalid_out();
#endif
    }

    uint32_t rdata()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.axi_in___05Frdata_out;
#else
        return dut.rdata_out;
#endif
#else
        return (uint32_t)dut.rdata_out();
#endif
    }

    bool uart_valid()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.uart_valid_out;
#else
        return dut.uart_valid_out;
#endif
#else
        return dut.uart_valid_out();
#endif
    }

    uint8_t uart_data()
    {
#ifdef VERILATOR
        eval(false);
#ifdef IOUART_TOP_VERILATOR
        return dut.uart_data_out;
#else
        return dut.uart_data_out;
#endif
#else
        return dut.uart_data_out();
#endif
    }

    void write32(uint32_t addr, uint32_t data)
    {
        awvalid = true;
        awaddr = u<16>(addr);
        awid = 1;
        wvalid = false;
        bready = true;
        if (!awready()) {
            fail("IOUART write address channel was not ready");
            return;
        }
        cycle();

        awvalid = false;
        wvalid = true;
        wdata = data;
        wlast = true;
        if (!wready()) {
            fail("IOUART write data channel was not ready");
            return;
        }
        cycle();

        wvalid = false;
        if (!bvalid()) {
            fail("IOUART write response was not valid");
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
        if (!arready()) {
            fail("IOUART read address channel was not ready");
            return 0;
        }
        cycle();

        arvalid = false;
        cycle();
        if (!rvalid()) {
            fail("IOUART read data channel was not valid");
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
        std::print("VERILATOR TestIOUART direct...");
#else
        std::print("CppHDL TestIOUART direct...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "iouart_test";
        _assign();

        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }
        write32(REG_TXDATA, 'O');
        write32(REG_TXDATA, 'K');
        write32(REG_TXDATA, '\n');
        if (captured != "OK\n") {
            fail("IOUART TX output mismatch");
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};
#endif

#if !defined(IOUART_DIRECT_VERILATOR)
static bool generate_iouart_direct_sv()
{
    const auto source_root = source_root_dir();
    const auto build_root = build_root_dir();
    std::filesystem::create_directory("generated");

    std::string cmd;
    cmd += shell_quote(build_root / "cpphdl");
    cmd += " " + shell_quote(std::filesystem::path(__FILE__));
    cmd += " -DIOUART_DIRECT_VERILATOR";
    cmd += " -I " + shell_quote(source_root / "include");
    cmd += " -I " + shell_quote(source_root / "tribe");
    cmd += " -I " + shell_quote(source_root / "tribe" / "common");
    cmd += " -I " + shell_quote(source_root / "tribe" / "devices");
    cmd += " -I " + shell_quote(source_root / "examples" / "axi");
    return std::system(cmd.c_str()) == 0;
}

static bool run_uart_elf(bool debug = false)
{
    const auto code_dir = tribe_code_dir();
    TestTribe test(debug);
    return test.run((code_dir / "uart.elf").string(),
        0,
        (code_dir / "uart.log").string(),
        100000,
        0,
        0,
        DEFAULT_RAM_SIZE,
        false);
}
#endif

int main(int argc, char** argv)
{
#if !defined(IOUART_DIRECT_VERILATOR)
    use_executable_workdir_if_needed(argv[0]);
#endif

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

    bool ok = true;
#if !defined(VERILATOR) || defined(IOUART_DIRECT_VERILATOR)
    ok = TestIOUARTDirect().run();
#endif

#if defined(IOUART_DIRECT_VERILATOR)
    return ok ? 0 : 1;
#else
    ok = ok && run_uart_elf(debug);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::cout << "Building IOUART direct Verilator smoke simulation...\n";
        ok &= generate_iouart_direct_sv();
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DIOUART_DIRECT_VERILATOR -DIOUART_TOP_VERILATOR", 1);
        if (ok) {
            ok &= VerilatorCompileInFolder(__FILE__, "IOUARTDevice", "IOUART",
                {"Predef_pkg"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "devices").string()},
                16, 4, 32);
            ok &= std::system("IOUARTDevice_16_4_32/obj_dir/VIOUART") == 0;
        }

        std::cout << "Building IOUART Tribe Verilator ELF simulation...\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileInFolder(__FILE__, "IOUART", "Tribe", {"Predef_pkg",
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
                  "Writeback",
                  "WritebackMem"}, {
                      (source_root / "include").string(),
                      (source_root / "tribe").string(),
                      (source_root / "tribe" / "common").string(),
                      (source_root / "tribe" / "spec").string(),
                      (source_root / "tribe" / "cache").string(),
                      (source_root / "tribe" / "devices").string()});
        ok &= std::system((std::string("IOUART/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
#endif
}

#endif
