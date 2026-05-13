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

#if !defined(VERILATOR) || defined(IOUART_DIRECT_VERILATOR)
class TestIOUARTDirect : public Module
{
    static constexpr uint32_t REG_TXDATA = 0x00;
    static constexpr uint32_t REG_STATUS = 0x04;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    IOUART<16, 4, 32> dut;
#endif

    Axi4Driver<16, 4, 32> axi = {};
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
        AXI4_DRIVER_FROM_DRIVER(dut.axi_in, axi);
        dut.__inst_name = "iouart";
        dut._assign();
#endif
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
        AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER(dut, axi_in, axi);
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
        return dut.axi_in___05Fawready_out;
#else
        return dut.axi_in.awready_out();
#endif
    }

    bool wready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.axi_in___05Fwready_out;
#else
        return dut.axi_in.wready_out();
#endif
    }

    bool bvalid()
    {
#ifdef VERILATOR
        eval(false);
        return dut.axi_in___05Fbvalid_out;
#else
        return dut.axi_in.bvalid_out();
#endif
    }

    bool arready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.axi_in___05Farready_out;
#else
        return dut.axi_in.arready_out();
#endif
    }

    bool rvalid()
    {
#ifdef VERILATOR
        eval(false);
        return dut.axi_in___05Frvalid_out;
#else
        return dut.axi_in.rvalid_out();
#endif
    }

    uint32_t rdata()
    {
#ifdef VERILATOR
        eval(false);
        return dut.axi_in___05Frdata_out;
#else
        return (uint32_t)dut.axi_in.rdata_out();
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
        axi.aw.valid = true;
        axi.aw.addr = u<16>(addr);
        axi.aw.id = 1;
        axi.w.valid = false;
        axi.b.ready = true;
        if (!awready()) {
            fail("IOUART write address channel was not ready");
            return;
        }
        cycle();

        axi.aw.valid = false;
        axi.w.valid = true;
        axi.w.data = data;
        axi.w.last = true;
        if (!wready()) {
            fail("IOUART write data channel was not ready");
            return;
        }
        cycle();

        axi.w.valid = false;
        if (!bvalid()) {
            fail("IOUART write response was not valid");
            return;
        }
        cycle();
        axi.b.ready = false;
    }

    uint32_t read32(uint32_t addr)
    {
        axi.ar.valid = true;
        axi.ar.addr = u<16>(addr);
        axi.ar.id = 2;
        axi.r.ready = false;
        if (!arready()) {
            fail("IOUART read address channel was not ready");
            return 0;
        }
        cycle();

        axi.ar.valid = false;
        cycle();
        if (!rvalid()) {
            fail("IOUART read data channel was not valid");
            return 0;
        }
        uint32_t data = rdata();
        axi.r.ready = true;
        cycle();
        axi.r.ready = false;
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
        std::cout << "Building IOUART direct Verilator simulation...\n";
        ok &= generate_iouart_direct_sv();
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DIOUART_DIRECT_VERILATOR", 1);
        if (ok) {
            ok &= VerilatorCompileInFolder(__FILE__, "IOUART", "IOUART",
                {"Predef_pkg"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "devices").string()},
                16, 4, 32);
            ok &= std::system("IOUART/obj_dir/VIOUART") == 0;
        }

        std::cout << "Building IOUART Tribe Verilator ELF simulation...\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "IOUART", source_root);
        ok &= std::system((std::string("IOUART/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
#endif
}

#endif
