#include <cpphdl.h>

using namespace cpphdl;

#ifdef DEFAULT_EXPRESSIONS
template<int LANES = 2 + 3, int PIPE_DEPTH_SUM = 0x2, int PIPE_DEPTH_MUL = -1>
#else
template<int LANES = 0, int PIPE_DEPTH_SUM = 1, int PIPE_DEPTH_MUL = 1>
#endif
class
#ifdef FLOW_XXX
[[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "module Arithmetic #(parameter int LANES = $(LANES), parameter int PIPE_DEPTH_SUM = $(PIPE_DEPTH_SUM), parameter int PIPE_DEPTH_MUL = $(PIPE_DEPTH_MUL))\n"
    "(input wire clk, input wire reset, output wire [31:0] value_out);\n"
    "// REPLACEMENT_DEFAULTS_$(LANES)_$(PIPE_DEPTH_SUM)_$(PIPE_DEPTH_MUL)\n"
    "// Literal dollar: $$; SV system function: $bits(value_out)\n"
    "assign value_out = LANES + PIPE_DEPTH_SUM + PIPE_DEPTH_MUL;\n"
    "endmodule\n;"
)]]
#endif
Arithmetic : public Module
{
public:
    _PORT(uint32_t) value_out = _ASSIGN((uint32_t)(LANES + PIPE_DEPTH_SUM + PIPE_DEPTH_MUL));
};

#if defined(NAMED_VALUES)
class ArithmeticTop : public Module
{
    static constexpr int SIZE = 12;
    static constexpr int REDUCE_DELAY = 4;
    static constexpr int SCALE_DELAY = 6;
    Arithmetic<SIZE, REDUCE_DELAY, SCALE_DELAY> arithm;

public:
    _PORT(uint32_t) value_out = _ASSIGN(arithm.value_out());
};
#elif defined(PARENT_TEMPLATE)
template<int SIZE, int REDUCE_DELAY, int SCALE_DELAY>
class ArithmeticParent : public Module
{
    Arithmetic<SIZE, REDUCE_DELAY, SCALE_DELAY> arithm;

public:
    _PORT(uint32_t) value_out = _ASSIGN(arithm.value_out());
};

class ArithmeticTop : public Module
{
    ArithmeticParent<12, 4, 6> parent;

public:
    _PORT(uint32_t) value_out = _ASSIGN(parent.value_out());
};
#elif defined(CONCRETE_VALUES)
class ArithmeticTop : public Module
{
public:
    Arithmetic<8, 2, 3> arithmetic;
};
#elif defined(PARTIAL_VALUES)
class ArithmeticTop : public Module
{
public:
    Arithmetic<8> arithmetic;
};
#endif
