#pragma once
#include <cpphdl.h>

using namespace cpphdl;

#ifndef REPLACEMENT_WIDTH
#define REPLACEMENT_WIDTH 8
#endif

#ifdef REPLACEMENT_DEFAULT
template<int LANES = 8>
#else
template<int LANES>
#endif
class [[clang::annotate(
#if defined(REPLACEMENT_FILE)
    "CPPHDL_REPLACEMENT_FILE=replacement_$(LANES).sv"
#elif defined(REPLACEMENT_SCRIPT)
    "CPPHDL_REPLACEMENT_SCRIPT=./replacement_$(LANES).sh"
#else
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "module Aaaaaa #(parameter int LANES = $(LANES))\n"
    "(input wire clk, input wire reset,\n"
    " input wire [LANES-1:0] value_in, output wire [LANES-1:0] value_out);\n"
    "assign value_out = value_in;\n"
    "endmodule\n"
#endif
)]] Aaaaaa : public Module {
public:
    _PORT(logic<LANES>) value_in;
    _PORT(logic<LANES>) value_out = _ASSIGN(value_in());
};

class Bbbbbb : public Module {
#ifdef INSTANTIATE_AAAAAA
    Aaaaaa<REPLACEMENT_WIDTH> aaaaaa;
#endif
public:
    _PORT(logic<REPLACEMENT_WIDTH>) value_in;
#ifdef INSTANTIATE_AAAAAA
    _PORT(logic<REPLACEMENT_WIDTH>) value_out = _ASSIGN(aaaaaa.value_out());
    void _assign() { aaaaaa.value_in = value_in; }
#else
    _PORT(logic<REPLACEMENT_WIDTH>) value_out = _ASSIGN(value_in());
#endif
};
