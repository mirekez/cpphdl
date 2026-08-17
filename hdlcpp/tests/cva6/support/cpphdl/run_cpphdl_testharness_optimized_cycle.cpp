#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"
#include "cpphdl_opt_t0_optimized_combs.h"

void cpphdl_model_set_reset(bool value)
{
    cpphdlModel->rstN = logic<1>(value);
}

void cpphdl_model_set_rtc(bool value)
{
    cpphdlModel->rtc = logic<1>(value);
}

void cpphdl_model_strobe()
{
    commit_optimized_regs(*cpphdlModel->dut);
}

void cpphdl_model_work(bool reset)
{
    calc_all(*cpphdlModel->dut, reset);
}
