#include "cpphdl.h"
#include "net/ethgig/ethgig_phy.h"
#include "net/ethgig/ethgig_pcs.h"
#include "net/ethgig/ethgig_mac.h"
#include "RGMIIVerif.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>

#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
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

template<size_t FIFO_DEPTH = 128>
class EthGigMacChain : public Module
{
public:
    _PORT(bool) host_tx_valid_in;
    _PORT(u<8>) host_tx_data_in;
    _PORT(bool) host_tx_last_in;
    _PORT(bool) host_tx_ready_out;

    _PORT(bool) host_rx_valid_out;
    _PORT(u<8>) host_rx_data_out;
    _PORT(bool) host_rx_last_out;
    _PORT(bool) host_rx_ready_in;

    _PORT(logic<48>) local_mac_in;
    _PORT(uint32_t) local_ip_in;
    _PORT(uint32_t) local_mask_in;
    _PORT(bool) promisc_in;

    _PORT(bool) rgmii_tx_ctl_out;
    _PORT(u<4>) rgmii_txd_out;
    _PORT(bool) rgmii_tx_last_out;
    _PORT(bool) rgmii_rx_ctl_in;
    _PORT(u<4>) rgmii_rxd_in;
    _PORT(bool) rgmii_rx_last_in;
    _PORT(bool) mdio_mdc_in;
    _PORT(bool) mdio_host_oe_in;
    _PORT(bool) mdio_host_data_in;
    _PORT(bool) mdio_data_out;
    _PORT(bool) mdio_drive_out;

    _PORT(uint32_t) tx_frames_out;
    _PORT(uint32_t) rx_frames_out;

private:
    EthGigMAC<FIFO_DEPTH> mac;
    EthGigPCS<FIFO_DEPTH> pcs;
    EthGigPHY phy;

public:
    void _assign()
    {
        mac.tx_valid_in = host_tx_valid_in;
        mac.tx_data_in = host_tx_data_in;
        mac.tx_last_in = host_tx_last_in;
        host_tx_ready_out = _ASSIGN(mac.tx_ready_out());
        mac.local_mac_in = local_mac_in;
        mac.local_ip_in = local_ip_in;
        mac.local_mask_in = local_mask_in;
        mac.promisc_in = promisc_in;

        host_rx_valid_out = _ASSIGN(mac.rx_valid_out());
        host_rx_data_out = _ASSIGN(mac.rx_data_out());
        host_rx_last_out = _ASSIGN(mac.rx_last_out());
        mac.rx_ready_in = host_rx_ready_in;

        pcs.tx_valid_in = mac.pcs_tx_valid_out;
        pcs.tx_data_in = mac.pcs_tx_data_out;
        pcs.tx_last_in = mac.pcs_tx_last_out;
        mac.pcs_tx_ready_in = pcs.tx_ready_out;

        phy.tx_valid_in = pcs.tx_valid_out;
        phy.tx_data_in = pcs.tx_data_out;
        phy.tx_last_in = pcs.tx_last_out;
        pcs.tx_ready_in = phy.tx_ready_out;

        pcs.rx_valid_in = phy.rx_valid_out;
        pcs.rx_data_in = phy.rx_data_out;
        pcs.rx_last_in = phy.rx_last_out;
        phy.rx_ready_in = pcs.rx_ready_out;

        mac.pcs_rx_valid_in = pcs.rx_valid_out;
        mac.pcs_rx_data_in = pcs.rx_data_out;
        mac.pcs_rx_last_in = pcs.rx_last_out;
        pcs.rx_ready_in = mac.pcs_rx_ready_out;

        rgmii_tx_ctl_out = _ASSIGN(phy.rgmii_tx_ctl_out());
        rgmii_txd_out = _ASSIGN(phy.rgmii_txd_out());
        rgmii_tx_last_out = _ASSIGN(phy.rgmii_tx_last_out());
        phy.rgmii_rx_ctl_in = rgmii_rx_ctl_in;
        phy.rgmii_rxd_in = rgmii_rxd_in;
        phy.rgmii_rx_last_in = rgmii_rx_last_in;
        phy.mdio_mdc_in = mdio_mdc_in;
        phy.mdio_host_oe_in = mdio_host_oe_in;
        phy.mdio_host_data_in = mdio_host_data_in;
        mdio_data_out = _ASSIGN(phy.mdio_data_out());
        mdio_drive_out = _ASSIGN(phy.mdio_drive_out());

        tx_frames_out = _ASSIGN(mac.tx_frames_out());
        rx_frames_out = _ASSIGN(mac.rx_frames_out());

        mac.__inst_name = "mac";
        pcs.__inst_name = "pcs";
        phy.__inst_name = "phy";
        mac._assign();
        pcs._assign();
        phy._assign();
    }

