#if defined(NS16550A_DIRECT_VERILATOR)
#include "cpphdl.h"
#include "NS16550A.h"

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

#else
#define MAIN_FILE_INCLUDED
#include "../main.cpp"
#endif

#if !defined(SYNTHESIS) || defined(NS16550A_DIRECT_VERILATOR)

#if defined(NS16550A_DIRECT_VERILATOR)
long sys_clock = -1;
#endif

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path tribe_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path build_root_dir()
{
    std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd / "tribe64" / "tribe64")) {
        return cwd;
    }
    if (std::filesystem::exists(cwd / "build" / "tribe64" / "tribe64")) {
        return cwd / "build";
    }
    if (std::filesystem::exists(cwd.parent_path() / "tribe64" / "tribe64")) {
        return cwd.parent_path();
    }
    if (std::filesystem::exists(cwd.parent_path().parent_path() / "tribe64" / "tribe64")) {
        return cwd.parent_path().parent_path();
    }

    const auto source_build = source_root_dir() / "build";
    if (std::filesystem::exists(source_build / "tribe64" / "tribe64")) {
        return source_build;
    }

    return cwd;
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

#if !defined(VERILATOR) || defined(NS16550A_DIRECT_VERILATOR)
class TestNS16550ADirect : public Module
{
    static constexpr uint32_t REG_RBR_THR_DLL = 0x00;
    static constexpr uint32_t REG_IER_DLM = 0x01;
    static constexpr uint32_t REG_IIR_FCR = 0x02;
    static constexpr uint32_t REG_LCR = 0x03;
    static constexpr uint32_t REG_LSR = 0x05;
    static constexpr uint32_t LSR_DR = 0x01;
    static constexpr uint32_t LSR_THRE_TEMT = 0x60;
    static constexpr uint32_t IIR_NO_INTERRUPT = 0x01;
    static constexpr uint32_t IIR_RX_AVAILABLE = 0x04;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    NS16550A<16, 4, 32> dut;
#endif

    Axi4Driver<16, 4, 32> axi = {};
    bool rx_valid = false;
    uint8_t rx_data = 0;
    bool error = false;
    std::string captured;

