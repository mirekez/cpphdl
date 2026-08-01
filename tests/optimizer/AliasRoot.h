#pragma once

#include "cpphdl.h"

template<unsigned Offset>
class AliasRootModel : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(output_comb_func());
    cpphdl::reg<cpphdl::logic<8>> state{};

    cpphdl::logic<8>& output_comb_func()
    {
        output_comb = state + cpphdl::logic<8>(Offset);
        return output_comb;
    }

    void _assign() {}
    void _work(bool reset)
    {
        state._next = reset ? cpphdl::logic<8>(0) : input();
    }
    void _strobe() { state.strobe(); }

private:
    cpphdl::logic<8> output_comb = 0;
};

using AliasRoot = AliasRootModel<3>;
