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
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path local = std::filesystem::path(home) / "riscv";
        if (std::filesystem::exists(local)) {
            return local;
        }
    }
    if (std::filesystem::exists("/home/me/riscv")) {
        return "/home/me/riscv";
    }
    if (std::filesystem::exists("/home/mike/riscv")) {
        return "/home/mike/riscv";
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

static bool write_file(const std::filesystem::path& path, const std::string& text)
{
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        std::print("can't write {}\n", path.string());
        return false;
    }
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
    return true;
}

static bool build_ethgig_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto cxx = riscv_home_dir() / "bin" / "riscv32-unknown-elf-g++";
    const auto elf = std::filesystem::current_path() / "ethgig_test.elf";

    if (!std::filesystem::exists(cxx)) {
        std::print("missing RISC-V compiler: {}\n", cxx.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(cxx);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -msmall-data-limit=0 -mno-relax";
    cmd += " -nostdlib -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "c_start.S");
    cmd += " " + shell_quote(code_dir / "checkpoint_isr.S");
    cmd += " " + shell_quote(code_dir / "ethgig_test.cpp");
    cmd += " -o " + shell_quote(elf);
    std::print("Building ethgig bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool run_ethgig_cpu(bool debug)
{
    const auto expected = std::filesystem::current_path() / "ethgig_test.expected";
    if (!write_file(expected, "ETHGIG\nDONE\n")) {
        return false;
    }
    setenv("TRIBE_ETH_LOOPBACK", "1", 1);
    return TestTribe(debug).run((std::filesystem::current_path() / "ethgig_test.elf").string(),
        0, expected.string(), 400000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "EthGig CPU loopback");
}

static bool check_eth_dma_cache_invalidate_policy()
{
    bool ok = true;
    if (TestTribe::eth_dma_needs_cache_invalidate(true, false)) {
        std::print("EthGig cache invalidate ERROR: TX completion must not invalidate CPU cache\n");
        ok = false;
    }
    if (!TestTribe::eth_dma_needs_cache_invalidate(false, true)) {
        std::print("EthGig cache invalidate ERROR: RX completion must invalidate CPU cache\n");
        ok = false;
    }
    if (!TestTribe::eth_dma_needs_cache_invalidate(true, true)) {
        std::print("EthGig cache invalidate ERROR: RX must win when TX and RX IRQs are both pending\n");
        ok = false;
    }
    if (TestTribe::eth_dma_needs_cache_invalidate(false, false)) {
        std::print("EthGig cache invalidate ERROR: idle DMA must not invalidate CPU cache\n");
        ok = false;
    }
    return ok;
}

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

    bool ok = check_eth_dma_cache_invalidate_policy();
    ok = ok && build_ethgig_elf();
    ok = ok && run_ethgig_cpu(debug);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building EthGigCPU Tribe Verilator ELF simulation...\n");
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "EthGigCPU", source_root);
        ok &= std::system((std::string("EthGigCPU/obj_dir/VTribeTest") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
}

#endif
