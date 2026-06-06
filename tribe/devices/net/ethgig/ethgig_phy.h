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

    _PORT(bool) mdio_mdc_in;
    _PORT(bool) mdio_host_oe_in;
    _PORT(bool) mdio_host_data_in;
    _PORT(bool) mdio_data_out = _ASSIGN(mdio_drive_reg ? (bool)mdio_out_reg : true);
    _PORT(bool) mdio_drive_out = _ASSIGN_REG(mdio_drive_reg);

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

    reg<array<u<16>, 32>> mdio_regs;
    reg<u1> mdio_prev_mdc_reg;
    reg<u<4>> mdio_state_reg;
    reg<u<6>> mdio_preamble_count_reg;
    reg<u<5>> mdio_bit_count_reg;
    reg<u<16>> mdio_shift_reg;
    reg<u<5>> mdio_phy_addr_reg;
    reg<u<5>> mdio_reg_addr_reg;
    reg<u<2>> mdio_opcode_reg;
    reg<u1> mdio_drive_reg;
    reg<u1> mdio_out_reg;
    reg<u<5>> mdio_read_index_reg;
    reg<u<16>> mdio_read_data_reg;

    static constexpr uint32_t MDIO_IDLE = 0;
    static constexpr uint32_t MDIO_HEADER = 1;
    static constexpr uint32_t MDIO_WRITE_TA = 2;
    static constexpr uint32_t MDIO_WRITE_DATA = 3;
    static constexpr uint32_t MDIO_READ_TA0 = 4;
    static constexpr uint32_t MDIO_READ_TA1 = 5;
    static constexpr uint32_t MDIO_READ_DATA = 6;

