#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>

static constexpr size_t CPU_IRQ_INPUT_LEN = 128;

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

static std::string cpu_irq_input()
{
    std::string input;
    uint32_t x = 0x2468ace1u;
    for (size_t i = 0; i < CPU_IRQ_INPUT_LEN; ++i) {
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

static bool build_cpu_bytecopy_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_bytecopy.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_bytecopy.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU byte-copy bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_unaligned_wordcopy_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_unaligned_wordcopy.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_unaligned_wordcopy.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU unaligned word-copy bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_trap_ra_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_trap_ra.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_trap_ra.S");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU trap-frame ra bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_sbi_return_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_sbi_return.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_sbi_return.S");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU SBI return bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_irq_load_hazard_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_irq_load_hazard.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32im_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin -msmall-data-limit=0 -mno-relax";
    cmd += " -DCPU_IRQ_INPUT_LEN=" + std::to_string(CPU_IRQ_INPUT_LEN);
    cmd += " -nostdlib -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "c_start.S");
    cmd += " " + shell_quote(code_dir / "checkpoint_isr.S");
    cmd += " " + shell_quote(code_dir / "cpu_irq_load_hazard.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU IRQ load-hazard bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_irq_atomic_hazard_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_irq_atomic_hazard.elf";

    if (!std::filesystem::exists(gcc)) {
        std::print("missing RISC-V compiler: {}\n", gcc.string());
        return false;
    }

    std::string cmd;
    cmd += shell_quote(gcc);
    cmd += " -march=rv32ima_zicsr -mabi=ilp32";
    cmd += " -O2 -g -ffreestanding -fno-builtin -msmall-data-limit=0 -mno-relax";
    cmd += " -DCPU_IRQ_INPUT_LEN=" + std::to_string(CPU_IRQ_INPUT_LEN);
    cmd += " -nostdlib -nostartfiles";
    cmd += " -T " + shell_quote(code_dir / "cpp_link.ld");
    cmd += " -I " + shell_quote(code_dir);
    cmd += " " + shell_quote(code_dir / "c_start.S");
    cmd += " " + shell_quote(code_dir / "checkpoint_isr.S");
    cmd += " " + shell_quote(code_dir / "cpu_irq_atomic_hazard.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU IRQ atomic-hazard bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool build_cpu_time_csr_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "cpu_time_csr.elf";

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
    cmd += " " + shell_quote(code_dir / "cpu_time_csr.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building CPU time CSR bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static bool run_cpu_fence_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_fence.elf").string(),
        0, (tribe_code_dir() / "cpu_fence.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU fence");
}

static bool run_cpu_trap_ra_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_trap_ra.elf").string(),
        0, (tribe_code_dir() / "cpu_trap_ra.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 1, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU trap ra");
}

static bool run_cpu_bytecopy_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_bytecopy.elf").string(),
        0, (tribe_code_dir() / "cpu_bytecopy.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU bytecopy");
}

static bool run_cpu_unaligned_wordcopy_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_unaligned_wordcopy.elf").string(),
        0, (tribe_code_dir() / "cpu_unaligned_wordcopy.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU unaligned wordcopy");
}

static bool run_cpu_sbi_return_cpp(bool debug)
{
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_sbi_return.elf").string(),
        0, (tribe_code_dir() / "cpu_sbi_return.log").string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 1, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU SBI return");
}

static bool run_cpu_irq_load_hazard_cpp(bool debug)
{
    const auto elf = std::filesystem::current_path() / "cpu_irq_load_hazard.elf";
    const auto expected = tribe_code_dir() / "cpu_irq_load_hazard.log";
    const std::string input = cpu_irq_input();
    const std::string ready_marker = "READY\n";
    const std::string expected_text = ready_marker + input + "IRQLOAD\nDONE\n";

    if (!write_file(expected, expected_text)) {
        return false;
    }

    ScopedEnv scripted_uart("TRIBE_UART_INPUT", input);
    ScopedEnv scripted_uart_after("TRIBE_UART_INPUT_AFTER", ready_marker);
    ScopedEnv scripted_uart_delay("TRIBE_UART_INPUT_CHAR_DELAY", "11");

    return TestTribe(debug).run(elf.string(),
        0, expected.string(), 500000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "",
        "CPU IRQ load hazard");
}