    void _work(bool reset)
    {
        mac._work(reset);
        pcs._work(reset);
        phy._work(reset);
    }

    void _strobe()
    {
        mac._strobe();
        pcs._strobe();
        phy._strobe();
    }
};

template class EthGigMacChain<128>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

class TestEthGigMac : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    EthGigMacChain<128> dut;
#endif
    RGMIIVerifFrontend verif;
    bool host_tx_valid = false;
    u<8> host_tx_data = 0;
    bool host_tx_last = false;
    bool host_rx_ready = true;
    logic<48> local_mac = 0;
    uint32_t local_ip = 0;
    uint32_t local_mask = 0;
    bool promisc = false;
    bool mdio_mdc = false;
    bool mdio_host_oe = false;
    bool mdio_host_data = true;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.host_tx_valid_in = _ASSIGN_REG(host_tx_valid);
        dut.host_tx_data_in = _ASSIGN_REG(host_tx_data);
        dut.host_tx_last_in = _ASSIGN_REG(host_tx_last);
        dut.host_rx_ready_in = _ASSIGN_REG(host_rx_ready);
        dut.local_mac_in = _ASSIGN_REG(local_mac);
        dut.local_ip_in = _ASSIGN_REG(local_ip);
        dut.local_mask_in = _ASSIGN_REG(local_mask);
        dut.promisc_in = _ASSIGN_REG(promisc);
        verif.rgmii_tx_ctl_in = _ASSIGN(dut.rgmii_tx_ctl_out());
        verif.rgmii_txd_in = _ASSIGN(dut.rgmii_txd_out());
        verif.rgmii_tx_last_in = _ASSIGN(dut.rgmii_tx_last_out());
        dut.rgmii_rx_ctl_in = _ASSIGN(verif.rgmii_rx_ctl_out());
        dut.rgmii_rxd_in = _ASSIGN(verif.rgmii_rxd_out());
        dut.rgmii_rx_last_in = _ASSIGN(verif.rgmii_rx_last_out());
        dut.mdio_mdc_in = _ASSIGN_REG(mdio_mdc);
        dut.mdio_host_oe_in = _ASSIGN_REG(mdio_host_oe);
        dut.mdio_host_data_in = _ASSIGN_REG(mdio_host_data);
        dut.__inst_name = "ethgig_mac_chain";
        verif.__inst_name = "rgmii_verif";
        dut._assign();
        verif._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.reset = reset;
        dut.host_tx_valid_in = host_tx_valid;
        dut.host_tx_data_in = host_tx_data;
        dut.host_tx_last_in = host_tx_last;
        dut.host_rx_ready_in = host_rx_ready;
        dut.local_mac_in = local_mac;
        dut.local_ip_in = local_ip;
        dut.local_mask_in = local_mask;
        dut.promisc_in = promisc;
        dut.mdio_mdc_in = mdio_mdc;
        dut.mdio_host_oe_in = mdio_host_oe;
        dut.mdio_host_data_in = mdio_host_data;
        dut.rgmii_rx_ctl_in = verif.rgmii_rx_ctl_out();
        dut.rgmii_rxd_in = (uint8_t)verif.rgmii_rxd_out();
        dut.rgmii_rx_last_in = verif.rgmii_rx_last_out();
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        dut.clk = 0;
        eval(reset);
        verif.verif.step(dut.rgmii_tx_ctl_out, (uint8_t)dut.rgmii_txd_out, dut.rgmii_tx_last_out);
        dut.rgmii_rx_ctl_in = verif.rgmii_rx_ctl_out();
        dut.rgmii_rxd_in = (uint8_t)verif.rgmii_rxd_out();
        dut.rgmii_rx_last_in = verif.rgmii_rx_last_out();
        dut.clk = 1;
        eval(reset);
        dut.clk = 0;
        eval(reset);
#else
        dut._work(reset);
        verif._work(reset);
        dut._strobe();
        verif._strobe();
