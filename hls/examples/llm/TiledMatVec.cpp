#include "../../Clocked.h"
#include "IntegerMath.h"

#ifndef LLM_TILE_DEPTH
#define LLM_TILE_DEPTH 8
#endif

// No clock or bus operations in the loader algorithm. Its pointer load is the
// suspension point; the wrapper holds its result until the buffer accepts it.
struct WeightLoader {
    uint64_t command(uint32_t base, uint32_t index, uint32_t unused) {
        auto weights = cpphdl::hls::external_memory<const uint64_t>(base);
        return weights[index];
    }
};

// W[rows,TILE_DEPTH] * X[TILE_DEPTH]. Two weight tiles alternate ownership:
// filling -> full -> compute reading -> free. Loading and compute overlap.
class TiledMatVec : public cpphdl::Module {
public:
    cpphdl::hls::ClockedMemory<WeightLoader> loader;
    cpphdl::hls::ClockedPipeline<integer_llm::Product,4,uint64_t,__uint128_t> product;
    cpphdl::hls::ClockedPipeline<integer_llm::WideAdd,4,__uint128_t,__uint128_t> reduce;
    cpphdl::hls::ExternalMemoryIf<> weights_out;
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
private:
    static_assert(LLM_TILE_DEPTH > 0 && LLM_TILE_DEPTH <= 256);
    static constexpr unsigned DEPTH = LLM_TILE_DEPTH;
    static constexpr unsigned COUNT_BITS = cpphdl::clog2(DEPTH + 1);
    cpphdl::memory<cpphdl::logic<64>,1,DEPTH> activations;
    cpphdl::memory<cpphdl::logic<64>,1,2*DEPTH> weights;
    cpphdl::reg<cpphdl::u1> active, full0, full1, load_pending, operand_valid, reducing, fault;
    cpphdl::reg<cpphdl::u<2>> compute_state;
    cpphdl::reg<cpphdl::u<COUNT_BITS>> fill_index, issued, received;
    cpphdl::reg<cpphdl::u<8>> row_count, fill_row, compute_row;
    cpphdl::reg<cpphdl::u32> base_address;
    cpphdl::reg<cpphdl::u64> operand_weight, operand_activation;
    cpphdl::reg<cpphdl::logic<128>> sum, carry;

