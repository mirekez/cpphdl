#include "cpphdl_optimized_externs.h"
#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"
#include "cpphdl_opt_t0_optimized_combs.h"

void cpphdl_model_assign()
{
    cpphdlModel->dut->rst_ni_in = _ASSIGN_REG(cpphdlModel->rstN);
    cpphdlModel->dut->rtc_i_in = _ASSIGN_REG(cpphdlModel->rtc);
    cpphdl_optimized_root_assign(*cpphdlModel->dut);
    bind_optimized_ports(*cpphdlModel->dut);
}
