#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

static std::filesystem::path tribe_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
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

static std::filesystem::path cpp_test_expected_log()
{
    return std::filesystem::current_path() / "cpp_test.log";
}

static bool write_cpp_test_expected_log()
{
    std::ofstream out(cpp_test_expected_log(), std::ios::binary);
    if (!out) {
        std::print("can't write expected C++ runtime log {}\n", cpp_test_expected_log().string());
        return false;
    }
    out << "cpp-runtime\n";
    out << "bss=0 data=17 ctor=42\n";
    out << "array=10 vector=92 string=109 heap=76\n";
    out << "PASS\n";
    return true;
}

static bool build_cpp_test_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto riscv_home = riscv_home_dir();
    const auto cxx = riscv_home / "bin" / "riscv32-unknown-elf-g++";
    const auto elf = std::filesystem::current_path() / "cpp_test.elf";
    const auto map = std::filesystem::current_path() / "cpp_test.map";

    if (!std::filesystem::exists(cxx)) {
        std::print("missing RISC-V C++ compiler: {}\n", cxx.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(cxx);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -std=gnu++20 -O2 -g";
    cmd += " -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics";
    cmd += " -ffunction-sections -fdata-sections";
    cmd += " -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -Wl,--gc-sections";
    cmd += " -Wl,-Map=" + shell_quote(map);
    cmd += " " + shell_quote(code_dir / "cpp_start.S");
    cmd += " " + shell_quote(code_dir / "cpp_syscalls.c");
    cmd += " " + shell_quote(code_dir / "cpp_test.cpp");
    cmd += " -o " + shell_quote(elf);

    std::print("Building bare-metal C++ ELF...\n");
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::print("C++ ELF build failed with status {}\n", rc);
        return false;
    }
    return true;
}

static bool run_cpp_program(bool debug = false)
{
    const auto elf = std::filesystem::current_path() / "cpp_test.elf";
    static constexpr int CPP_TEST_MAX_CYCLES = 50000000;
    TestTribe test(debug);
    return test.run(elf.string(),
        0,
        cpp_test_expected_log().string(),
        CPP_TEST_MAX_CYCLES,
        0,
        0,
        TRIBE_RAM_BYTES / 4,
        false);
}

int main(int argc, char** argv)
{
    use_executable_workdir_if_needed(argv[0]);

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

    bool ok = write_cpp_test_expected_log();
    ok = ok && build_cpp_test_elf();

#ifndef VERILATOR
    if (ok && !noveril) {
        std::cout << "Building Tribe Verilator C++ runtime simulation...\n";
        const auto source_root = source_root_dir();
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "Cpp", source_root);
        ok &= std::system((std::string("Cpp/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (ok) {
        ok = ok && run_cpp_program(debug);
    }
    return ok ? 0 : 1;
}

#endif