    bool fill_full() { return (uint32_t(fill_row) & 1u) ? bool(full1) : bool(full0); }
    bool compute_full() { return (uint32_t(compute_row) & 1u) ? bool(full1) : bool(full0); }
    __uint128_t wide(cpphdl::logic<128> value) {
        return (__uint128_t(uint64_t(value >> 64)) << 64) | uint64_t(value);
    }
public:
    void _assign() {
        loader.command_valid_in = _ASSIGN(active && !fault && fill_row < row_count && !fill_full() && !load_pending);
        loader.operation_in = _ASSIGN(uint32_t(base_address));
        loader.index_in = _ASSIGN(uint32_t(fill_row)*DEPTH + uint32_t(fill_index));
        loader.value_in = _ASSIGN(0u);
        loader.response_ready_in = _ASSIGN(active && load_pending && !fault);
        assignIf(*this, loader, weights_out, loader.memory_out);
        product.command_valid_in = _ASSIGN(bool(operand_valid) && !fault);
        product.operation_in = _ASSIGN(uint64_t(operand_weight));
        product.index_in = _ASSIGN(uint64_t(operand_activation));
        product.value_in = _ASSIGN(uint64_t(0));
        product.response_ready_in = _ASSIGN(compute_state == 1 && !fault);
        product._assign();
        reduce.command_valid_in = _ASSIGN(compute_state == 2 && !reducing && !fault);
        reduce.operation_in = _ASSIGN(wide(sum));
        reduce.index_in = _ASSIGN(wide(carry));
        reduce.value_in = _ASSIGN(__uint128_t(0));
        reduce.response_ready_in = _ASSIGN(ready_in() && !fault);
        reduce._assign();
        command_ready_out = _ASSIGN(!active && !fault && !load_in());
        valid_out = _ASSIGN(reduce.response_valid_out() && !fault);
        data_out = _ASSIGN(uint64_t(reduce.result_out() >> 48));
        row_out = _ASSIGN(uint32_t(compute_row));
        fault_out = _ASSIGN(bool(fault));
        loading_out = _ASSIGN(active && load_pending);
        computing_out = _ASSIGN(compute_state != 0);
    }
    void _work(bool reset) {
        cpphdl::logic<128> term;
        __uint128_t raw_term;
        uint32_t address;
        loader._work(reset); product._work(reset); reduce._work(reset);
        if (reset) {
            active.clr(); full0.clr(); full1.clr(); load_pending.clr(); operand_valid.clr(); reducing.clr(); fault.clr();
            compute_state.clr(); fill_index.clr(); issued.clr(); received.clr(); row_count.clr(); fill_row.clr(); compute_row.clr();
            base_address.clr(); operand_weight.clr(); operand_activation.clr(); sum.clr(); carry.clr();
        } else {
            if (load_in() && !active) {
                if (load_address_in() < DEPTH) activations[load_address_in()] = load_data_in();
                else fault._next = true;
            }
            if (command_valid_in() && command_ready_out()) {
                if (!rows_in() || rows_in() > 255 || (base_in() & 7u) ||
                    base_in() > UINT32_MAX - 255u*DEPTH*8u) fault._next = true;
                else {
                    active._next = true; row_count._next = rows_in(); base_address._next = base_in();
                    fill_row._next = 0; compute_row._next = 0; fill_index._next = 0;
                }
            }
            if (loader.command_valid_in() && loader.command_ready_out()) load_pending._next = true;
            if (loader.response_valid_out() && loader.response_ready_in()) {
                if (loader.fault_out()) fault._next = true;
                else {
                    address = (uint32_t(fill_row) & 1u)*DEPTH + uint32_t(fill_index);
                    weights[address] = loader.result_out();
                    if (fill_index == DEPTH-1) {
                        if (uint32_t(fill_row) & 1u) full1._next = true; else full0._next = true;
                        fill_row._next = uint32_t(fill_row)+1u; fill_index._next = 0;
                    } else fill_index._next = uint32_t(fill_index)+1u;
                }
                load_pending._next = false;
            }
            if (active && !fault) {
                if (compute_state == 0 && compute_full()) {
                    compute_state._next = 1; issued._next = 0; received._next = 0;
                    sum._next = 0; carry._next = 0; reducing._next = false;
                }
                if (operand_valid && product.command_ready_out()) operand_valid._next = false;
                if (compute_state == 1 && issued < DEPTH && (!operand_valid || product.command_ready_out())) {
                    address = (uint32_t(compute_row) & 1u)*DEPTH + uint32_t(issued);
                    operand_weight._next = uint64_t(weights[address]);
                    operand_activation._next = uint64_t(activations[uint32_t(issued)]);
                    operand_valid._next = true; issued._next = uint32_t(issued)+1u;
                    // Last word is now held in the operand register, so this
                    // bank may be reused while products/reduction are in flight.
                    if (issued == DEPTH-1) {
                        if (uint32_t(compute_row) & 1u) full1._next = false; else full0._next = false;
                    }
                }
                if (product.response_valid_out() && product.response_ready_in()) {
                    raw_term = product.result_out();
                    term = uint64_t(raw_term >> 64);
                    term = (term << 64) | uint64_t(raw_term);
                    sum._next = sum ^ carry ^ term;
                    carry._next = ((sum & carry) | (sum & term) | (carry & term)) << 1;
                    received._next = uint32_t(received)+1u;
                    if (received == DEPTH-1) compute_state._next = 2;
                }
                if (reduce.command_valid_in() && reduce.command_ready_out()) reducing._next = true;
                if (valid_out() && ready_in()) {
                    compute_row._next = uint32_t(compute_row)+1u; compute_state._next = 0;
                    if (uint32_t(compute_row)+1u == uint32_t(row_count)) active._next = false;
                }
            }
        }
    }
    void _strobe() {
        loader._strobe(); product._strobe(); reduce._strobe(); activations.apply(); weights.apply();
        active.strobe(); full0.strobe(); full1.strobe(); load_pending.strobe(); operand_valid.strobe(); reducing.strobe(); fault.strobe();
        compute_state.strobe(); fill_index.strobe(); issued.strobe(); received.strobe(); row_count.strobe(); fill_row.strobe(); compute_row.strobe();
        base_address.strobe(); operand_weight.strobe(); operand_activation.strobe(); sum.strobe(); carry.strobe();
    }
};

#ifndef SYNTHESIS
#include "TiledMatVecTest.h"
#endif
