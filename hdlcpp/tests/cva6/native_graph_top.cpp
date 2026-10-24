#include "cpphdl.h"
#define private public
#include "all_generated.h"
#undef private

template<typename Dut = ariane_testharness<>>
class Cva6NativeGraph : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) rst_ni_in, rtc_in;
    _PORT(cpphdl::logic<32>) cycle_limit_in, tohost_in;
    _PORT(cpphdl::logic<32>) exit_out = _ASSIGN(dut.exit_o_out());
    _PORT(cpphdl::logic<256>) commit0_out = _ASSIGN(commit(0));
    _PORT(cpphdl::logic<256>) commit1_out = _ASSIGN(commit(1));
    Dut dut;

    cpphdl::logic<256> commit(unsigned lane) {
        if (lane >= std::remove_reference_t<decltype(dut.i_cva6_rvfi.rvfi_instr_o)>::COUNT_VALUE) return 0;
        const auto instruction = std::as_const(dut.i_cva6_rvfi.rvfi_instr_o)[lane];
        return cpphdl::cat{cpphdl::logic<8>(instruction.valid), cpphdl::logic<8>(instruction.rd_addr),
            cpphdl::logic<8>(instruction.trap), cpphdl::logic<8>(0), cpphdl::logic<32>(instruction.cause),
            cpphdl::logic<32>(instruction.rd_wdata), cpphdl::logic<32>(instruction.insn),
            cpphdl::logic<32>(instruction.pc_wdata), cpphdl::logic<32>(instruction.pc_rdata),
            cpphdl::logic<64>(instruction.order)};
    }

    void _assign() {
        dut.jtag_enable = 0;
        dut.debug_enable = 1;
        dut.rst_ni_in = _ASSIGN(rst_ni_in());
        dut.rtc_i_in = _ASSIGN(rtc_in());
        dut._assign();
    }
    void _work(bool reset) {
        dut.i_rvfi_tracer.SIM_FINISH = static_cast<uint32_t>(uint64_t(cycle_limit_in()));
        dut.i_rvfi_tracer.TOHOST_ADDR = static_cast<uint32_t>(uint64_t(tohost_in()));
        dut._work(reset);
    }
    void _strobe() { dut._strobe(); }
};

extern Cva6NativeGraph<> elaboration_root;
