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

static bool build_ethgig_elf(bool polling)
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
    if (polling) cmd += " -DETHGIG_POLLING_TEST=1";
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

static bool run_ethgig_cpu(bool debug, bool polling)
{
    const auto expected = std::filesystem::current_path() / "ethgig_test.expected";
    if (!write_file(expected, "ETHGIG\nDONE\n")) {
        return false;
    }
    setenv("TRIBE_ETH_LOOPBACK", "1", 1);
    return TestTribe(debug).run((std::filesystem::current_path() / "ethgig_test.elf").string(),
        0, expected.string(), polling ? 4000000 : 400000, 0, 0, DEFAULT_RAM_SIZE, false,
        0, 0, 3, false, 0, "", false, "", 0, "", "", 0, false, "", false, "", "EthGig CPU loopback");
}

static bool check_eth_dma_cache_invalidate_policy()
{
    bool ok = true;
    bool pending = TestTribe::eth_dma_cache_invalidate_next(true, false, false);
    if (!pending) ok = false;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        pending = TestTribe::eth_dma_cache_invalidate_next(false, pending, false);
        if (!pending) ok = false;
    }
    pending = TestTribe::eth_dma_cache_invalidate_next(false, pending, true);
    if (pending) ok = false;
    // Held IRQs generate no new completion; let L1 finish initialization.
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        pending = TestTribe::eth_dma_cache_invalidate_next(false, pending, true);
        if (pending) ok = false;
    }
    // A second descriptor completes while the first request is delivered.
    pending = TestTribe::eth_dma_cache_invalidate_next(true, true, true);
    if (!pending) ok = false;
    if (!ok) std::print("EthGig cache invalidate ERROR: completion lost or repeated\n");

    return ok;
}

static bool check_rx_backlog_preserves_inflight_frame()
{
    bool ok = true;
    RGMIIVerif verif;
    verif.push_rx_packet({0x11, 0x12});
    bool ignored = false;
    verif.push_rx_packet_priority_limited({0x21}, 4, false, ignored);
    verif.push_rx_packet({0x31});
    verif.push_rx_packet({0x41});

    verif.step(false, 0, false);
    if (!verif.rgmii_rx_ctl || verif.rgmii_rxd != 0x1 || verif.rgmii_rx_last) {
        std::print("EthGig RX backlog ERROR: bad first low nibble\n");
        ok = false;
    }
    verif.step(false, 0, false);
    if (!verif.rgmii_rx_ctl || verif.rgmii_rxd != 0x1 || verif.rgmii_rx_last) {
        std::print("EthGig RX backlog ERROR: bad first high nibble\n");
        ok = false;
    }

    bool evicted = false;
    if (!verif.push_rx_packet_priority_limited({0x51}, 4, true, evicted) || !evicted) {
        std::print("EthGig RX backlog ERROR: full queue did not retain newest frame\n");
        ok = false;
    }
    if (verif.pending_rx_packets() != 4) {
        std::print("EthGig RX backlog ERROR: bounded queue size changed\n");
        ok = false;
    }

    verif.step(false, 0, false);
    if (!verif.rgmii_rx_ctl || verif.rgmii_rxd != 0x2 || verif.rgmii_rx_last) {
        std::print("EthGig RX backlog ERROR: in-flight frame was truncated\n");
        ok = false;
    }
    verif.step(false, 0, false);
    if (!verif.rgmii_rx_ctl || verif.rgmii_rxd != 0x1 || !verif.rgmii_rx_last) {
        std::print("EthGig RX backlog ERROR: in-flight frame lost its end marker\n");
        ok = false;
    }
    verif.step(false, 0, false);
    if (!verif.rgmii_rx_ctl || verif.rgmii_rxd != 0x1) {
        std::print("EthGig RX backlog ERROR: evicted wrong pending frame\n");
        ok = false;
    }
    return ok;
}

static bool check_rx_backlog_preserves_payload()
{
    bool ok = true;
    bool evicted = false;
    RGMIIVerif verif;
    if (!verif.push_rx_packet_priority_limited({0xa1}, 4, true, evicted) ||
        !verif.push_rx_packet_priority_limited({0xb1}, 4, true, evicted) ||
        !verif.push_rx_packet_priority_limited({0xc1}, 4, true, evicted) ||
        !verif.push_rx_packet_priority_limited({0xd1}, 4, true, evicted)) {
        std::print("EthGig RX priority ERROR: initial queue failed\n");
        return false;
    }
    // A recoverable ACK cannot displace queued payload.
    if (verif.push_rx_packet_priority_limited({0xe1}, 4, false, evicted) ||
        evicted) {
        std::print("EthGig RX priority ERROR: ACK displaced payload\n");
        ok = false;
    }
    if (verif.push_rx_packet_priority_limited({0xf1}, 4, true, evicted) || evicted) {
        std::print("EthGig RX priority ERROR: later payload displaced earlier payload\n");
        ok = false;
    }
    RGMIIVerif mixed;
    if (!mixed.push_rx_packet_priority_limited({0xa1}, 4, false, evicted) ||
        !mixed.push_rx_packet_priority_limited({0xb1}, 4, true, evicted) ||
        !mixed.push_rx_packet_priority_limited({0xc1}, 4, true, evicted) ||
        !mixed.push_rx_packet_priority_limited({0xd1}, 4, true, evicted)) {
        std::print("EthGig RX priority ERROR: mixed queue failed\n");
        return false;
    }
    // New payload first evicts the queued low-priority ACK.
    if (!mixed.push_rx_packet_priority_limited({0xf1}, 4, true, evicted) ||
        !evicted) {
        std::print("EthGig RX priority ERROR: payload was not retained\n");
        ok = false;
    }
    mixed.step(false, 0, false);
    if (!mixed.rgmii_rx_ctl || mixed.rgmii_rxd != 0x1) {
        std::print("EthGig RX priority ERROR: wrong packet was evicted\n");
        ok = false;
    }
    return ok;
}

static bool check_tx_packet_requires_commit()
{
    RGMIIVerif verif;
    verif.step(true, 0x0a, false);
    verif.step(true, 0x0b, true);
    if (!verif.has_tx_packet() || verif.front_tx_packet().size() != 1 ||
        verif.front_tx_packet()[0] != 0xba) {
        std::print("EthGig TX queue ERROR: packet unavailable before commit\n");
        return false;
    }
    if (!verif.has_tx_packet()) {
        std::print("EthGig TX queue ERROR: inspection removed packet\n");
        return false;
    }
    verif.pop_tx_packet();
    if (verif.has_tx_packet()) {
        std::print("EthGig TX queue ERROR: commit did not remove packet\n");
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    bool polling = false;
    bool policy_only = false;
    bool noveril = false;
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--polling") == 0) polling = true;
        if (strcmp(argv[i], "--policy-only") == 0) policy_only = true;
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }

    bool ok = check_eth_dma_cache_invalidate_policy();
    ok = check_rx_backlog_preserves_inflight_frame() && ok;
    ok = check_rx_backlog_preserves_payload() && ok;
    ok = check_tx_packet_requires_commit() && ok;
    if (policy_only) return ok ? 0 : 1;
    ok = ok && build_ethgig_elf(polling);
    ok = ok && run_ethgig_cpu(debug, polling);

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
