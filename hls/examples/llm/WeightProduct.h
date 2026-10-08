#pragma once
#include "../../Clocked.h"
#include "../../ExternalMemory.h"
#include "IntegerMath.h"

#ifndef LLM_DDR_BITS
#define LLM_DDR_BITS 512
#endif
#ifndef LLM_PIPELINE_STAGES
#define LLM_PIPELINE_STAGES 4
#endif

// One weight/activation product per admitted command. A DDR beat is cached so
// adjacent row elements reuse the burst. The pipeline itself has II=1.
class WeightProduct : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<integer_llm::Product, LLM_PIPELINE_STAGES,
                               uint64_t, __uint128_t> product;
    cpphdl::hls::DramReadIf<LLM_DDR_BITS> weights_out;
    _PORT(bool) valid_in;
    _PORT(uint32_t) address_in;
    _PORT(uint64_t) activation_in;
    _PORT(bool) ready_out;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(cpphdl::logic<128>) product_out;
    _PORT(bool) fault_out;
private:
    static constexpr unsigned BEAT_BYTES = LLM_DDR_BITS / 8;
    cpphdl::reg<cpphdl::u1> cache_valid, waiting, fault;
    cpphdl::reg<cpphdl::u32> cache_address, requested_address;
    cpphdl::reg<cpphdl::logic<LLM_DDR_BITS>> cache_data;

    bool hit() {
        return (bool)cache_valid && (uint32_t)cache_address == (address_in() & ~(BEAT_BYTES - 1u));
    }
    uint64_t weight() {
        return (uint64_t)(cache_data >> ((address_in() & (BEAT_BYTES - 1u)) * 8u));
    }
    cpphdl::logic<128> result() {
        cpphdl::logic<128> bits;
        __uint128_t value;
        value = product.result_out();
        bits = (uint64_t)(value >> 64);
        bits = (bits << 64) | (uint64_t)value;
        return bits;
    }
public:
    void _assign() {
        weights_out.valid_in = _ASSIGN(valid_in() && !hit() && !waiting && !fault && (address_in() & 7u) == 0);
        weights_out.addr_in = _ASSIGN(address_in() & ~(BEAT_BYTES - 1u));
        weights_out.ready_in = _ASSIGN((bool)waiting);
        product.command_valid_in = _ASSIGN(valid_in() && hit() && !fault && (address_in() & 7u) == 0);
        product.operation_in = _ASSIGN(activation_in());
        product.index_in = _ASSIGN(weight());
        product.value_in = _ASSIGN(uint64_t(0));
        product.response_ready_in = _ASSIGN(ready_in());
        product._assign();
        ready_out = _ASSIGN(hit() && !fault && (address_in() & 7u) == 0 && product.command_ready_out());
        valid_out = _ASSIGN(product.response_valid_out());
        product_out = _ASSIGN(result());
        fault_out = _ASSIGN((bool)fault || product.fault_out() != 0);
    }
    void _work(bool reset) {
        product._work(reset);
        if (reset) {
            cache_valid.clr(); waiting.clr(); fault.clr();
            cache_address.clr(); requested_address.clr(); cache_data.clr();
        } else {
            if (weights_out.valid_in() && weights_out.ready_out()) {
                requested_address._next = address_in() & ~(BEAT_BYTES - 1u);
                waiting._next = true;
            }
            if (waiting && weights_out.valid_out()) {
                cache_data._next = weights_out.data_out();
                cache_address._next = requested_address;
                cache_valid._next = !weights_out.error_out();
                fault._next = weights_out.error_out();
                waiting._next = false;
            }
            if (valid_in() && (address_in() & 7u) != 0) fault._next = true;
        }
    }
    void _strobe() {
        product._strobe();
        cache_valid.strobe(); waiting.strobe(); fault.strobe();
        cache_address.strobe(); requested_address.strobe(); cache_data.strobe();
    }
};