static bool run_cpu_irq_atomic_hazard_cpp(bool debug)
{
    const auto elf = std::filesystem::current_path() / "cpu_irq_atomic_hazard.elf";
    const auto expected = tribe_code_dir() / "cpu_irq_atomic_hazard.log";
    const std::string input = cpu_irq_input();
    const std::string ready_marker = "READY\n";
    const std::string expected_text = ready_marker + input + "IRQATOMIC\nDONE\n";

    if (!write_file(expected, expected_text)) {
        return false;
    }

    ScopedEnv scripted_uart("TRIBE_UART_INPUT", input);
    ScopedEnv scripted_uart_after("TRIBE_UART_INPUT_AFTER", ready_marker);
    ScopedEnv scripted_uart_delay("TRIBE_UART_INPUT_CHAR_DELAY", "7");

    return TestTribe(debug).run(elf.string(),
        0, expected.string(), 700000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "",
        "CPU IRQ atomic hazard");
}

static bool run_cpu_time_csr_cpp(bool debug)
{
    const auto log = tribe_code_dir() / "cpu_time_csr.log";
    if (!write_file(log, "TIMECSR\n")) {
        return false;
    }
    return TestTribe(debug).run((std::filesystem::current_path() / "cpu_time_csr.elf").string(),
        0, log.string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "CPU time CSR");
}

static bool run_cpu_bytecopy_checkpoint_cpp(bool debug)
{
    const auto elf = std::filesystem::current_path() / "cpu_bytecopy.elf";
    const auto log = tribe_code_dir() / "cpu_bytecopy.log";
    const auto checkpoint = std::filesystem::current_path() / "cpu_bytecopy.checkpoint";

    std::filesystem::remove(checkpoint);
    bool partial_ok = TestTribe(debug).run(elf.string(),
        0, log.string(), 301, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0,
        "", checkpoint.string(), 300, false, "", true, "", "CPU bytecopy checkpoint-save");
    if (!partial_ok) {
        std::print("checkpoint partial run did not save cleanly\n");
        return false;
    }
    if (!std::filesystem::exists(checkpoint)) {
        std::print("checkpoint file was not created: {}\n", checkpoint.string());
        return false;
    }

    return TestTribe(debug).run(elf.string(),
        0, log.string(), 200000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0,
        checkpoint.string(), "", 0, true, "", false, "", "CPU bytecopy checkpoint-restore");
}

static bool check_system_decode_has_no_decode_branch()
{
    struct Case
    {
        uint32_t raw;
        uint8_t sys;
        const char* name;
    };
    const Case cases[] = {
        {0x00000073, Sys::ECALL, "ecall"},
        {0x00100073, Sys::EBREAK, "ebreak"},
        {0x10500073, Sys::WFI, "wfi"},
        {0x0000100f, Sys::FENCEI, "fence.i"},
        {0xffffffff, Sys::TRAP, "illegal"},
    };

    // Scenario: decode of system instructions must not accidentally request a
    // branch operation. Trap and fence handling later in the pipe relies on the
    // system opcode path being independent from normal branch redirect logic.
    for (const auto& tc : cases) {
        Zicsr instr = {{{tc.raw}}};
        State state;
        instr.decode(state);
        if (state.sys_op != tc.sys || state.br_op != Br::BNONE) {
            std::print("bad system decode for {}: sys={} br={}\n",
                tc.name, (uint32_t)state.sys_op, (uint32_t)state.br_op);
            return false;
        }
    }

    {
        Rv32ic instr = {{{0x9002}}};
        State state;
        instr.decode(state);
        if (state.sys_op != Sys::EBREAK || state.trap_op != Trap::BREAKPOINT ||
            state.br_op != Br::BNONE || state.wb_op != Wb::WNONE) {
            std::print("bad compressed ebreak decode: sys={} trap={} br={} wb={} rd={}\n",
                (uint32_t)state.sys_op, (uint32_t)state.trap_op,
                (uint32_t)state.br_op, (uint32_t)state.wb_op, (uint32_t)state.rd);
            return false;
        }
    }
    return true;
}