#endif
        ++_system_clock;
    }

    bool host_tx_ready()
    {
#ifdef VERILATOR
        return dut.host_tx_ready_out;
#else
        return dut.host_tx_ready_out();
#endif
    }

    bool host_rx_valid()
    {
#ifdef VERILATOR
        return dut.host_rx_valid_out;
#else
        return dut.host_rx_valid_out();
#endif
    }

    uint8_t host_rx_data()
    {
#ifdef VERILATOR
        return (uint8_t)dut.host_rx_data_out;
#else
        return (uint8_t)dut.host_rx_data_out();
#endif
    }

    bool host_rx_last()
    {
#ifdef VERILATOR
        return dut.host_rx_last_out;
#else
        return dut.host_rx_last_out();
#endif
    }

    bool mdio_data()
    {
#ifdef VERILATOR
        return dut.mdio_data_out;
#else
        return dut.mdio_data_out();
#endif
    }

    static uint32_t crc32_next(uint32_t crc, uint8_t data)
    {
        uint32_t value = crc ^ data;
        for (uint32_t i = 0; i < 8; ++i) {
            value = (value & 1u) ? ((value >> 1) ^ 0xedb88320u) : (value >> 1);
        }
        return value;
    }

    static uint32_t fcs_for_payload(const std::vector<uint8_t>& payload)
    {
        uint32_t crc = 0xffffffffu;
        for (uint8_t value : payload) {
            crc = crc32_next(crc, value);
        }
        return ~crc;
    }

    static std::vector<uint8_t> make_wire_frame(const std::vector<uint8_t>& packet)
    {
        std::vector<uint8_t> payload = packet;
        while (payload.size() < 60) {
            payload.push_back(0);
        }
        uint32_t fcs = fcs_for_payload(payload);
        std::vector<uint8_t> wire;
        for (int i = 0; i < 7; ++i) {
            wire.push_back(0x55);
        }
        wire.push_back(0xd5);
        wire.insert(wire.end(), payload.begin(), payload.end());
        for (uint32_t i = 0; i < 4; ++i) {
            wire.push_back((uint8_t)((fcs >> (i * 8u)) & 0xffu));
        }
        return wire;
    }

    static logic<48> mac_value(std::initializer_list<uint8_t> bytes)
    {
        logic<48> value = 0;
        uint32_t i = 0;
        for (uint8_t byte : bytes) {
            value.bits(i * 8 + 7, i * 8) = byte;
            ++i;
        }
        return value;
    }

    static bool vector_prefix_matches(const std::vector<uint8_t>& got, const std::vector<uint8_t>& expected)
    {
        if (got.size() < expected.size()) {
            return false;
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            if (got[i] != expected[i]) {
                return false;
            }
        }
        return true;
    }

    bool send_packet(const std::vector<uint8_t>& packet)
    {
        size_t index = 0;
        for (uint32_t guard = 0; guard < 5000 && index < packet.size(); ++guard) {
            host_tx_valid = host_tx_ready();
            if (host_tx_valid) {
                host_tx_data = packet[index];
                host_tx_last = index + 1 == packet.size();
                ++index;
            }
            cycle(false);
        }
        host_tx_valid = false;
        host_tx_last = false;
        cycle(false);
        return index == packet.size();
    }

    std::vector<uint8_t> receive_packet()
    {
        std::vector<uint8_t> packet;
        host_rx_ready = true;
        for (uint32_t guard = 0; guard < 10000; ++guard) {
            if (host_rx_valid()) {
                packet.push_back(host_rx_data());
                if (host_rx_last()) {
                    cycle(false);
                    return packet;
                }
            }
            cycle(false);
        }
        return packet;
    }

    void mdio_clock()
    {
        mdio_mdc = false;
        cycle(false);
        mdio_mdc = true;
        cycle(false);
    }

    void mdio_send_bit(bool bit)
    {
        mdio_host_oe = true;
        mdio_host_data = bit;
        mdio_clock();
    }

    bool mdio_read_bit()
    {
        bool bit;
        mdio_host_oe = false;
        mdio_host_data = true;
        mdio_mdc = false;
        cycle(false);
        bit = mdio_data();
        mdio_mdc = true;
        cycle(false);
        return bit;
    }

    void mdio_send_bits(uint32_t value, uint32_t bits)
    {
        for (int32_t i = (int32_t)bits - 1; i >= 0; --i) {
            mdio_send_bit(((value >> (uint32_t)i) & 1u) != 0u);
        }
    }

    void mdio_write(uint32_t reg, uint16_t value)
    {
        for (uint32_t i = 0; i < 32; ++i) {
            mdio_send_bit(true);
        }
        mdio_send_bits(0x5u, 4);
        mdio_send_bits(0u, 5);
        mdio_send_bits(reg & 0x1fu, 5);
        mdio_send_bits(0x2u, 2);
        mdio_send_bits(value, 16);
        mdio_host_oe = false;
        cycle(false);
    }

    uint16_t mdio_read(uint32_t reg)
    {
        uint16_t value = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            mdio_send_bit(true);
        }
        mdio_send_bits(0x6u, 4);
        mdio_send_bits(0u, 5);
        mdio_send_bits(reg & 0x1fu, 5);
        mdio_read_bit();
        mdio_read_bit();
        for (uint32_t i = 0; i < 16; ++i) {
            value = (uint16_t)((value << 1) | (mdio_read_bit() ? 1u : 0u));
        }
        mdio_host_oe = false;
        cycle(false);
        return value;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestEthGigMac...");
