#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <chrono>
#include <cmath>
#include <cstdlib>
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
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        std::print("can't write {}\n", path.string());
        return false;
    }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

static bool build_perf_elf()
{
    const auto code_dir = tribe_code_dir();
    const auto gcc = riscv_home_dir() / "bin" / "riscv32-unknown-elf-gcc";
    const auto elf = std::filesystem::current_path() / "perf_kernel_slice.elf";

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
    cmd += " " + shell_quote(code_dir / "perf_kernel_slice.c");
    cmd += " -o " + shell_quote(elf);
    std::print("Building performance bare-metal ELF...\n");
    return std::system(cmd.c_str()) == 0;
}

static double percent(uint64_t value, uint64_t total)
{
    return total ? 100.0 * (double)value / (double)total : 0.0;
}

static bool check_metric(const char* name, double value, double expected, double tolerance_percent)
{
    const double tolerance = std::fabs(expected) * tolerance_percent / 100.0;
    const double lo = expected - tolerance;
    const double hi = expected + tolerance;
    if (value < lo || value > hi) {
        std::print("PERF regression: {}={:.2f} expected {:.2f} +/- {:.1f}% [{:.2f}, {:.2f}]\n",
            name, value, expected, tolerance_percent, lo, hi);
        return false;
    }
    std::print("PERF metric: {}={:.2f} expected {:.2f} +/- {:.1f}%\n",
        name, value, expected, tolerance_percent);
    return true;
}

static bool check_metric_u64(const char* name, uint64_t value, uint64_t expected, double tolerance_percent)
{
    const double tolerance = std::fabs((double)expected) * tolerance_percent / 100.0;
    const double lo = (double)expected - tolerance;
    const double hi = (double)expected + tolerance;
    if ((double)value < lo || (double)value > hi) {
        std::print("PERF regression: {}={} expected {} +/- {:.1f}% [{:.0f}, {:.0f}]\n",
            name, value, expected, tolerance_percent, lo, hi);
        return false;
    }
    std::print("PERF metric: {}={} expected {} +/- {:.1f}%\n",
        name, value, expected, tolerance_percent);
    return true;
}