static bool check_writeback_mem_address_tags()
{
    WritebackMem wb;
    State state{};
    uint32_t alu_addr = 0x1000;
    bool dcache_read_valid = false;
    uint32_t dcache_read_addr = 0;
    uint32_t dcache_read_data = 0;
    bool dcache_write_valid = false;
    uint32_t dcache_write_addr = 0;
    uint32_t dcache_write_data = 0;
    uint8_t dcache_write_mask = 0;
    bool hold = false;
    bool split_load = false;
    uint32_t split_low = 0;
    uint32_t split_high = 0;

    state.valid = true;
    state.wb_op = Wb::MEM;
    state.funct3 = 0b010;
    state.pc = 0x80;
    state.rd = 5;

    wb.state_in = _ASSIGN(state);
    wb.alu_result_in = _ASSIGN(alu_addr);
    wb.split_load_in = _ASSIGN(split_load);
    wb.split_load_low_addr_in = _ASSIGN(split_low);
    wb.split_load_high_addr_in = _ASSIGN(split_high);
    wb.dcache_read_valid_in = _ASSIGN(dcache_read_valid);
    wb.dcache_read_addr_in = _ASSIGN(dcache_read_addr);
    wb.dcache_read_data_in = _ASSIGN(dcache_read_data);
    wb.dcache_write_valid_in = _ASSIGN(dcache_write_valid);
    wb.dcache_write_addr_in = _ASSIGN(dcache_write_addr);
    wb.dcache_write_data_in = _ASSIGN(dcache_write_data);
    wb.dcache_write_mask_in = _ASSIGN(dcache_write_mask);
    wb.store_forward_enable_in = _ASSIGN(true);
    wb.hold_in = _ASSIGN(hold);
    wb._assign();

    // Scenario: a stale/previous D-cache response must not complete the current
    // writeback load. Linux can otherwise restore a trap frame with a wrong EPC.
    dcache_read_valid = true;
    dcache_read_addr = 0x2000;
    dcache_read_data = 0xdeadbeef;
    ++sys_clock;
    if (wb.load_ready_out() || wb.wb_mem_data_out() != 0) {
        std::print("WritebackMem accepted wrong-address load response: ready={} data={:08x}\n",
            (bool)wb.load_ready_out(), (uint32_t)wb.wb_mem_data_out());
        return false;
    }

    // A matching response should still complete normally.
    dcache_read_addr = alu_addr;
    dcache_read_data = 0x12345678;
    ++sys_clock;
    if (!wb.load_ready_out() || wb.load_result_out() != 0x12345678) {
        std::print("WritebackMem rejected matching load response: ready={} data={:08x}\n",
            (bool)wb.load_ready_out(), (uint32_t)wb.load_result_out());
        return false;
    }

    // While held, only a matching response may be latched for later writeback.
    hold = true;
    dcache_read_addr = 0x2000;
    dcache_read_data = 0xa5a5a5a5;
    ++sys_clock;
    wb._work(false);
    wb._strobe();

    hold = false;
    dcache_read_valid = false;
    ++sys_clock;
    if (wb.load_ready_out()) {
        std::print("WritebackMem latched wrong-address held response\n");
        return false;
    }

    hold = true;
    dcache_read_valid = true;
    dcache_read_addr = alu_addr;
    dcache_read_data = 0xcafef00d;
    ++sys_clock;
    wb._work(false);
    wb._strobe();

    hold = false;
    dcache_read_valid = false;
    ++sys_clock;
    if (!wb.load_ready_out() || wb.load_result_out() != 0xcafef00d) {
        std::print("WritebackMem failed held matching response: ready={} data={:08x}\n",
            (bool)wb.load_ready_out(), (uint32_t)wb.load_result_out());
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    bool noveril = false;
    bool debug = false;
    std::string only = std::getenv("CPU_TEST_ONLY") ? std::getenv("CPU_TEST_ONLY") : "";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }
    auto run_selected = [&](const char* name) {
        return only.empty() || only == name;
    };

    bool ok = check_system_decode_has_no_decode_branch();
    if (run_selected("writeback_mem_addr")) {
        ok = ok && check_writeback_mem_address_tags();
    }
    // Scenario: RISC-V time/timeh CSRs must expose the platform timer used by
    // CLINT/SBI, not the raw CPU cycle counter. Linux uses rdtime as its
    // clocksource and SBI set_timer deadlines are in the same timebase.
    if (run_selected("csr_time")) {
        ok = ok && build_cpu_time_csr_elf();
        ok = ok && run_cpu_time_csr_cpp(debug);
    }
    // Scenario: repeated MMIO polling uses fence iorw,iorw after each device
    // access. The fence must drain/serialize memory traffic without wedging the
    // current pipeline when the next instruction immediately returns to MMIO.
    if (run_selected("fence")) {
        ok = ok && build_cpu_fence_elf();
        ok = ok && run_cpu_fence_cpp(debug);
    }
    // Scenario: byte loads and stores around unaligned offsets must observe
    // dirty cached data instead of bypassing to stale RAM contents.
    if (run_selected("bytecopy")) {
        ok = ok && build_cpu_bytecopy_elf();
        ok = ok && run_cpu_bytecopy_cpp(debug);
    }
    // Scenario: Linux's RV32 unaligned-access probe issues a burst of
    // unaligned word loads followed by unaligned word stores. A held load
    // response must not be lost when the following store is already visible in
    // fetch/decode.
    if (run_selected("unaligned_wordcopy")) {
        ok = ok && build_cpu_unaligned_wordcopy_elf();
        ok = ok && run_cpu_unaligned_wordcopy_cpp(debug);
    }
    // Scenario: checkpoint save/restore must preserve enough architectural,
    // cache, device, and memory state that execution can resume mid-program and
    // produce the same UART log as an uninterrupted run.
    if (run_selected("bytecopy_checkpoint")) {
        ok = ok && build_cpu_bytecopy_elf();
        ok = ok && run_cpu_bytecopy_checkpoint_cpp(debug);
    }
    // Scenario: trap entry saves ra, trap return restores it, and the next ret
    // must reach the original sentinel target instead of looping at the trap PC.
    if (run_selected("trap_ra")) {
        ok = ok && build_cpu_trap_ra_elf();
        ok = ok && run_cpu_trap_ra_cpp(debug);
    }
    // Scenario: local SBI emulation must return both a0 and a1, and the first
    // instruction after ecall must see the updated a1 value. This catches the
    // v6.19 boot regression where SBI v0.2 was reported through a0 but a1 was
    // not architecturally visible soon enough.
    if (run_selected("sbi_return")) {
        ok = ok && build_cpu_sbi_return_elf();
        ok = ok && run_cpu_sbi_return_cpp(debug);
    }
    // Scenario: a UART/PLIC external interrupt can arrive while the main code
    // is continuously creating load-use dependencies. Trap-vector fetch must
    // not be held by a stale load hazard when no fetched instruction is valid.
    if (run_selected("irq_load_hazard")) {
        ok = ok && build_cpu_irq_load_hazard_elf();
        ok = ok && run_cpu_irq_load_hazard_cpp(debug);
    }
    // Scenario: an interrupt must not enter while LR/SC is holding the memory
    // pipeline busy. Otherwise the trap flush can leave atomic_busy asserted
    // with no valid instruction left, wedging trap-vector fetch at stvec.
    if (run_selected("irq_atomic_hazard")) {
        ok = ok && build_cpu_irq_atomic_hazard_elf();
        ok = ok && run_cpu_irq_atomic_hazard_cpp(debug);
    }

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building CPU fence Tribe Verilator ELF simulation...\n");
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "CPU", source_root);
        ok &= std::system((std::string("CPU/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
}

#endif
