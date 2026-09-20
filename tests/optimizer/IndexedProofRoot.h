#pragma once
#include "cpphdl.h"

using cpphdl::logic;
using cpphdl::reg;

template<unsigned Width>
class IndexedRegister : public cpphdl::Module {
public:
    reg<logic<Width>> value;
    logic<Width> result_comb;
    logic<Width> observed;
    static constexpr bool __cpphdl_complete_result_comb = [] {
        cpphdl::comb_write_proof<Width> proof;
        for (unsigned bit = 0; bit < Width; ++bit) proof.write(bit);
        return proof.complete();
    }();
    logic<Width>& result_comb_func() {
        for (unsigned bit = 0; bit < Width; ++bit) {
            result_comb[bit] = value.get(bit);
        }
        return result_comb;
    }
    void _work(bool) {
        value._next = 7;
        observed = result_comb_func();
    }
    void _strobe() { value.strobe(); }
};

class IndexedProofRoot : public cpphdl::Module {
public:
    IndexedRegister<8> child;
    cpphdl::array<2, cpphdl::reg<cpphdl::logic<8>>> bank;
    cpphdl::logic<8> changing = 0;
    cpphdl::logic<8> stable_comb;
    cpphdl::logic<8> changing_comb;
    cpphdl::logic<8> before, after, stableBefore, stableAfter;
    static constexpr bool __cpphdl_complete_stable_comb = [] {
        cpphdl::comb_write_proof<8> proof;
        for (unsigned bit = 0; bit < 8; ++bit) proof.write(bit);
        return proof.complete();
    }();
    static constexpr bool __cpphdl_complete_changing_comb = __cpphdl_complete_stable_comb;

    cpphdl::logic<8>& stable_comb_func() {
        for (unsigned bit = 0; bit < 8; ++bit) {
            stable_comb[bit] = bank[0].get(bit);
        }
        return stable_comb;
    }
    cpphdl::logic<8>& changing_comb_func() {
        for (unsigned bit = 0; bit < 8; ++bit) {
            changing_comb[bit] = changing.get(bit);
        }
        return changing_comb;
    }
    void _work(bool) {
        child._work(false);
        stableBefore = stable_comb_func();
        bank[0]._next = 0xa5;
        stableAfter = stable_comb_func();
        before = changing_comb_func();
        changing = uint64_t(changing) ^ 0xff;
        after = changing_comb_func();
    }
    void _strobe() {
        bank[0].strobe();
        child._strobe();
    }
};
