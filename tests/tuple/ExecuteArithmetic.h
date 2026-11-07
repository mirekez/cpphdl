#pragma once
#include "../../examples/tuple/Pipeline.h"
#include "../../examples/tuple/Instr.h"
#include "../../examples/tuple/ExecuteCalc.h"

struct ExecuteInput
{
    uint32_t rs1_val;
    uint32_t rs2_val;
    uint32_t imm;
    uint32_t pc;
    uint8_t rs2;
    uint8_t alu_op;
    uint8_t mem_op;
    uint8_t br_op;
    uint8_t funct3;
    bool valid;
};

class ExecuteArithmetic : public Module
{
    ExecuteCalc<ExecuteCalc<int, int, 0, 0>::State, ExecuteInput, 1, 2> execute;
public:
    _PORT(uint32_t) a_in;
    _PORT(uint32_t) b_in;
    _PORT(uint8_t) op_in;
    _PORT(bool) valid_in;
    _PORT(uint32_t) result_out = _ASSIGN(execute.alu_result_out());

private:
    array<2, ExecuteInput> input_comb;
    array<2, ExecuteInput>& input_comb_func()
    {
        size_t i;
        for (i = 0; i < 2; ++i) {
            input_comb[i] = {};
        }
        input_comb[0].rs1_val = a_in();
        input_comb[0].rs2_val = b_in();
        input_comb[0].rs2 = 1;
        input_comb[0].alu_op = op_in();
        input_comb[0].valid = valid_in();
        return input_comb;
    }

public:
    void _assign()
    {
        execute.state_in = _ASSIGN_COMB(input_comb_func());
        execute._assign();
    }
    void _work(bool reset) { execute._work(reset); }
    void _strobe() { execute._strobe(); }
};
