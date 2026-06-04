#pragma once

#include "cpphdl.h"

using namespace cpphdl;

class EthGigPHY : public Module
{
public:
    _PORT(bool) tx_valid_in;
    _PORT(u<8>) tx_data_in;
    _PORT(bool) tx_last_in;
    _PORT(bool) tx_ready_out = _ASSIGN(!tx_busy_reg);

    _PORT(bool) rx_valid_out = _ASSIGN_REG(rx_valid_reg);
    _PORT(u<8>) rx_data_out = _ASSIGN_REG(rx_data_reg);
    _PORT(bool) rx_last_out = _ASSIGN_REG(rx_last_reg);
    _PORT(bool) rx_ready_in;

    _PORT(bool) rgmii_tx_ctl_out = _ASSIGN_REG(rgmii_tx_ctl_reg);
    _PORT(u<4>) rgmii_txd_out = _ASSIGN_REG(rgmii_txd_reg);
    _PORT(bool) rgmii_tx_last_out = _ASSIGN_REG(rgmii_tx_last_reg);

    _PORT(bool) rgmii_rx_ctl_in;
    _PORT(u<4>) rgmii_rxd_in;
    _PORT(bool) rgmii_rx_last_in;

private:
    reg<u1> tx_busy_reg;
    reg<u<8>> tx_data_reg;
    reg<u1> tx_last_reg;
    reg<u1> tx_high_reg;
    reg<u1> rgmii_tx_ctl_reg;
    reg<u<4>> rgmii_txd_reg;
    reg<u1> rgmii_tx_last_reg;

    reg<u1> rx_have_low_reg;
    reg<u<4>> rx_low_reg;
    reg<u1> rx_low_last_reg;
    reg<u1> rx_valid_reg;
    reg<u<8>> rx_data_reg;
    reg<u1> rx_last_reg;

public:
    void _assign() {}

    void _work(bool reset)
    {
        if (rx_valid_reg && rx_ready_in()) {
            rx_valid_reg._next = false;
        }

        rgmii_tx_ctl_reg._next = false;
        rgmii_txd_reg._next = 0;
        rgmii_tx_last_reg._next = false;

        if (!tx_busy_reg && tx_valid_in() && tx_ready_out()) {
            tx_busy_reg._next = true;
            tx_data_reg._next = tx_data_in();
            tx_last_reg._next = tx_last_in();
            tx_high_reg._next = false;
        }

        if (tx_busy_reg) {
            rgmii_tx_ctl_reg._next = true;
            if (!tx_high_reg) {
                rgmii_txd_reg._next = (uint8_t)((uint32_t)tx_data_reg & 0x0fu);
                rgmii_tx_last_reg._next = false;
                tx_high_reg._next = true;
            }
            else {
                rgmii_txd_reg._next = (uint8_t)(((uint32_t)tx_data_reg >> 4) & 0x0fu);
                rgmii_tx_last_reg._next = tx_last_reg;
                tx_busy_reg._next = false;
                tx_high_reg._next = false;
            }
        }

        if (rgmii_rx_ctl_in()) {
            if (!rx_have_low_reg) {
                rx_low_reg._next = rgmii_rxd_in();
                rx_low_last_reg._next = rgmii_rx_last_in();
                rx_have_low_reg._next = true;
            }
            else if (!rx_valid_reg || rx_ready_in()) {
                rx_data_reg._next = (uint8_t)(((uint32_t)rgmii_rxd_in() << 4) | (uint32_t)rx_low_reg);
                rx_last_reg._next = rgmii_rx_last_in() || rx_low_last_reg;
                rx_valid_reg._next = true;
                rx_have_low_reg._next = false;
            }
        }

        if (reset) {
            tx_busy_reg._next = false;
            tx_high_reg._next = false;
            rgmii_tx_ctl_reg._next = false;
            rgmii_txd_reg._next = 0;
            rgmii_tx_last_reg._next = false;
            rx_have_low_reg._next = false;
            rx_low_reg._next = 0;
            rx_low_last_reg._next = false;
            rx_valid_reg._next = false;
            rx_data_reg._next = 0;
            rx_last_reg._next = false;
        }
    }

    void _strobe()
    {
        tx_busy_reg.strobe();
        tx_data_reg.strobe();
        tx_last_reg.strobe();
        tx_high_reg.strobe();
        rgmii_tx_ctl_reg.strobe();
        rgmii_txd_reg.strobe();
        rgmii_tx_last_reg.strobe();
        rx_have_low_reg.strobe();
        rx_low_reg.strobe();
        rx_low_last_reg.strobe();
        rx_valid_reg.strobe();
        rx_data_reg.strobe();
        rx_last_reg.strobe();
    }
};
