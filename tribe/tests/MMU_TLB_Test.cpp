#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif

#if defined(VERILATOR) && defined(MMU_TLB_DIRECT_VERILATOR)
#include "cpphdl.h"
#include "../MMU_TLB.h"

#include <filesystem>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <cstring>

#include "../../examples/tools.h"

#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)

long sys_clock = -1;
#else
#define MAIN_FILE_INCLUDED

#include "../main.cpp"

#include <filesystem>
#include <print>
#include <string>
#include <cstring>
#endif

using namespace cpphdl;

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

static bool build_mmu_tlb_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "mmu_tlb.elf";

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
    cmd += " " + shell_quote(code_dir / "mmu_tlb.c");
    cmd += " -o " + shell_quote(elf);
    return std::system(cmd.c_str()) == 0;
}

#if !defined(VERILATOR) || defined(MMU_TLB_DIRECT_VERILATOR)
class TestMMUTLBDirect : public Module
{
    static constexpr uint8_t PTE_V = 1u << 0;
    static constexpr uint8_t PTE_R = 1u << 1;
    static constexpr uint8_t PTE_W = 1u << 2;
    static constexpr uint8_t PTE_X = 1u << 3;
    static constexpr uint8_t PTE_A = 1u << 6;
    static constexpr uint8_t PTE_D = 1u << 7;

#ifdef VERILATOR
    VERILATOR_MODEL mmu;
#else
    MMU_TLB<4> mmu;
#endif

    uint32_t vaddr = 0;
    bool read = false;
    bool write = false;
    bool execute = false;
    uint32_t satp = 0;
    u<2> priv = 1;
    bool fill = false;
    u<2> fill_index = 0;
    uint32_t fill_vpn = 0;
    uint32_t fill_ppn = 0;
    uint8_t fill_flags = 0;
    bool sfence = false;
    bool error = false;

    void check(bool condition, const char* message)
    {
        if (!condition) {
            std::print("\n{}\n", message);
            error = true;
        }
    }

public:
    void _assign()
    {
#ifndef VERILATOR
        mmu.vaddr_in = _ASSIGN_REG(vaddr);
        mmu.read_in = _ASSIGN_REG(read);
        mmu.write_in = _ASSIGN_REG(write);
        mmu.execute_in = _ASSIGN_REG(execute);
        mmu.satp_in = _ASSIGN_REG(satp);
        mmu.priv_in = _ASSIGN_REG(priv);
        mmu.fill_in = _ASSIGN_REG(fill);
        mmu.fill_index_in = _ASSIGN_REG(fill_index);
        mmu.fill_vpn_in = _ASSIGN_REG(fill_vpn);
        mmu.fill_ppn_in = _ASSIGN_REG(fill_ppn);
        mmu.fill_flags_in = _ASSIGN_REG(fill_flags);
        mmu.sfence_in = _ASSIGN_REG(sfence);
        mmu.__inst_name = "mmu";
        mmu._assign();
#endif
    }

    void _work(bool reset)
    {
#ifndef VERILATOR
        mmu._work(reset);
        if (reset) {
            error = false;
        }
#else
        if (reset) {
            error = false;
        }
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        mmu._strobe();
#endif
    }

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        mmu.clk = 0;
        eval(reset);
        mmu.clk = 1;
        eval(reset);
        mmu.clk = 0;
        eval(reset);
#else
        mmu._work(reset);
        mmu._strobe();
#endif
        ++sys_clock;
    }

#ifdef VERILATOR
    void eval(bool reset)
    {
        mmu.vaddr_in = vaddr;
        mmu.read_in = read;
        mmu.write_in = write;
        mmu.execute_in = execute;
        mmu.satp_in = satp;
        mmu.priv_in = (uint8_t)priv;
        mmu.fill_in = fill;
        mmu.fill_index_in = (uint8_t)fill_index;
        mmu.fill_vpn_in = fill_vpn;
        mmu.fill_ppn_in = fill_ppn;
        mmu.fill_flags_in = fill_flags;
        mmu.sfence_in = sfence;
        mmu.reset = reset;
        mmu.eval();
    }
