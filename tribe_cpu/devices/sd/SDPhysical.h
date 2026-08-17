#pragma once

#include "cpphdl.h"

using namespace cpphdl;

class SDPhysical : public Module
{
public:
    _PORT(bool) tx_valid_in;
    _PORT(u<8>) tx_data_in;
    _PORT(bool) tx_last_in;
    _PORT(bool) tx_ready_out = _ASSIGN(!tx_valid_reg);

    _PORT(bool) rx_valid_out = _ASSIGN_REG(rx_valid_reg);
    _PORT(u<8>) rx_data_out = _ASSIGN_REG(rx_data_reg);
    _PORT(bool) rx_last_out = _ASSIGN_REG(rx_last_reg);
    _PORT(bool) rx_ready_in;

    _PORT(bool) sd_cmd_valid_out = _ASSIGN_REG(tx_valid_reg);
    _PORT(u<8>) sd_cmd_data_out = _ASSIGN_REG(tx_data_reg);
    _PORT(bool) sd_cmd_last_out = _ASSIGN_REG(tx_last_reg);
    _PORT(bool) sd_cmd_ready_in;

    _PORT(bool) sd_rsp_valid_in;
    _PORT(u<8>) sd_rsp_data_in;
    _PORT(bool) sd_rsp_last_in;
    _PORT(bool) sd_rsp_ready_out = _ASSIGN(!rx_valid_reg);

private:
    reg<u1> tx_valid_reg;
    reg<u<8>> tx_data_reg;
    reg<u1> tx_last_reg;
    reg<u1> rx_valid_reg;
    reg<u<8>> rx_data_reg;
    reg<u1> rx_last_reg;

public:
    void _work(bool reset)
    {
        if (tx_valid_reg && sd_cmd_ready_in()) {
            tx_valid_reg._next = false;
        }
        if (!tx_valid_reg && tx_valid_in()) {
            tx_valid_reg._next = true;
            tx_data_reg._next = tx_data_in();
            tx_last_reg._next = tx_last_in();
        }

        if (rx_valid_reg && rx_ready_in()) {
            rx_valid_reg._next = false;
        }
        if (!rx_valid_reg && sd_rsp_valid_in()) {
            rx_valid_reg._next = true;
            rx_data_reg._next = sd_rsp_data_in();
            rx_last_reg._next = sd_rsp_last_in();
        }

        if (reset) {
            tx_valid_reg.clr();
            tx_data_reg.clr();
            tx_last_reg.clr();
            rx_valid_reg.clr();
            rx_data_reg.clr();
            rx_last_reg.clr();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        tx_valid_reg.strobe(checkpoint_fd);
        tx_data_reg.strobe(checkpoint_fd);
        tx_last_reg.strobe(checkpoint_fd);
        rx_valid_reg.strobe(checkpoint_fd);
        rx_data_reg.strobe(checkpoint_fd);
        rx_last_reg.strobe(checkpoint_fd);
    }
};