static bool run_perf_test(bool debug)
{
    const auto elf = std::filesystem::current_path() / "perf_kernel_slice.elf";
    const auto expected = std::filesystem::current_path() / "perf_kernel_slice.log";
    if (!write_file(expected, "PERF\n")) {
        return false;
    }

    TestTribe test(debug);
    const auto start = std::chrono::high_resolution_clock::now();
    const bool run_ok = test.run(elf.string(),
        0, expected.string(), 6000000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 1, false, 0, "", false, "", 0, "", "", 0, false, "", false, "PERF\n",
        "Perf kernel slice");
    const auto stop = std::chrono::high_resolution_clock::now();
    const uint64_t wall_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
    const auto perf = test.perf_snapshot();

    const double stall_pct = percent(perf.stall, perf.clocks);
    const double issue_pct = percent(perf.icache_issue_wait, perf.clocks);
    const double total_stall_pct = percent(perf.stall + perf.icache_issue_wait, perf.clocks);
    const double hazard_pct = percent(perf.hazard, perf.clocks);
    const double dcache_wait_pct = percent(perf.dcache_wait, perf.clocks);
    const double icache_wait_pct = percent(perf.icache_wait, perf.clocks);
    const double branch_pct = percent(perf.branch, perf.clocks);
    const double icache_refill_pct = percent(perf.icache_refill_wait, perf.clocks);

    std::print("PERF summary: wall_us={} clocks={} stall={:.2f}% issue_wait={:.2f}% total_stall={:.2f}% hazard={:.2f}% dcache_wait={:.2f}% icache_wait={:.2f}% branch={:.2f}% icache_refill={:.2f}%\n",
        wall_us,
        perf.clocks,
        stall_pct,
        issue_pct,
        total_stall_pct,
        hazard_pct,
        dcache_wait_pct,
        icache_wait_pct,
        branch_pct,
        icache_refill_pct);

    if (const char* calibrate = std::getenv("TRIBE_PERF_PRINT_BASELINE")) {
        (void)calibrate;
        std::print("PERF baseline constants:\n");
        std::print("  clocks={} wall_us={}\n", perf.clocks, wall_us);
        std::print("  stall_pct={:.2f} issue_pct={:.2f} total_stall_pct={:.2f} hazard_pct={:.2f} dcache_wait_pct={:.2f} icache_wait_pct={:.2f} branch_pct={:.2f} icache_refill_pct={:.2f}\n",
            stall_pct,
            issue_pct,
            total_stall_pct,
            hazard_pct,
            dcache_wait_pct,
            icache_wait_pct,
            branch_pct,
            icache_refill_pct);
    }

    if (!run_ok) {
        return false;
    }

    bool ok = true;
#ifdef VERILATOR
    constexpr uint64_t expected_clocks = 4626979;
    constexpr uint64_t expected_wall_us = 32318844;
    constexpr double expected_stall_pct = 51.08;
    constexpr double expected_issue_pct = 22.90;
    constexpr double expected_total_stall_pct = 73.97;
    constexpr double expected_dcache_wait_pct = 12.62;
    constexpr double expected_icache_wait_pct = 36.84;
    constexpr double expected_branch_pct = 1.62;
    constexpr double expected_icache_refill_pct = 35.29;
#else
    constexpr uint64_t expected_clocks = 4284023;
    constexpr uint64_t expected_wall_us = 39487501;
    constexpr double expected_stall_pct = 47.66;
    constexpr double expected_issue_pct = 24.73;
    constexpr double expected_total_stall_pct = 72.39;
    constexpr double expected_dcache_wait_pct = 12.16;
    constexpr double expected_icache_wait_pct = 33.75;
    constexpr double expected_branch_pct = 1.75;
    constexpr double expected_icache_refill_pct = 32.08;
#endif

    // Baselines are calibrated to a deterministic supervisor-mode workload
    // that deliberately walks a Linux-sized text footprint. The hard stall
    // counter is the important Linux-like guard: a 5M-cycle Linux boot slice
    // currently reports about 52% stalled, and this ELF stays within 10%.
    ok = ok && check_metric_u64("clocks", perf.clocks, expected_clocks, 10.0);
    ok = ok && check_metric_u64("wall_us", wall_us, expected_wall_us, 15.0);
    ok = ok && check_metric("stall_pct", stall_pct, expected_stall_pct, 10.0);
    ok = ok && check_metric("issue_wait_pct", issue_pct, expected_issue_pct, 10.0);
    ok = ok && check_metric("total_stall_pct", total_stall_pct, expected_total_stall_pct, 10.0);
    ok = ok && check_metric("hazard_pct", hazard_pct, 0.0, 0.0);
    ok = ok && check_metric("dcache_wait_pct", dcache_wait_pct, expected_dcache_wait_pct, 10.0);
    ok = ok && check_metric("icache_wait_pct", icache_wait_pct, expected_icache_wait_pct, 10.0);
    ok = ok && check_metric("branch_pct", branch_pct, expected_branch_pct, 15.0);
    ok = ok && check_metric("icache_refill_pct", icache_refill_pct, expected_icache_refill_pct, 10.0);
    return ok;
}

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            debug = true;
        }
        else if (arg == "--noveril") {
            noveril = true;
        }
        else {
            std::print("Unknown option: {}\n", arg);
            return 2;
        }
    }

#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
    return run_perf_test(debug) ? 0 : 1;
#else
    bool ok = build_perf_elf();
    ok = ok && run_perf_test(debug);

    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building Perf Tribe Verilator ELF simulation...\n");
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "Perf", source_root);
        ok &= std::system((std::string("Perf/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
    return ok ? 0 : 1;
#endif
}

#endif
