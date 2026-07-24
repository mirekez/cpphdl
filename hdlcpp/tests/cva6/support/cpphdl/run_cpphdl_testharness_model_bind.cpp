#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

void cpphdl_model_assign()
{
    cpphdlModel->dut->rst_ni_in = _ASSIGN_REG(cpphdlModel->rstN);
    cpphdlModel->dut->rtc_i_in = _ASSIGN_REG(cpphdlModel->rtc);
    cpphdlModel->dut->_assign();
}
