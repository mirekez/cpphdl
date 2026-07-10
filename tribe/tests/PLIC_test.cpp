#include "cpphdl.h"
#include "PLIC.h"

#include <chrono>
#include <cstdlib>
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

long _system_clock = -1;

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path build_root_dir()
{
    if (const char* build_dir = std::getenv("CPPHDL_BUILD_DIR")) {
        std::filesystem::path path(build_dir);
        if (std::filesystem::exists(path / "cpphdl")) {
            return path;
        }
    }

    std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd / "cpphdl")) {
        return cwd;
    }
    if (std::filesystem::exists(cwd / "build" / "cpphdl")) {
        return cwd / "build";
    }
    if (std::filesystem::exists(cwd.parent_path() / "cpphdl")) {
        return cwd.parent_path();
    }
    if (std::filesystem::exists(cwd.parent_path().parent_path() / "cpphdl")) {
        return cwd.parent_path().parent_path();
    }

    const auto source_build = source_root_dir() / "build";
    if (std::filesystem::exists(source_build / "cpphdl")) {
        return source_build;
    }

    return cwd;
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

template<size_t ADDR_WIDTH = 24, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 32>
class PLICTest : public Module
{
public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;
    _PORT(bool) source1_in;
    _PORT(bool) irq_out;

private:
    PLIC<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> plic;

public:
    void _assign()
    {
        AXI4_DRIVER_FROM(plic.axi_in, axi_in);
        plic.source_irq_in[0] = _ASSIGN(false);
        for (size_t i = 2; i < 32; ++i) {
            plic.source_irq_in[i] = _ASSIGN(false);
        }
        plic.source_irq_in[1] = source1_in;
        plic.__inst_name = "plic";
        plic._assign();
        AXI4_RESPONDER_FROM(axi_in, plic.axi_in);
        irq_out = plic.external_irq_out;
    }

    void _work(bool reset)
    {
        plic._work(reset);
    }

    void _strobe()
    {
        plic._strobe();
    }
};

template class PLICTest<24, 4, 32>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

class TestPLIC : public Module
{
    static constexpr uint32_t PRIORITY_SOURCE1 = 0x000004;
    static constexpr uint32_t ENABLE = 0x002000;
    static constexpr uint32_t THRESHOLD = 0x200000;
    static constexpr uint32_t CLAIM = 0x200004;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    PLICTest<24, 4, 32> dut;
#endif

    Axi4Driver<24, 4, 32> axi = {};
    bool source1 = false;
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
        AXI4_DRIVER_FROM_DRIVER(dut.axi_in, axi);
        dut.source1_in = _ASSIGN_REG(source1);
        dut.__inst_name = "plic_test";
        dut._assign();
#endif
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
        AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER(dut, axi_in, axi);
        dut.source1_in = source1;
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
        ++_system_clock;
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

    void write32(uint32_t addr, uint32_t data)
    {
        axi.aw.valid = true;
        axi.aw.addr = u<24>(addr);
        axi.aw.id = 1;
        axi.w.valid = false;
        axi.b.ready = true;
        if (!awready()) {
            fail("PLIC write address channel was not ready");
            return;
        }
        cycle();

        axi.aw.valid = false;
        axi.w.valid = true;
        axi.w.data = logic<32>(data);
        axi.w.last = true;
        if (!wready()) {
            fail("PLIC write data channel was not ready");
            return;
        }
        cycle();

        axi.w.valid = false;
        if (!bvalid()) {
            fail("PLIC write response was not valid");
            return;
        }
        cycle();
        axi.b.ready = false;
    }

    uint32_t read32(uint32_t addr)
    {
        axi.ar.valid = true;
        axi.ar.addr = u<24>(addr);
        axi.ar.id = 2;
        axi.r.ready = false;
        if (!arready()) {
            fail("PLIC read address channel was not ready");
            return 0;
        }
        cycle();

        axi.ar.valid = false;
        if (!rvalid()) {
            fail("PLIC read data channel was not valid");
            return 0;
        }
        uint32_t data = rdata();
        axi.r.ready = true;
        cycle();
        axi.r.ready = false;
        return data;
    }

