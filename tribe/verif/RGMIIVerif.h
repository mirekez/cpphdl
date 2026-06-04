#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "cpphdl.h"

using namespace cpphdl;

class RGMIIVerif
{
    std::deque<std::vector<uint8_t>> rx_packets;
    std::vector<uint8_t> tx_packet;
    std::vector<std::vector<uint8_t>> tx_packets;
    bool rx_have_low = false;
    uint8_t rx_low = 0;
    bool rx_low_last = false;
    bool tx_high = false;
    uint8_t tx_byte = 0;

public:
    bool rgmii_rx_ctl = false;
    uint8_t rgmii_rxd = 0;
    bool rgmii_rx_last = false;
    uint32_t tx_packet_count = 0;
    uint32_t rx_packet_count = 0;

    void push_rx_packet(const std::vector<uint8_t>& packet)
    {
        rx_packets.push_back(packet);
        ++rx_packet_count;
    }

    bool has_tx_packet() const
    {
        return !tx_packets.empty();
    }

    std::vector<uint8_t> pop_tx_packet()
    {
        std::vector<uint8_t> packet = tx_packets.front();
        tx_packets.erase(tx_packets.begin());
        return packet;
    }

    const std::vector<std::vector<uint8_t>>& transmitted_packets() const
    {
        return tx_packets;
    }

    void step(bool rgmii_tx_ctl, uint8_t rgmii_txd, bool rgmii_tx_last)
    {
        rgmii_rx_ctl = false;
        rgmii_rxd = 0;
        rgmii_rx_last = false;

        if (!rx_packets.empty()) {
            std::vector<uint8_t>& packet = rx_packets.front();
            if (!packet.empty()) {
                if (!tx_high) {
                    tx_byte = packet.front();
                    rgmii_rxd = tx_byte & 0x0f;
                    rgmii_rx_ctl = true;
                    rgmii_rx_last = false;
                    tx_high = true;
                }
                else {
                    rgmii_rxd = (tx_byte >> 4) & 0x0f;
                    rgmii_rx_ctl = true;
                    rgmii_rx_last = packet.size() == 1;
                    packet.erase(packet.begin());
                    tx_high = false;
                    if (packet.empty()) {
                        rx_packets.pop_front();
                    }
                }
            }
        }

        if (rgmii_tx_ctl) {
            if (!rx_have_low) {
                rx_low = rgmii_txd & 0x0f;
                rx_low_last = rgmii_tx_last;
                rx_have_low = true;
            }
            else {
                uint8_t byte = (uint8_t)(((rgmii_txd & 0x0f) << 4) | rx_low);
                bool last = rgmii_tx_last || rx_low_last;
                tx_packet.push_back(byte);
                rx_have_low = false;
                rx_low_last = false;
                if (last) {
                    tx_packets.push_back(tx_packet);
                    tx_packet.clear();
                    ++tx_packet_count;
                }
            }
        }
    }
};

class RGMIIVerifFrontend : public Module
{
public:
    _PORT(bool) rgmii_tx_ctl_in;
    _PORT(u<4>) rgmii_txd_in;
    _PORT(bool) rgmii_tx_last_in;

    _PORT(bool) rgmii_rx_ctl_out = _ASSIGN(verif.rgmii_rx_ctl);
    _PORT(u<4>) rgmii_rxd_out = _ASSIGN((u<4>)verif.rgmii_rxd);
    _PORT(bool) rgmii_rx_last_out = _ASSIGN(verif.rgmii_rx_last);

    RGMIIVerif verif;

    void push_rx_packet(const std::vector<uint8_t>& packet)
    {
        verif.push_rx_packet(packet);
    }

    bool has_tx_packet() const
    {
        return verif.has_tx_packet();
    }

    std::vector<uint8_t> pop_tx_packet()
    {
        return verif.pop_tx_packet();
    }

    void _assign() {}

    void _work(bool reset)
    {
        if (!reset) {
            verif.step(rgmii_tx_ctl_in(), (uint8_t)rgmii_txd_in(), rgmii_tx_last_in());
        }
    }

    void _strobe() {}
};