#endif

    bool translated()
    {
#ifdef VERILATOR
        eval(false);
        return mmu.translated_out;
#else
        return mmu.translated_out();
#endif
    }

    bool hit()
    {
#ifdef VERILATOR
        eval(false);
        return mmu.hit_out;
#else
        return mmu.hit_out();
#endif
    }

    bool fault()
    {
#ifdef VERILATOR
        eval(false);
        return mmu.fault_out;
#else
        return mmu.fault_out();
#endif
    }

    bool miss()
    {
#ifdef VERILATOR
        eval(false);
        return mmu.miss_out;
#else
        return mmu.miss_out();
#endif
    }

    uint32_t paddr()
    {
#ifdef VERILATOR
        eval(false);
        return mmu.paddr_out;
#else
        return (uint32_t)mmu.paddr_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("Verilator TestMMUTLB direct...");
#else
        std::print("CppHDL TestMMUTLB direct...");
#endif
        __inst_name = "mmu_tlb_direct";
        _assign();
        for (int i = 0; i < 3; ++i) {
            cycle(true);
        }

        vaddr = 0x12345678u;
        read = true;
        satp = 0;
        priv = 1;
        cycle();
        check(!translated(), "bare mode should not translate");
        check(paddr() == vaddr, "bare mode should pass address through");
        check(!fault(), "bare mode should not fault");

        fill = true;
        fill_index = 0;
        fill_vpn = 0x12345u;
        fill_ppn = 0x23456u;
        fill_flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
        cycle();
        fill = false;

        satp = 0x80000000u;
        vaddr = 0x12345678u;
        read = true;
        write = false;
        execute = false;
        cycle();
        check(translated(), "Sv32 mode should translate in S-mode");
        check(hit(), "filled TLB entry should hit");
        check(!fault(), "valid readable TLB entry should not fault");
        check(paddr() == 0x23456678u, "TLB should translate VPN to PPN");

        write = true;
        cycle();
        check(!fault(), "dirty writable TLB entry should allow stores");

        sfence = true;
        cycle();
        sfence = false;
        write = false;
        cycle();
        check(miss(), "sfence.vma should invalidate TLB entries");
        check(fault(), "TLB miss should fault until a walker refills the entry");
        std::print(" {}\n", !error ? "PASSED" : "FAILED");
        return !error;
    }
};
#endif

#if !defined(SYNTHESIS)
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

#if defined(VERILATOR) && defined(MMU_TLB_DIRECT_VERILATOR)
    Verilated::commandArgs(argc, argv);
    return TestMMUTLBDirect().run() ? 0 : 1;
#elif defined(VERILATOR)
    Verilated::commandArgs(argc, argv);
    return TestTribe(debug).run((std::filesystem::current_path() / "mmu_tlb.elf").string(),
        0, (tribe_code_dir() / "mmu_tlb.log").string(), 100000, 0, 0, DEFAULT_RAM_SIZE, false) ? 0 : 1;
#else
    bool ok = TestMMUTLBDirect().run();
    ok = ok && build_mmu_tlb_elf();
    ok = ok && TestTribe(debug).run((std::filesystem::current_path() / "mmu_tlb.elf").string(),
        0, (tribe_code_dir() / "mmu_tlb.log").string(), 100000, 0, 0, DEFAULT_RAM_SIZE, false);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();

        std::print("Building MMU_TLB direct Verilator simulation...\n");
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DMMU_TLB_DIRECT_VERILATOR", 1);
        ok &= VerilatorCompile(__FILE__, "MMU_TLB", {"Predef_pkg"},
            {(source_root / "include").string(),
             (source_root / "tribe").string()}, 4);
        ok &= std::system("MMU_TLB_4/obj_dir/VMMU_TLB") == 0;

        if (ok) {
            std::print("Building MMU_TLB Tribe Verilator ELF simulation...\n");
            std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
            setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
            ok &= VerilatorCompileTribeInFolder(__FILE__, "MMU_TLB", source_root);
            ok &= std::system((std::string("MMU_TLB/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
        }
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    return ok ? 0 : 1;
#endif
}
#endif
