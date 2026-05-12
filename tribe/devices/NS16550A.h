#pragma once

#include "cpphdl.h"
#include "Axi4.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256>
class NS16550A : public Module
{
public:
    static constexpr uint32_t REG_RBR_THR_DLL = 0x00;
    static constexpr uint32_t REG_IER_DLM = 0x01;
    static constexpr uint32_t REG_IIR_FCR = 0x02;
    static constexpr uint32_t REG_LCR = 0x03;
    static constexpr uint32_t REG_MCR = 0x04;
    static constexpr uint32_t REG_LSR = 0x05;
    static constexpr uint32_t REG_MSR = 0x06;
    static constexpr uint32_t REG_SCR = 0x07;

    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;

    _PORT(bool) uart_valid_out = _BIND_VAR(uart_valid_reg);
    _PORT(uint8_t) uart_data_out = _BIND_VAR(uart_data_reg);

private:
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;

    reg<u8> ier_reg;
    reg<u8> lcr_reg;
    reg<u8> mcr_reg;
    reg<u8> scr_reg;
    reg<u8> dll_reg;
    reg<u8> dlm_reg;
    reg<u1> uart_valid_reg;
    reg<u8> uart_data_reg;

    _LAZY_COMB(dlab_comb, bool)
        return dlab_comb = (lcr_reg & u<8>(0x80)) != 0;
    }

    // NS16550A read map. TX is modeled as always empty/ready: LSR.THRE and LSR.TEMT are set.
    _LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        uint32_t addr;
        uint32_t lane;
        uint8_t data;
        read_data_comb = 0;
        addr = (uint32_t)read_addr_reg & 7u;
        lane = (uint32_t)read_addr_reg % (DATA_WIDTH / 8);
        data = 0;
        if (addr == REG_RBR_THR_DLL) {
            data = dlab_comb_func() ? (uint8_t)dll_reg : 0;
        }
        if (addr == REG_IER_DLM) {
            data = dlab_comb_func() ? (uint8_t)dlm_reg : (uint8_t)ier_reg;
        }
        if (addr == REG_IIR_FCR) {
            data = 0x01;
        }
        if (addr == REG_LCR) {
            data = (uint8_t)lcr_reg;
        }
        if (addr == REG_MCR) {
            data = (uint8_t)mcr_reg;
        }
        if (addr == REG_LSR) {
            data = 0x60;
        }
        if (addr == REG_MSR) {
            data = 0;
        }
        if (addr == REG_SCR) {
            data = (uint8_t)scr_reg;
        }
        read_data_comb.bits(lane * 8 + 7, lane * 8) = data;
        return read_data_comb;
    }

public:
    void _assign()
    {
        axi_in.awready_out = _BIND(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = _BIND(write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.bvalid_out = _BIND_VAR(write_resp_valid_reg);
        axi_in.bid_out = _BIND_VAR(write_id_reg);
        axi_in.arready_out = _BIND(!read_valid_reg);
        axi_in.rvalid_out = _BIND_VAR(read_valid_reg);
        axi_in.rdata_out = _BIND_VAR(read_data_comb_func());
        axi_in.rlast_out = _BIND_VAR(read_valid_reg);
        axi_in.rid_out = _BIND_VAR(read_id_reg);
    }

    void _work(bool reset)
    {
        uint32_t addr;
        uint8_t data;
        uart_valid_reg._next = false;

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            read_addr_reg._next = axi_in.araddr_in();
            read_id_reg._next = axi_in.arid_in();
            read_valid_reg._next = true;
        }
        if (read_valid_reg && axi_in.rready_in()) {
            read_valid_reg._next = false;
        }

        if (axi_in.awvalid_in() && axi_in.awready_out()) {
            write_addr_reg._next = axi_in.awaddr_in();
            write_id_reg._next = axi_in.awid_in();
            write_addr_valid_reg._next = true;
        }
        if (axi_in.wvalid_in() && axi_in.wready_out()) {
            addr = (uint32_t)write_addr_reg & 7u;
            data = (uint8_t)(uint32_t)axi_in.wdata_in().bits(7, 0);
            write_addr_valid_reg._next = false;
            write_resp_valid_reg._next = true;

            if (addr == REG_RBR_THR_DLL) {
                if (dlab_comb_func()) {
                    dll_reg._next = data;
                }
                else {
                    uart_data_reg._next = data;
                    uart_valid_reg._next = true;
                }
            }
            if (addr == REG_IER_DLM) {
                if (dlab_comb_func()) {
                    dlm_reg._next = data;
                }
                else {
                    ier_reg._next = data;
                }
            }
            if (addr == REG_LCR) {
                lcr_reg._next = data;
            }
            if (addr == REG_MCR) {
                mcr_reg._next = data;
            }
            if (addr == REG_SCR) {
                scr_reg._next = data;
            }
        }
        if (write_resp_valid_reg && axi_in.bready_in()) {
            write_resp_valid_reg._next = false;
        }

        if (reset) {
            read_addr_reg.clr();
            read_id_reg.clr();
            read_valid_reg.clr();
            write_addr_reg.clr();
            write_id_reg.clr();
            write_addr_valid_reg.clr();
            write_resp_valid_reg.clr();
            ier_reg.clr();
            lcr_reg.clr();
            mcr_reg.clr();
            scr_reg.clr();
            dll_reg.clr();
            dlm_reg.clr();
            uart_valid_reg.clr();
            uart_data_reg.clr();
        }
    }

    void _strobe()
    {
        read_addr_reg.strobe();
        read_id_reg.strobe();
        read_valid_reg.strobe();
        write_addr_reg.strobe();
        write_id_reg.strobe();
        write_addr_valid_reg.strobe();
        write_resp_valid_reg.strobe();
        ier_reg.strobe();
        lcr_reg.strobe();
        mcr_reg.strobe();
        scr_reg.strobe();
        dll_reg.strobe();
        dlm_reg.strobe();
        uart_valid_reg.strobe();
        uart_data_reg.strobe();
    }
};