#else
        std::print("CppHDL TestEthGigMac...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "ethgig_mac_test";
        _assign();
        for (int i = 0; i < 6; ++i) {
            cycle(true);
        }
        local_mac = mac_value({0x02, 0x00, 0x00, 0x00, 0x00, 0x11});
        local_ip = 0xc0a8012au;
        local_mask = 0xffffff00u;

        std::vector<uint8_t> tx = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x11,
            0x08, 0x00, 0xde, 0xad, 0xbe, 0xef};
        if (!send_packet(tx)) {
            std::print("\nERROR: timed out sending host TX packet\n");
            error = true;
        }
        for (int i = 0; i < 3000 && !verif.has_tx_packet(); ++i) {
            cycle(false);
        }
        if (!verif.has_tx_packet()) {
            std::print("\nERROR: RGMII verifier did not receive transmitted packet\n");
            error = true;
        }
        else {
            auto got = verif.pop_tx_packet();
            auto expected = make_wire_frame(tx);
            if (got != expected) {
                std::print("\nERROR: TX wire frame mismatch got={} expected={}\n", got.size(), expected.size());
                error = true;
            }
        }

        std::vector<uint8_t> rx = {
            0x02, 0x00, 0x00, 0x00, 0x00, 0x11,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x22,
            0x08, 0x00,
            0x45, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x00,
            0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0x01,
            0xc0, 0xa8, 0x01, 0x7b, 0x12, 0x34};
        verif.push_rx_packet(make_wire_frame(rx));
        auto got_rx = receive_packet();
        if (got_rx.size() != 60 || !vector_prefix_matches(got_rx, rx)) {
            std::print("\nERROR: RX packet mismatch got={} expected-prefix={}\n", got_rx.size(), rx.size());
            error = true;
        }

        std::vector<uint8_t> rejected = rx;
        rejected[0] = 0x02;
        rejected[5] = 0x99;
        verif.push_rx_packet(make_wire_frame(rejected));
        auto should_drop = receive_packet();
        if (!should_drop.empty()) {
            std::print("\nERROR: RX accepted wrong destination MAC\n");
            error = true;
        }

        rejected = rx;
        rejected[33] = 0x7b;
        rejected[30] = 0x0a;
        verif.push_rx_packet(make_wire_frame(rejected));
        should_drop = receive_packet();
        if (!should_drop.empty()) {
            std::print("\nERROR: RX accepted packet outside configured IPv4 mask\n");
            error = true;
        }

        mdio_write(4, 0x01e1);
        uint16_t mdio_value = mdio_read(4);
        if (mdio_value != 0x01e1) {
            std::print("\nERROR: MDIO readback mismatch got=0x{:04x}\n", mdio_value);
            error = true;
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

static bool generate_sv()
{
    const auto source_root = source_root_dir();
    const auto build_root = source_root / "build";
    std::string cmd;
    cmd += shell_quote(build_root / "cpphdl");
    cmd += " " + shell_quote(std::filesystem::path(__FILE__));
    cmd += " -I " + shell_quote(source_root / "include");
    cmd += " -I " + shell_quote(source_root / "tribe");
    cmd += " -I " + shell_quote(source_root / "tribe" / "common");
    cmd += " -I " + shell_quote(source_root / "tribe" / "devices");
    cmd += " -I " + shell_quote(source_root / "tribe" / "verif");
    if (const char* toolchain_args = std::getenv("CPPHDL_TOOLCHAIN_ARGS")) {
        cmd += " ";
        cmd += toolchain_args;
    }
    return std::system(cmd.c_str()) == 0;
}

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }
#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
#endif
    bool ok = TestEthGigMac().run();
#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        ok &= generate_sv();
        if (ok) {
            ok &= VerilatorCompileInExactFolder(__FILE__, "EthGigMac", "EthGigMacChain",
                {"Predef_pkg", "EthGigPHY", "EthGigPCS", "EthGigMAC"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "devices").string(),
                 (source_root / "tribe" / "verif").string()},
                128);
            ok &= std::system("EthGigMac/obj_dir/VEthGigMacChain") == 0;
        }
    }
#endif
    return ok ? 0 : 1;
}
#endif
