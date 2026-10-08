#include "WeightProduct.h"

#ifndef LLM_MAX_DEPTH
#define LLM_MAX_DEPTH 8
#endif
#ifndef LLM_MAX_ROWS
#define LLM_MAX_ROWS 8
#endif
#ifndef LLM_MAX_COLUMNS
#define LLM_MAX_COLUMNS 8
#endif

// W[rows,depth] in DDR times X[depth,columns] in the local activation tile.
// Capacities are compile-time choices; runtime geometry may be smaller.
class MatrixMath : public cpphdl::Module {
public:
    WeightProduct multiply;
    cpphdl::hls::ClockedPipeline<integer_llm::WideAdd, 4, __uint128_t, __uint128_t> reduce;
    cpphdl::hls::DramReadIf<LLM_DDR_BITS> weights_out;
    _PORT(bool) load_in;
    _PORT(uint32_t) load_address_in;
    _PORT(uint64_t) load_data_in;
    _PORT(bool) command_valid_in;
    _PORT(bool) command_ready_out;
    _PORT(uint32_t) rows_in;
    _PORT(uint32_t) columns_in;
    _PORT(uint32_t) depth_in;
    _PORT(uint32_t) base_in;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(uint32_t) row_out;
    _PORT(uint32_t) column_out;
    _PORT(bool) fault_out;
private:
    static_assert(LLM_MAX_DEPTH > 0 && LLM_MAX_ROWS > 0 && LLM_MAX_COLUMNS > 0);
    static_assert(uint64_t(LLM_MAX_DEPTH) * LLM_MAX_ROWS * 8 <= UINT32_MAX);
    static_assert(uint64_t(LLM_MAX_DEPTH) * LLM_MAX_COLUMNS < UINT32_MAX);
    static constexpr unsigned ACTIVATION_WORDS = LLM_MAX_DEPTH * LLM_MAX_COLUMNS;
    static constexpr uint32_t MAX_WEIGHT_BYTES = LLM_MAX_DEPTH * LLM_MAX_ROWS * 8u;
    cpphdl::memory<cpphdl::logic<64>, 1, ACTIVATION_WORDS> activations;
    cpphdl::reg<cpphdl::u<2>> state;
    cpphdl::reg<cpphdl::u<cpphdl::clog2(LLM_MAX_ROWS + 1)>> rows, row;
    cpphdl::reg<cpphdl::u<cpphdl::clog2(LLM_MAX_COLUMNS + 1)>> columns, column;
    cpphdl::reg<cpphdl::u<cpphdl::clog2(LLM_MAX_DEPTH + 1)>> depth, sent, received;
    cpphdl::reg<cpphdl::u<cpphdl::clog2(ACTIVATION_WORDS + 1)>> activation_address;
    cpphdl::reg<cpphdl::u32> row_address, weight_address;
    cpphdl::reg<cpphdl::logic<128>> sum, carry;
    cpphdl::reg<cpphdl::u1> reducing, fault;

