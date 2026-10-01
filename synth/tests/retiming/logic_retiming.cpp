#include "cpphdl.h"

class
#ifdef RETIMING_ANNOTATE
[[clang::annotate("CPPHDL_RETIMING=fit_pipeline_retiming:2.5")]]
#endif
ArithmeticChain : public cpphdl::Module {
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint8_t) tag_value_in;
    _PORT(uint8_t) result_out = _ASSIGN_REG(result);
    _PORT(uint8_t) partial_out = _ASSIGN_REG(first);
    _PORT(uint8_t) tag_out = _ASSIGN_REG(tag2);
    cpphdl::reg<cpphdl::logic<8>> first, result, tag1, tag2;
    void _work(bool reset) {
        uint8_t x;
        first._next = a_in() + b_in();
        x = uint8_t(first) + 13;
        x = x ^ 0x59;
        x = x + 29;
        x = x ^ (x << 1);
        result._next = x + 31;
        tag1._next = tag_value_in(); tag2._next = tag1;
        if (reset) { first._next = 0; result._next = 0; tag1._next = 0x5a; tag2._next = 0x5a; }
    }
    void _strobe() { first.strobe(); result.strobe(); tag1.strobe(); tag2.strobe(); }
};
class LogicRetiming : public cpphdl::Module {
    ArithmeticChain stage;
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint8_t) tag_value_in;
    _PORT(uint8_t) result_out = _ASSIGN(stage.result_out());
    _PORT(uint8_t) partial_out = _ASSIGN(stage.partial_out());
    _PORT(uint8_t) tag_out = _ASSIGN(stage.tag_out());
    _PORT(uint8_t) mirror_out = _ASSIGN_REG(mirror);
    cpphdl::reg<cpphdl::logic<8>> mirror;
    void _assign() {
        stage.a_in = _ASSIGN(a_in()); stage.b_in = _ASSIGN(b_in()); stage.tag_value_in = _ASSIGN(tag_value_in());
    }
    void _work(bool reset) { stage._work(reset); mirror._next = a_in(); if (reset) mirror._next = 0; }
    void _strobe() { stage._strobe(); mirror.strobe(); }
};
LogicRetiming cpphdl_top;
#ifdef RETIMING_RUN
#include "Test.h"
#endif
