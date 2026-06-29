#define MAIN_FILE_INCLUDED
#undef L2_AXI_WIDTH
#undef TRIBE_RAM_BYTES_CONFIG
#undef TRIBE_IO_REGION_SIZE_CONFIG
#undef TRIBE_CLINT_TICK_DIV_CONFIG
#define L2_AXI_WIDTH 64
#define TRIBE_RAM_BYTES_CONFIG (32 * 1024 * 1024)
#define TRIBE_IO_REGION_SIZE_CONFIG (4 * 1024 * 1024)
#define TRIBE_CLINT_TICK_DIV_CONFIG 256
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

static std::filesystem::path tribe_linux_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "linux";
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
    return true;
}

static bool check_metric_u64_max_ratio(const char* name, uint64_t value, uint64_t expected, double max_ratio)
{
    const double hi = (double)expected * max_ratio;
    if ((double)value > hi) {
        std::print("PERF regression: {}={} expected at most {:.1f}x baseline {} [{:.0f}]\n",
            name, value, max_ratio, expected, hi);
        return false;
    }
    return true;
}

static bool run_perf_test(bool debug)
{
    const auto linux_dir = tribe_linux_dir();
    const auto elf = linux_dir / "vmlinux";
    const auto dtb = linux_dir / "config32.initramfs.dtb";
    const auto initramfs = linux_dir / "initramfs.cpio";
    for (const auto& path : {elf, dtb, initramfs}) {
        if (!std::filesystem::exists(path)) {
            std::print("missing Linux perf input: {}\n", path.string());
            return false;
        }
    }

    TestTribe test(debug);
    const char* old_quiet = std::getenv("TRIBE_TEST_QUIET");
    const std::string saved_quiet = old_quiet ? old_quiet : "";
    setenv("TRIBE_TEST_QUIET", "1", 1);
    const auto start = std::chrono::high_resolution_clock::now();
    const bool run_ok = test.run(elf.string(),
        0, "/dev/null", 5000000, 0, 0x80000000, TRIBE_RAM_BYTES / 4, false,
        0, 0x81f00000, 1, true, 0x80000000u - 0xc0000000u, dtb.string(), false,
        initramfs.string(), 0x81c00000, "", "", 0, false, "", false,
        "", "Linux boot perf slice");
    const auto stop = std::chrono::high_resolution_clock::now();
    if (old_quiet) {
        setenv("TRIBE_TEST_QUIET", saved_quiet.c_str(), 1);
    }
    else {
        unsetenv("TRIBE_TEST_QUIET");
    }
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

    if (!run_ok) {
        return false;
    }

    bool ok = true;
#ifdef VERILATOR
    constexpr uint64_t expected_clocks = 5000000;
    constexpr uint64_t expected_wall_us = 33587140;
    constexpr double expected_stall_pct = 29.44;
    constexpr double expected_issue_pct = 27.30;
    constexpr double expected_total_stall_pct = 56.74;
    constexpr double expected_hazard_pct = 0.01;
    constexpr double expected_dcache_wait_pct = 12.53;
    constexpr double expected_icache_wait_pct = 15.64;
    constexpr double expected_branch_pct = 1.58;
    constexpr double expected_icache_refill_pct = 13.91;
#else
    constexpr uint64_t expected_clocks = 5000000;
    constexpr uint64_t expected_wall_us = 38374253;
    constexpr double expected_stall_pct = 29.44;
    constexpr double expected_issue_pct = 27.30;
    constexpr double expected_total_stall_pct = 56.74;
    constexpr double expected_hazard_pct = 0.01;
    constexpr double expected_dcache_wait_pct = 12.53;
    constexpr double expected_icache_wait_pct = 15.64;
    constexpr double expected_branch_pct = 1.58;
    constexpr double expected_icache_refill_pct = 13.91;
#endif

    // This guards the real early Linux boot path instead of a synthetic ELF:
    // ELF load, DTB/initramfs placement, Sv32 setup, early printk, and the
    // first kernel init sequence all execute with the same cache geometry used
    // by run_linux_probe.sh.
    ok = ok && check_metric_u64("clocks", perf.clocks, expected_clocks, 10.0);
    ok = ok && check_metric_u64_max_ratio("wall_us", wall_us, expected_wall_us, 3.0);
    ok = ok && check_metric("stall_pct", stall_pct, expected_stall_pct, 20.0);
    ok = ok && check_metric("issue_wait_pct", issue_pct, expected_issue_pct, 20.0);
    ok = ok && check_metric("total_stall_pct", total_stall_pct, expected_total_stall_pct, 25.0);
    ok = ok && check_metric("hazard_pct", hazard_pct, expected_hazard_pct, 200.0);
    ok = ok && check_metric("dcache_wait_pct", dcache_wait_pct, expected_dcache_wait_pct, 20.0);
    ok = ok && check_metric("icache_wait_pct", icache_wait_pct, expected_icache_wait_pct, 20.0);
    ok = ok && check_metric("branch_pct", branch_pct, expected_branch_pct, 30.0);
    ok = ok && check_metric("icache_refill_pct", icache_refill_pct, expected_icache_refill_pct, 25.0);
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
    bool ok = run_perf_test(debug);

    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::string verilator_defines =
            "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH) +
            " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES) +
            " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE) +
            " -DTRIBE_CLINT_TICK_DIV_CONFIG=256";
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_defines.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "Perf", source_root);
        ok &= std::system((std::string("Perf/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
    return ok ? 0 : 1;
#endif
}

#endif
