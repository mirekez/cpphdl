#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

static constexpr size_t CHECKPOINT_ISR_INPUT_LEN = 512;

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

class ScopedEnv
{
    std::string name;
    std::string old_value;
    bool had_old = false;

public:
    ScopedEnv(const char* env_name, const std::string& value)
        : name(env_name)
    {
        if (const char* old = std::getenv(env_name)) {
            old_value = old;
            had_old = true;
        }
        setenv(env_name, value.c_str(), 1);
    }

    ~ScopedEnv()
    {
        if (had_old) {
            setenv(name.c_str(), old_value.c_str(), 1);
        }
        else {
            unsetenv(name.c_str());
        }
    }
};

static std::string checkpoint_prbs_input()
{
    std::string input;
    uint32_t x = 0x13579bdfu;
    for (size_t i = 0; i < CHECKPOINT_ISR_INPUT_LEN; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        input.push_back((char)('!' + (x % 94u)));
    }
    return input;
}

static bool write_file(const std::filesystem::path& path, const std::string& text)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        std::print("can't write {}\n", path.string());
        return false;
    }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

static bool validate_checkpoint_uart_log(const std::filesystem::path& path, const std::string& expected)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::print("can't read checkpoint UART output {}\n", path.string());
        return false;
    }
    std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (got != expected) {
        std::print("checkpoint UART output mismatch: got {} bytes, expected {} bytes\n",
            got.size(), expected.size());
        if (got.find("FAIL\n") != std::string::npos) {
            std::print("checkpoint ISR reported FAIL; PLIC claim loop or PRBS echo broke\n");
        }
        return false;
    }
    if (got.find("FAIL\n") != std::string::npos || got.find("PLIC0\nDONE\n") == std::string::npos) {
        std::print("checkpoint UART output did not prove clean PLIC claim-zero termination\n");
        return false;
    }
    return true;
}

static bool build_checkpoint_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "checkpoint_isr.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin -msmall-data-limit=0 -mno-relax";
    cmd += " -DCHECKPOINT_INPUT_LEN=" + std::to_string(CHECKPOINT_ISR_INPUT_LEN);
    cmd += " -nostdlib -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "c_start.S");
    cmd += " " + shell_quote(code_dir / "checkpoint_isr.S");
    cmd += " " + shell_quote(code_dir / "checkpoint_isr.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building checkpoint interrupt bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool run_checkpoint_segment(bool debug,
                                   const std::filesystem::path& elf,
                                   const std::filesystem::path& expected,
                                   const std::string& checkpoint_load,
                                   const std::string& checkpoint_save,
                                   uint64_t checkpoint_cycle,
                                   bool append_output,
                                   bool save_only,
                                   int max_cycles)
{
    return TestTribe(debug).run(elf.string(),
        0, expected.string(), max_cycles, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0,
        checkpoint_load, checkpoint_save, checkpoint_cycle, append_output, "",
        save_only, "", "Checkpoint ISR");
}

