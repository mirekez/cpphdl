#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

CpphdlModelContext* cpphdlModel = nullptr;

void cpphdl_model_create()
{
    cpphdlModel = new CpphdlModelContext;
    cpphdlModel->dut = new cpphdl_opt_t0;
}
