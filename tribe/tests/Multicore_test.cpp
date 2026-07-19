#define MULTICORE
#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <cstring>
#include <filesystem>
#include <print>
#include <string>

// Multicore behavior under test:
// 1. Every configured hart starts with a distinct hart ID and private stack.
// 2. All harts share one L2 and observe each other's write-through L1 stores.
// 3. Native C++ and generated Verilator RTL both complete the same barriers.
// 4. Concurrent AMOs on one word are globally serialized across private L1 caches.
// 5. A peer store invalidates an LR reservation so the following SC fails.
// 6. An explicit UART success marker works even when a tohost address is configured.

static std::filesystem::path multicore_source_root()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path multicore_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path multicore_riscv_gxx()
{
    if (const char* riscv_home = std::getenv("RISCV_HOME")) {
        return std::filesystem::path(riscv_home) / "bin" / "riscv32-unknown-elf-g++";
    }
    if (const char* riscv = std::getenv("RISCV")) {
        return std::filesystem::path(riscv) / "bin" / "riscv32-unknown-elf-g++";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "riscv" / "bin" / "riscv32-unknown-elf-g++";
    }
    return "riscv32-unknown-elf-g++";
}

static std::string multicore_shell_quote(const std::filesystem::path& path)
{
    std::string quoted = "'";
    for (char ch : path.string()) {
        quoted += ch == '\'' ? "'\\''" : std::string(1, ch);
    }
    return quoted + "'";
}

static bool build_multicore_elf()
{
    const auto code_dir = multicore_code_dir();
    const auto compiler = multicore_riscv_gxx();
    const auto elf = std::filesystem::current_path() / "multicore_test.elf";

    if (!std::filesystem::exists(compiler)) {
        std::print("missing RISC-V compiler: {}\n", compiler.string());
        return false;
    }

    std::string command = multicore_shell_quote(compiler);
    command += " -march=rv32ima_zicsr -mabi=ilp32";
    command += " -O2 -g -ffreestanding -fno-builtin -fno-exceptions -fno-rtti";
    command += " -fno-threadsafe-statics -msmall-data-limit=0 -mno-relax";
    command += " -nostdlib -nostartfiles";
    command += " -T " + multicore_shell_quote(code_dir / "cpp_link.ld");
    command += " -I " + multicore_shell_quote(code_dir);
    command += " " + multicore_shell_quote(code_dir / "multicore_start.S");
    command += " " + multicore_shell_quote(code_dir / "multicore_test.cpp");
    command += " -o " + multicore_shell_quote(elf);
    std::print("Building {}-core bare-metal ELF...\n", CPUS_PER_L2_CACHE);
    return std::system(command.c_str()) == 0;
}

static bool run_multicore(bool debug)
{
    TestTribe test(debug);
    bool passed = test.run(
        (std::filesystem::current_path() / "multicore_test.elf").string(),
        0, "", 500000, 1, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false,
        "MULTICORE PASS", "multicore");
    return passed;
}

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = build_multicore_elf() && run_multicore(debug);
#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = multicore_source_root();
        std::string width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(
            __FILE__, "Multicore", source_root, TEST_TRIBE_CPU_CORES);
        ok &= std::system((std::string("Multicore/obj_dir/VTribeTest") +
            (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    return ok ? 0 : 1;
}

#endif
