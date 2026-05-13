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

static bool build_cpu_fence_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_fence.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_fence.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU fence bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool run_cpu_fence_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_fence.elf").string(),
        0, (tribe_code_dir() / "cpu_fence.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false);
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

    bool ok = build_cpu_fence_elf();
    ok = ok && run_cpu_fence_cpp(debug);

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building CPU fence Tribe Verilator ELF simulation...\n");
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "CPU", source_root);
        ok &= std::system((std::string("CPUTribe/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
}

#endif
