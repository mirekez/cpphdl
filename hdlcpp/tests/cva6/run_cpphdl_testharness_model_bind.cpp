#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

void cpphdl_model_assign()
{
    cpphdlModel->dut->rst_ni_in = _ASSIGN_REG(cpphdlModel->rstN);
    cpphdlModel->dut->rtc_i_in = _ASSIGN_REG(cpphdlModel->rtc);
    cpphdl_optimized_root_assign_abi(cpphdlModel->dut);
}
