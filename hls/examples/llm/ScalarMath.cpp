#include "../../Clocked.h"
#include "IntegerMath.h"

class ScalarMath : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<integer_llm::ScalarMath, 4, uint64_t, uint64_t> math;
    _PORT(bool) valid_in;
    _PORT(uint64_t) lhs_in;
    _PORT(uint64_t) rhs_in;
    _PORT(uint64_t) operation_in;
    _PORT(bool) ready_out;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(uint64_t) result_out;
    void _assign() {
        math.command_valid_in = _ASSIGN(valid_in());
        math.operation_in = _ASSIGN(lhs_in());
        math.index_in = _ASSIGN(rhs_in());
        math.value_in = _ASSIGN(operation_in());
        math.response_ready_in = _ASSIGN(ready_in());
        math._assign();
        ready_out = _ASSIGN(math.command_ready_out());
        valid_out = _ASSIGN(math.response_valid_out());
        result_out = _ASSIGN(math.result_out());
    }
    void _work(bool reset) { math._work(reset); }
    void _strobe() { math._strobe(); }
};

#ifndef SYNTHESIS
#include "ScalarMathTest.h"
#endif