    void fail(const std::string& message)
    {
        std::print("\n{}\n", message);
        error = true;
    }

public:
    void _assign()
    {
#ifndef VERILATOR
        AXI4_DRIVER_FROM_DRIVER(dut.axi_in, axi);
        dut.uart_rx_valid_in = _ASSIGN_REG(rx_valid);
        dut.uart_rx_data_in = _ASSIGN_REG(rx_data);
        dut.__inst_name = "ns16550a";
        dut._assign();
#endif
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
        AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER(dut, axi_in, axi);
        dut.uart_rx_valid_in = rx_valid;
        dut.uart_rx_data_in = rx_data;
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
        ++sys_clock;
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

    bool irq()
    {
#ifdef VERILATOR
        eval(false);
        return dut.irq_out;
#else
        return dut.irq_out();
#endif
    }

    bool rx_ready()
    {
#ifdef VERILATOR
        eval(false);
        return dut.uart_rx_ready_out;
#else
        return dut.uart_rx_ready_out();
#endif
    }

    bool uart_valid()
    {
#ifdef VERILATOR
        eval(false);
        return dut.uart_valid_out;
#else
        return dut.uart_valid_out();
#endif
    }

    uint8_t uart_data()
    {
#ifdef VERILATOR
        eval(false);
        return dut.uart_data_out;
#else
        return dut.uart_data_out();
#endif
    }

    void write8(uint32_t addr, uint8_t data)
    {
        axi.aw.valid = true;
        axi.aw.addr = u<16>(addr);
        axi.aw.id = 1;
        axi.w.valid = false;
        axi.b.ready = true;
        if (!awready()) {
            fail("NS16550A write address channel was not ready");
            return;
        }
        cycle();

        axi.aw.valid = false;
        axi.w.valid = true;
        axi.w.data = logic<32>(uint32_t(data) << ((addr % 4) * 8));
        axi.w.last = true;
        if (!wready()) {
            fail("NS16550A write data channel was not ready");
            return;
        }
        cycle();

        axi.w.valid = false;
        if (!bvalid()) {
            fail("NS16550A write response was not valid");
            return;
        }
        cycle();
        axi.b.ready = false;
    }

    uint8_t read8(uint32_t addr)
    {
        axi.ar.valid = true;
        axi.ar.addr = u<16>(addr);
        axi.ar.id = 2;
        axi.r.ready = false;
        if (!arready()) {
            fail("NS16550A read address channel was not ready");
            return 0;
        }
        cycle();

        axi.ar.valid = false;
        if (!rvalid()) {
            fail("NS16550A read data channel was not valid");
            return 0;
        }
        uint32_t data = rdata();
        axi.r.ready = true;
        cycle();
        axi.r.ready = false;
        return uint8_t(data >> ((addr % 4) * 8));
    }

    void push_rx(uint8_t data)
    {
        if (!rx_ready()) {
            fail("NS16550A RX was not ready before byte injection");
            return;
        }
        rx_data = data;
        rx_valid = true;
        cycle();
        rx_valid = false;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestNS16550A direct...");
#else
        std::print("CppHDL TestNS16550A direct...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "ns16550a_test";
        _assign();

        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }

        if (!rx_ready()) {
            fail("RX ready was low after reset");
        }
        if (irq()) {
            fail("IRQ was high after reset");
        }
        if (read8(REG_LSR) != LSR_THRE_TEMT) {
            fail("LSR reset value did not report TX empty");
        }
        if (read8(REG_IIR_FCR) != IIR_NO_INTERRUPT) {
            fail("IIR did not report no-interrupt after reset");
        }

        write8(REG_IER_DLM, 0x01);
        if (irq()) {
            fail("IRQ asserted before RX data arrived");
        }
        push_rx('A');
        if (rx_ready()) {
            fail("RX ready stayed high while RX byte was buffered");
        }
        if (!irq()) {
            fail("RX interrupt did not assert with IER.ERBFI set");
        }
        if (read8(REG_IIR_FCR) != IIR_RX_AVAILABLE) {
            fail("IIR did not report RX available interrupt");
        }
        if (read8(REG_LSR) != (LSR_THRE_TEMT | LSR_DR)) {
            fail("LSR did not report RX data ready");
        }
        if (read8(REG_RBR_THR_DLL) != 'A') {
            fail("RBR read did not return injected RX byte");
        }
        if (irq()) {
            fail("IRQ stayed high after RBR drained RX byte");
        }
        if (!rx_ready()) {
            fail("RX ready did not return high after RBR read");
        }

        push_rx('B');
        write8(REG_IER_DLM, 0x00);
        if (irq()) {
            fail("IRQ stayed high after disabling IER.ERBFI");
        }
        if ((read8(REG_LSR) & LSR_DR) == 0) {
            fail("Disabling RX IRQ incorrectly dropped buffered RX data");
        }
        if (read8(REG_RBR_THR_DLL) != 'B') {
            fail("RBR read after disabling IRQ did not return buffered byte");
        }

        write8(REG_RBR_THR_DLL, 'O');
        write8(REG_RBR_THR_DLL, 'K');
        write8(REG_RBR_THR_DLL, '\n');
        if (captured != "OK\n") {
            fail("TX output mismatch");
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};
#endif

#if !defined(NS16550A_DIRECT_VERILATOR)
static bool build_ns16550a_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "ns16550a.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -nostdlib -nostartfiles -Wl,-Ttext=0";
    cmd += " " + shell_quote(code_dir / "ns16550a.S");
    cmd += " -o " + shell_quote(elf);
    return std::system(cmd.c_str()) == 0;
}

static bool generate_ns16550a_direct_sv()
{
    const auto source_root = source_root_dir();
    const auto build_root = build_root_dir();

    std::string cmd;
    cmd += shell_quote(build_root / "cpphdl");
    cmd += " " + shell_quote(std::filesystem::path(__FILE__));
    cmd += " -DNS16550A_DIRECT_VERILATOR";
    cmd += " -I " + shell_quote(source_root / "include");
    cmd += " -I " + shell_quote(source_root / "tribe");
    cmd += " -I " + shell_quote(source_root / "tribe" / "common");
    cmd += " -I " + shell_quote(source_root / "tribe" / "devices");
    return std::system(cmd.c_str()) == 0;
}

static bool run_ns16550a_elf(bool debug)
{
    TestTribe test(debug);
    return test.run((std::filesystem::current_path() / "ns16550a.elf").string(),
        0, (tribe_code_dir() / "ns16550a.log").string(), 100000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "NS16550A ELF");
}
#endif

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

    bool ok = true;
#if !defined(VERILATOR) || defined(NS16550A_DIRECT_VERILATOR)
    ok = TestNS16550ADirect().run();
#endif

#if defined(NS16550A_DIRECT_VERILATOR)
    return ok ? 0 : 1;
#else
    ok = ok && build_ns16550a_elf();
    ok = ok && run_ns16550a_elf(debug);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::cout << "Building NS16550A direct Verilator simulation...\n";
        ok &= generate_ns16550a_direct_sv();
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DNS16550A_DIRECT_VERILATOR", 1);
        if (ok) {
            ok &= VerilatorCompileInExactFolder(__FILE__, "NS16550A", "NS16550A",
                {"Predef_pkg"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "devices").string()},
                16, 4, 32);
            ok &= std::system("NS16550A/obj_dir/VNS16550A") == 0;
        }

        std::cout << "Building NS16550A Tribe Verilator ELF simulation...\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "NS16550A", source_root);
        ok &= std::system((std::string("NS16550A/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
#endif
}

#endif