    __uint128_t wide(cpphdl::logic<128> v) {
        return (__uint128_t(uint64_t(v >> 64)) << 64) | uint64_t(v);
    }
    uint64_t activation() { return uint64_t(activations[uint32_t(activation_address)]); }
public:
    void _assign() {
        assignIf(*this, multiply, weights_out, multiply.weights_out);
        multiply.valid_in = _ASSIGN(state == 1 && sent < depth && !fault);
        multiply.address_in = _ASSIGN(uint32_t(weight_address));
        multiply.activation_in = _ASSIGN(activation());
        multiply.ready_in = _ASSIGN(state == 1);
        reduce.command_valid_in = _ASSIGN(state == 2 && !reducing);
        reduce.operation_in = _ASSIGN(wide(sum));
        reduce.index_in = _ASSIGN(wide(carry));
        reduce.value_in = _ASSIGN(__uint128_t(0));
        reduce.response_ready_in = _ASSIGN(ready_in());
        reduce._assign();
        command_ready_out = _ASSIGN(state == 0 && !fault && !load_in());
        valid_out = _ASSIGN(reduce.response_valid_out());
        data_out = _ASSIGN(uint64_t(reduce.result_out() >> 48));
        row_out = _ASSIGN(uint32_t(row)); column_out = _ASSIGN(uint32_t(column));
        fault_out = _ASSIGN(bool(fault) || multiply.fault_out() || reduce.fault_out() != 0);
    }
    void _work(bool reset) {
        cpphdl::logic<128> product;
        uint32_t next_column;
        multiply._work(reset); reduce._work(reset);
        if (reset) {
            state.clr(); rows.clr(); columns.clr(); depth.clr(); row.clr(); column.clr();
            sent.clr(); received.clr(); activation_address.clr();
            row_address.clr(); weight_address.clr(); sum.clr(); carry.clr(); reducing.clr(); fault.clr();
        } else {
            if (load_in() && state == 0) {
                if (load_address_in() < ACTIVATION_WORDS) activations[load_address_in()] = load_data_in();
                else fault._next = true;
            }
            if (command_valid_in() && command_ready_out()) {
                if (rows_in() == 0 || rows_in() > LLM_MAX_ROWS || columns_in() == 0 || columns_in() > LLM_MAX_COLUMNS ||
                    depth_in() == 0 || depth_in() > LLM_MAX_DEPTH || (base_in() & 7u) != 0 ||
                    base_in() > UINT32_MAX - MAX_WEIGHT_BYTES + 1u) {
                    fault._next = true;
                } else {
                    rows._next = rows_in(); columns._next = columns_in(); depth._next = depth_in();
                    row._next = 0; column._next = 0; sent._next = 0; received._next = 0;
                    activation_address._next = 0;
                    row_address._next = base_in(); weight_address._next = base_in();
                    sum._next = 0; carry._next = 0; reducing._next = false; state._next = 1;
                }
            }
            if (state == 1) {
                if (multiply.valid_in() && multiply.ready_out()) {
                    sent._next = uint32_t(sent) + 1u;
                    weight_address._next = uint32_t(weight_address) + 8u;
                    activation_address._next = uint32_t(activation_address) + uint32_t(columns);
                }
                if (multiply.valid_out()) {
                    product = multiply.product_out();
                    // One full-width product per clock without carry propagation
                    // in the feedback path. Reduce/round only after the last term.
                    sum._next = sum ^ carry ^ product;
                    carry._next = ((sum & carry) | (sum & product) | (carry & product)) << 1;
                    received._next = uint32_t(received) + 1u;
                    if (uint32_t(received) + 1u == uint32_t(depth)) state._next = 2;
                }
            }
            if (state == 2) {
                if (reduce.command_valid_in() && reduce.command_ready_out()) reducing._next = true;
                if (reduce.response_valid_out() && ready_in()) {
                    next_column = uint32_t(column) + 1u;
                    if (next_column == columns) {
                        next_column = 0;
                        row._next = uint32_t(row) + 1u;
                        row_address._next = weight_address;
                    }
                    column._next = next_column;
                    activation_address._next = next_column;
                    weight_address._next = next_column == 0 ? weight_address : row_address;
                    sent._next = 0; received._next = 0; sum._next = 0; carry._next = 0; reducing._next = false;
                    state._next = next_column == 0 && uint32_t(row) + 1u == uint32_t(rows) ? 0 : 1;
                }
            }
        }
    }
    void _strobe() {
        multiply._strobe(); reduce._strobe(); activations.apply();
        state.strobe(); rows.strobe(); columns.strobe(); depth.strobe(); row.strobe(); column.strobe();
        sent.strobe(); received.strobe(); activation_address.strobe();
        row_address.strobe(); weight_address.strobe(); sum.strobe(); carry.strobe(); reducing.strobe(); fault.strobe();
    }
};
#ifndef SYNTHESIS
#include "MatrixMathTest.h"
#endif