    void set_source(bool value)
    {
        source1 = value;
        cycle();
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestPLIC...");
#else
        std::print("CppHDL TestPLIC...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "plic_test";
        _assign();

        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }

        if (irq()) {
            fail("PLIC IRQ was high after reset");
        }

        write32(PRIORITY_SOURCE1, 1);
        write32(ENABLE, 1u << 1);
        write32(THRESHOLD, 0);
        if (read32(CLAIM) != 0) {
            fail("PLIC claim was nonzero before a source interrupt");
        }
        if (irq()) {
            fail("PLIC IRQ asserted before source interrupt");
        }

        // Linux first observes a level interrupt from UART, reads the claim
        // register, then drains UART RBR/IIR so the source level drops.
        set_source(true);
        if (!irq()) {
            fail("PLIC did not raise IRQ for enabled source 1");
        }
        if (read32(CLAIM) != 1) {
            fail("PLIC first claim did not return source 1");
        }
        set_source(false);
        if (irq()) {
            fail("PLIC IRQ stayed high after claim consumed the only pending source");
        }

        // While the gateway is waiting for completion, extra claim reads must
        // return zero. This is the Linux plic_handle_irq loop condition.
        for (int i = 0; i < 4; ++i) {
            uint32_t claim = read32(CLAIM);
            if (claim != 0) {
                fail("PLIC repeated claim returned stale source " + std::to_string(claim));
            }
        }

        // Completion releases the gateway. With the UART source already low,
        // follow-up claim reads must still return zero and must not re-arm IRQ.
        write32(CLAIM, 1);
        if (irq()) {
            fail("PLIC IRQ reasserted after completing a low source");
        }
        for (int i = 0; i < 4; ++i) {
            uint32_t claim = read32(CLAIM);
            if (claim != 0) {
                fail("PLIC post-completion claim returned stale source " + std::to_string(claim));
            }
        }

        // A new level after completion must be observable, proving that the
        // previous stale-claim protection did not permanently block source 1.
        set_source(true);
        if (!irq()) {
            fail("PLIC did not re-raise IRQ after source 1 asserted again");
        }
        if (read32(CLAIM) != 1) {
            fail("PLIC second claim did not return source 1");
        }
        set_source(false);
        write32(CLAIM, 1);
        if (read32(CLAIM) != 0) {
            fail("PLIC final claim was nonzero after second completion");
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

static bool generate_plic_sv()
{
    const auto source_root = source_root_dir();
    const auto build_root = build_root_dir();

    std::string cmd;
    cmd += shell_quote(build_root / "cpphdl");
    cmd += " " + shell_quote(std::filesystem::path(__FILE__));
    cmd += " -I " + shell_quote(source_root / "include");
    cmd += " -I " + shell_quote(source_root / "tribe");
    cmd += " -I " + shell_quote(source_root / "tribe" / "common");
    cmd += " -I " + shell_quote(source_root / "tribe" / "devices");
    if (const char* toolchain_args = std::getenv("CPPHDL_TOOLCHAIN_ARGS")) {
        cmd += " ";
        cmd += toolchain_args;
    }
    return std::system(cmd.c_str()) == 0;
}

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
#endif

    bool ok = TestPLIC().run();

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::cout << "Building PLIC Verilator simulation...\n";
        ok &= generate_plic_sv();
        if (ok) {
            ok &= VerilatorCompileInExactFolder(__FILE__, "PLIC", "PLICTest",
                {"Predef_pkg", "PLIC"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "devices").string()},
                24, 4, 32);
            ok &= std::system("PLIC/obj_dir/VPLICTest") == 0;
        }
    }
#endif

    return ok ? 0 : 1;
}

#endif
