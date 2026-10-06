#include "../../Clocked.h"
#include "IntegerMath.h"
#include "WeightReadStream.h"

#ifndef LLM_TILE_DEPTH
#define LLM_TILE_DEPTH 8
#endif

// All products are ordered. Separate issue/receive/output counters preserve row
// identity without assuming the latency of either retimed arithmetic pipeline.
class StreamingMatVec : public cpphdl::Module {
public:
    WeightReadStream reader;
    cpphdl::hls::ClockedPipeline<integer_llm::Product,4,uint64_t,__uint128_t> product;
    cpphdl::hls::ClockedPipeline<integer_llm::WideAdd,4,__uint128_t,__uint128_t> reduce;
    cpphdl::hls::DramReadIf<512> weights_out;
    _PORT(bool) load_in;
    _PORT(uint32_t) load_address_in;
    _PORT(uint64_t) load_data_in;
    _PORT(bool) command_valid_in;
    _PORT(bool) command_ready_out;
    _PORT(uint32_t) rows_in;
    _PORT(uint32_t) base_in;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(uint32_t) row_out;
    _PORT(bool) fault_out;
    _PORT(bool) loading_out;
    _PORT(bool) computing_out;
    _PORT(bool) product_accepted_out;
private:
    static constexpr unsigned DEPTH = LLM_TILE_DEPTH;
    static constexpr unsigned COUNT_BITS = cpphdl::clog2(DEPTH+1);
    static_assert(DEPTH >= 1 && DEPTH <= 256);
    cpphdl::memory<cpphdl::logic<64>,1,DEPTH> activations;
    cpphdl::reg<cpphdl::u1> active, fault, operand_valid, sum_valid;
    cpphdl::reg<cpphdl::u8> row_count, output_row;
    cpphdl::reg<cpphdl::u<COUNT_BITS>> issue_column, receive_column;
    cpphdl::reg<cpphdl::u64> operand_weight, operand_activation;
    cpphdl::reg<cpphdl::logic<128>> sum, carry, final_sum, final_carry;
    __uint128_t wide(cpphdl::logic<128> v) {
        return (__uint128_t(uint64_t(v >> 64)) << 64) | uint64_t(v);
    }
public:
    void _assign() {
        assignIf(*this,reader,weights_out,reader.memory_out);
        reader.command_in = _ASSIGN(command_valid_in() && command_ready_out() && rows_in() > 0 && rows_in() <= 255);
        reader.base_in = _ASSIGN(base_in());
        reader.words_in = _ASSIGN(rows_in()*DEPTH);
        reader.ready_in = _ASSIGN(active && !fault_out() && (!operand_valid || product.command_ready_out()));
        product.command_valid_in = _ASSIGN(operand_valid && !fault_out());
        product.operation_in = _ASSIGN(uint64_t(operand_weight));
        product.index_in = _ASSIGN(uint64_t(operand_activation));
        product.value_in = _ASSIGN(uint64_t(0));
        product.response_ready_in = _ASSIGN(!fault_out() && (!sum_valid || reduce.command_ready_out()));
        product._assign();
        reduce.command_valid_in = _ASSIGN(sum_valid && !fault_out());
        reduce.operation_in = _ASSIGN(wide(final_sum));
        reduce.index_in = _ASSIGN(wide(final_carry));
        reduce.value_in = _ASSIGN(__uint128_t(0));
        reduce.response_ready_in = _ASSIGN(ready_in());
        reduce._assign();
        command_ready_out = _ASSIGN(!active && !fault_out() && !load_in());
        valid_out = _ASSIGN(reduce.response_valid_out());
        data_out = _ASSIGN(uint64_t(reduce.result_out() >> 48));
        row_out = _ASSIGN(uint32_t(output_row));
        fault_out = _ASSIGN(bool(fault) || reader.fault_out());
        loading_out = _ASSIGN(reader.pending_out());
        computing_out = _ASSIGN(operand_valid || product.response_valid_out() || sum_valid || reduce.response_valid_out());
        product_accepted_out = _ASSIGN(product.command_valid_in() && product.command_ready_out());
    }
    void _work(bool reset) {
        cpphdl::logic<128> term, next_sum, next_carry;
        __uint128_t raw;
        reader._work(reset); product._work(reset); reduce._work(reset);
        if (reset) {
            active.clr(); fault.clr(); operand_valid.clr(); sum_valid.clr(); row_count.clr(); output_row.clr();
            issue_column.clr(); receive_column.clr(); operand_weight.clr(); operand_activation.clr();
            sum.clr(); carry.clr(); final_sum.clr(); final_carry.clr();
        } else {
            if (load_in() && !active) {
                if (load_address_in() < DEPTH) activations[load_address_in()] = load_data_in();
                else fault._next = true;
            }
            if (command_valid_in() && command_ready_out()) {
                if (!rows_in() || rows_in() > 255) fault._next = true;
                else { active._next = true; row_count._next = rows_in(); output_row._next = 0; }
            }
            if (operand_valid && product.command_ready_out()) operand_valid._next = false;
            if (reader.valid_out() && reader.ready_in()) {
                operand_weight._next = reader.data_out();
                operand_activation._next = uint64_t(activations[uint32_t(issue_column)]);
                operand_valid._next = true;
                issue_column._next = issue_column == DEPTH-1 ? 0u : uint32_t(issue_column)+1u;
            }
            if (sum_valid && reduce.command_ready_out()) sum_valid._next = false;
            if (product.response_valid_out() && product.response_ready_in()) {
                raw = product.result_out(); term = uint64_t(raw >> 64);
                term = (term << 64) | uint64_t(raw);
                next_sum = sum ^ carry ^ term;
                next_carry = ((sum & carry) | (sum & term) | (carry & term)) << 1;
                if (receive_column == DEPTH-1) {
                    final_sum._next = next_sum; final_carry._next = next_carry; sum_valid._next = true;
                    sum._next = 0; carry._next = 0; receive_column._next = 0;
                } else {
                    sum._next = next_sum; carry._next = next_carry;
                    receive_column._next = uint32_t(receive_column)+1u;
                }
            }
            if (valid_out() && ready_in()) {
                output_row._next = uint32_t(output_row)+1u;
                if (uint32_t(output_row)+1u == row_count) active._next = false;
            }
        }
    }
    void _strobe() {
        reader._strobe(); product._strobe(); reduce._strobe(); activations.apply();
        active.strobe(); fault.strobe(); operand_valid.strobe(); sum_valid.strobe(); row_count.strobe(); output_row.strobe();
        issue_column.strobe(); receive_column.strobe(); operand_weight.strobe(); operand_activation.strobe();
        sum.strobe(); carry.strobe(); final_sum.strobe(); final_carry.strobe();
    }
};

#ifndef SYNTHESIS
#include "StreamingMatVecTest.h"
#endif
