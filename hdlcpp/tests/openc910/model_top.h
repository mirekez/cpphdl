#pragma once
#include "cpphdl.h"
#define private public
#include "all_generated.h"
#undef private

// Observation and stimulus only. The processor, interconnect and SRAM below
// are all ordinary HDLCPP output from the pinned, unmodified RTL checkout.
class OpenC910Model : public cpphdl::Module {
public:
    soc dut;
    _PORT(cpphdl::logic<1>) rst_n_in, jrst_n_in, jclk_in;
#define CPU dut.x_cpu_sub_system_axi.x_rv_integration_platform.x_cpu_top
#define CORE CPU.x_ct_top_0.x_ct_core
    _PORT(cpphdl::logic<3>) retire_out = _ASSIGN(cpphdl::cat{
        CPU.core0_pad_retire2_out(), CPU.core0_pad_retire1_out(), CPU.core0_pad_retire0_out()});
    _PORT(cpphdl::logic<40>) pc0_out = _ASSIGN(CPU.core0_pad_retire0_pc_out());
    _PORT(cpphdl::logic<40>) pc1_out = _ASSIGN(CPU.core0_pad_retire1_pc_out());
    _PORT(cpphdl::logic<40>) pc2_out = _ASSIGN(CPU.core0_pad_retire2_pc_out());
    _PORT(cpphdl::logic<64>) wb0_out = _ASSIGN(CORE.x_ct_iu_top.x_ct_iu_rbus.iu_idu_ex2_pipe0_wb_preg_data_out());
    // Match the upstream monitor's registered value, before the multiplier
    // forwarding mux on iu_idu_ex2_pipe1_wb_preg_data.
    _PORT(cpphdl::logic<64>) wb1_out = _ASSIGN(cpphdl::logic<64>(CORE.x_ct_iu_top.x_ct_iu_rbus.rbus_pipe1_wb_data));
    _PORT(cpphdl::logic<64>) wb2_out = _ASSIGN(CORE.x_ct_lsu_top.x_ct_lsu_ld_wb.lsu_idu_wb_pipe3_wb_preg_data_out());
    _PORT(cpphdl::logic<4>) awlen_out = _ASSIGN(cpphdl::logic<4>(dut.x_axi_slave128.awlen));
    _PORT(cpphdl::logic<32>) address_out = _ASSIGN(cpphdl::logic<32>(dut.x_axi_slave128.mem_addr));
    _PORT(cpphdl::logic<1>) wvalid_out = _ASSIGN(CPU.biu_pad_wvalid_out());
    _PORT(cpphdl::logic<16>) wstrb_out = _ASSIGN(CPU.biu_pad_wstrb_out());
    _PORT(cpphdl::logic<128>) wdata_out = _ASSIGN(CPU.biu_pad_wdata_out());
    _PORT(cpphdl::logic<1>) clock_enable_out = _ASSIGN(CPU.axim_clk_en_in());
#undef CORE
#undef CPU
    void _assign() {
        dut.i_pad_rst_b_in = _ASSIGN(rst_n_in());
        dut.i_pad_jtg_trst_b_in = _ASSIGN(jrst_n_in());
        dut.i_pad_jtg_tclk_in = _ASSIGN(jclk_in());
        dut.i_pad_jtg_tdi_in = _ASSIGN(cpphdl::logic<1>(0));
        dut.i_pad_jtg_tms_in = _ASSIGN(cpphdl::logic<1>(0));
        dut.i_pad_uart0_sin_in = _ASSIGN(cpphdl::logic<1>(1));
        dut._assign();
    }
    void _work(bool reset) { dut._work(reset); }
    void _strobe() { dut._strobe(); }
};