public:
    void _assign() {}

    void _work(bool reset)
    {
        uint32_t i;

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

        if (mdio_drive_reg && (uint32_t)mdio_state_reg != MDIO_READ_TA0 &&
            (uint32_t)mdio_state_reg != MDIO_READ_TA1 &&
            (uint32_t)mdio_state_reg != MDIO_READ_DATA) {
            mdio_drive_reg._next = false;
            mdio_out_reg._next = true;
        }

        if (!mdio_prev_mdc_reg && mdio_mdc_in()) {
            bool mdio_in;
            uint32_t header;
            uint32_t next_shift;
            uint32_t next_index;

            mdio_in = mdio_host_oe_in() ? mdio_host_data_in() : true;
            if ((uint32_t)mdio_state_reg == MDIO_IDLE) {
                if (mdio_in) {
                    if ((uint32_t)mdio_preamble_count_reg < 32u) {
                        mdio_preamble_count_reg._next = mdio_preamble_count_reg + 1;
                    }
                }
                else if ((uint32_t)mdio_preamble_count_reg >= 16u) {
                    mdio_state_reg._next = MDIO_HEADER;
                    mdio_bit_count_reg._next = 1;
                    mdio_shift_reg._next = 0;
                }
                else {
                    mdio_preamble_count_reg._next = 0;
                }
            }
            else if ((uint32_t)mdio_state_reg == MDIO_HEADER) {
                next_shift = (((uint32_t)mdio_shift_reg << 1) | (mdio_in ? 1u : 0u)) & 0xffffu;
                mdio_shift_reg._next = next_shift;
                if ((uint32_t)mdio_bit_count_reg + 1u >= 14u) {
                    header = next_shift;
                    mdio_opcode_reg._next = (header >> 10) & 0x3u;
                    mdio_phy_addr_reg._next = (header >> 5) & 0x1fu;
                    mdio_reg_addr_reg._next = header & 0x1fu;
                    mdio_bit_count_reg._next = 0;
                    mdio_shift_reg._next = 0;
                    if (((header >> 12) & 0x3u) == 1u && ((header >> 10) & 0x3u) == 1u) {
                        mdio_state_reg._next = MDIO_WRITE_TA;
                    }
                    else if (((header >> 12) & 0x3u) == 1u && ((header >> 10) & 0x3u) == 2u) {
                        mdio_state_reg._next = MDIO_READ_TA0;
                        mdio_read_data_reg._next = mdio_regs[header & 0x1fu];
                        mdio_read_index_reg._next = 15;
                        mdio_drive_reg._next = true;
                        mdio_out_reg._next = false;
                    }
                    else {
                        mdio_state_reg._next = MDIO_IDLE;
                        mdio_preamble_count_reg._next = 0;
                    }
                }
                else {
                    mdio_bit_count_reg._next = mdio_bit_count_reg + 1;
                }
            }
            else if ((uint32_t)mdio_state_reg == MDIO_WRITE_TA) {
                if ((uint32_t)mdio_bit_count_reg + 1u >= 2u) {
                    mdio_state_reg._next = MDIO_WRITE_DATA;
                    mdio_bit_count_reg._next = 0;
                    mdio_shift_reg._next = 0;
                }
                else {
                    mdio_bit_count_reg._next = mdio_bit_count_reg + 1;
                }
            }
            else if ((uint32_t)mdio_state_reg == MDIO_WRITE_DATA) {
                next_shift = (((uint32_t)mdio_shift_reg << 1) | (mdio_in ? 1u : 0u)) & 0xffffu;
                mdio_shift_reg._next = next_shift;
                if ((uint32_t)mdio_bit_count_reg + 1u >= 16u) {
                    mdio_regs._next[(uint32_t)mdio_reg_addr_reg] = next_shift;
                    mdio_state_reg._next = MDIO_IDLE;
                    mdio_bit_count_reg._next = 0;
                    mdio_preamble_count_reg._next = 0;
                }
                else {
                    mdio_bit_count_reg._next = mdio_bit_count_reg + 1;
                }
            }
            else if ((uint32_t)mdio_state_reg == MDIO_READ_TA0) {
                mdio_drive_reg._next = true;
                mdio_out_reg._next = false;
                mdio_state_reg._next = MDIO_READ_TA1;
            }
            else if ((uint32_t)mdio_state_reg == MDIO_READ_TA1) {
                mdio_drive_reg._next = true;
                mdio_out_reg._next = (bool)((uint32_t)mdio_read_data_reg >> 15);
                mdio_state_reg._next = MDIO_READ_DATA;
                mdio_read_index_reg._next = 14;
            }
            else if ((uint32_t)mdio_state_reg == MDIO_READ_DATA) {
                mdio_drive_reg._next = true;
                mdio_out_reg._next = (bool)(((uint32_t)mdio_read_data_reg >> (uint32_t)mdio_read_index_reg) & 1u);
                if ((uint32_t)mdio_read_index_reg == 0u) {
                    mdio_state_reg._next = MDIO_IDLE;
                    mdio_drive_reg._next = false;
                    mdio_preamble_count_reg._next = 0;
                }
                else {
                    next_index = (uint32_t)mdio_read_index_reg - 1u;
                    mdio_read_index_reg._next = next_index;
                }
            }
        }
        mdio_prev_mdc_reg._next = mdio_mdc_in();

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
            for (i = 0; i < 32; ++i) {
                mdio_regs._next[i] = 0;
            }
            mdio_regs._next[0] = 0x1140;
            mdio_regs._next[1] = 0x786d;
            mdio_regs._next[2] = 0x0141;
            mdio_regs._next[3] = 0x0dd0;
            mdio_prev_mdc_reg._next = false;
            mdio_state_reg._next = MDIO_IDLE;
            mdio_preamble_count_reg._next = 0;
            mdio_bit_count_reg._next = 0;
            mdio_shift_reg._next = 0;
            mdio_phy_addr_reg._next = 0;
            mdio_reg_addr_reg._next = 0;
            mdio_opcode_reg._next = 0;
            mdio_drive_reg._next = false;
            mdio_out_reg._next = true;
            mdio_read_index_reg._next = 0;
            mdio_read_data_reg._next = 0;
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
        mdio_regs.strobe();
        mdio_prev_mdc_reg.strobe();
        mdio_state_reg.strobe();
        mdio_preamble_count_reg.strobe();
        mdio_bit_count_reg.strobe();
        mdio_shift_reg.strobe();
        mdio_phy_addr_reg.strobe();
        mdio_reg_addr_reg.strobe();
        mdio_opcode_reg.strobe();
        mdio_drive_reg.strobe();
        mdio_out_reg.strobe();
        mdio_read_index_reg.strobe();
        mdio_read_data_reg.strobe();
    }
};