static bool run_checkpoint_cpp(bool debug)
{
    const auto elf = std::filesystem::current_path() / "checkpoint_isr.elf";
    const auto expected = std::filesystem::current_path() / "checkpoint_isr.log";
    const std::string input = checkpoint_prbs_input();
    const std::string ready_marker = "READY\n";
    const std::string expected_text = ready_marker + input + "PLIC0\nDONE\n";
    const std::vector<uint64_t> save_cycles = {300, 1200, 3000, 7000, 13000, 22000};
    std::string previous_checkpoint;

    if (!write_file(expected, expected_text)) {
        return false;
    }
    for (size_t i = 0; i <= save_cycles.size(); ++i) {
        std::filesystem::remove(std::filesystem::current_path() / ("checkpoint_isr_" + std::to_string(i) + ".bin"));
    }

    ScopedEnv scripted_uart("TRIBE_UART_INPUT", input);
    ScopedEnv scripted_uart_after("TRIBE_UART_INPUT_AFTER", ready_marker);

    // Scenario: a kernel-style supervisor trap handler services UART RX via
    // PLIC supervisor external interrupts while the testbench repeatedly saves and
    // restores the whole machine. Each restore resumes the same PRBS input
    // stream and the final UART log must be byte-for-byte identical.
    for (size_t i = 0; i < save_cycles.size(); ++i) {
        const auto checkpoint = std::filesystem::current_path() / ("checkpoint_isr_" + std::to_string(i) + ".bin");
        if (!run_checkpoint_segment(debug, elf, expected,
                previous_checkpoint, checkpoint.string(), save_cycles[i],
                i != 0, true, (int)save_cycles[i] + 500)) {
            std::print("checkpoint segment {} failed\n", i);
            return false;
        }
        if (!std::filesystem::exists(checkpoint)) {
            std::print("checkpoint file was not created: {}\n", checkpoint.string());
            return false;
        }
        previous_checkpoint = checkpoint.string();
    }

    if (!run_checkpoint_segment(debug, elf, expected,
            previous_checkpoint, "", 0, true, false, 300000)) {
        return false;
    }
    return validate_checkpoint_uart_log(std::filesystem::current_path() / "out.txt", expected_text);
}

static bool run_checkpoint_verilator(bool debug,
                                     const std::filesystem::path& elf,
                                     const std::filesystem::path& expected,
                                     const std::string& input,
                                     const std::string& ready_marker)
{
    const auto source_root = source_root_dir();
    std::print("Building checkpoint Tribe Verilator ISR simulation...\n");
    std::string verilator_defines =
        "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH) +
        " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES) +
        " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE);
    setenv("CPPHDL_VERILATOR_CFLAGS", verilator_defines.c_str(), 1);
    if (!VerilatorCompileTribeInFolder(__FILE__, "Checkpoint", source_root)) {
        return false;
    }

    ScopedEnv scripted_uart("TRIBE_UART_INPUT", input);
    ScopedEnv scripted_uart_after("TRIBE_UART_INPUT_AFTER", ready_marker);
    std::string command = "Checkpoint/obj_dir/VTribe --program " + shell_quote(elf) +
        " --log " + shell_quote(expected) +
        " --cycles 300000 --boot-priv m";
    if (debug) {
        command += " --debug";
    }
    return std::system(command.c_str()) == 0;
}

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = build_checkpoint_elf();

#ifndef VERILATOR
    ok = ok && run_checkpoint_cpp(debug);
    if (ok && !noveril) {
        const auto elf = std::filesystem::current_path() / "checkpoint_isr.elf";
        const auto expected = std::filesystem::current_path() / "checkpoint_isr.log";
        const std::string input = checkpoint_prbs_input();
        const std::string ready_marker = "READY\n";
        ok = ok && run_checkpoint_verilator(debug, elf, expected, input, ready_marker);
    }
#else
    Verilated::commandArgs(argc, argv);
    const auto elf = std::filesystem::current_path() / "checkpoint_isr.elf";
    const auto expected = std::filesystem::current_path() / "checkpoint_isr.log";
    const std::string input = checkpoint_prbs_input();
    const std::string ready_marker = "READY\n";
    const std::string expected_text = ready_marker + input + "PLIC0\nDONE\n";
    ok = ok && write_file(expected, expected_text);
    ScopedEnv scripted_uart("TRIBE_UART_INPUT", input);
    ScopedEnv scripted_uart_after("TRIBE_UART_INPUT_AFTER", ready_marker);
    // Verilated DUT internals are not serialized by the C++ checkpoint file.
    // Keep Verilator coverage to the ISR/UART execution path; the restore
    // sequence is covered by the native C++ model above.
    ok = ok && TestTribe(debug).run(elf.string(),
        0, expected.string(), 300000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "",
        false, "", "Checkpoint ISR");
#endif

    return ok ? 0 : 1;
}

#endif
