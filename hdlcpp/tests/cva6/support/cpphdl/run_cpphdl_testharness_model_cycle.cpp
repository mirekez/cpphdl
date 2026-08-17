#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

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
    cpphdlModel->dut->_strobe();
}

void cpphdl_model_work(bool reset)
{
    cpphdlModel->dut->_work(reset);
}
