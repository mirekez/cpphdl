#pragma once

#include "cpphdl.h"

struct ProjectedCombValue {
    cpphdl::logic<8> op = 0;
    cpphdl::logic<8> result = 0;
};

using ProjectedCombArray = cpphdl::array<2, ProjectedCombValue>;
using ProjectedCombByteArray = cpphdl::array<2, cpphdl::logic<8>>;

class ProjectedCombLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(ProjectedCombValue) decoded_out = _ASSIGN_COMB(decoded_comb_func());
    _PORT(cpphdl::logic<8>) decoded_out__field_op = _ASSIGN_COMB(decoded_op_comb_func());
    _PORT(cpphdl::logic<8>) decoded_out__field_result = _ASSIGN_COMB(decoded_result_comb_func());
    _PORT(ProjectedCombArray) decoded_array_out = _ASSIGN_COMB(decoded_array_comb_func());
    _PORT(ProjectedCombByteArray) decoded_array_out__field_op = _ASSIGN_COMB(decoded_array_op_comb_func());

    ProjectedCombValue& decoded_comb_func()
    {
        decoded_comb = {};
        if ((uint64_t)input() & 1) {
            decoded_comb.op = view_comb_func() + cpphdl::logic<8>(3);
            decoded_comb.result = view_comb_func() ^ cpphdl::logic<8>(0x5a);
        } else {
            decoded_comb.op = view_comb_func() - cpphdl::logic<8>(2);
            decoded_comb.result = view_comb_func() | cpphdl::logic<8>(0x80);
        }
        return decoded_comb;
    }

    cpphdl::logic<8>& view_comb_func()
    {
        view_comb = 0;
        view_comb = input();
        return view_comb;
    }

    cpphdl::logic<8>& single_comb_func()
    {
        single_comb = 0;
        single_comb = input() + cpphdl::logic<8>(1);
        return single_comb;
    }

    cpphdl::logic<8>& decoded_op_comb_func()
    {
        decoded_op_comb = 0;
        if ((uint64_t)input() & 1)
            decoded_op_comb = input() + cpphdl::logic<8>(3);
        else
            decoded_op_comb = input() - cpphdl::logic<8>(2);
        return decoded_op_comb;
    }

    cpphdl::logic<8>& decoded_result_comb_func()
    {
        decoded_result_comb = 0;
        if ((uint64_t)input() & 1)
            decoded_result_comb = input() ^ cpphdl::logic<8>(0x5a);
        else
            decoded_result_comb = input() | cpphdl::logic<8>(0x80);
        return decoded_result_comb;
    }

    ProjectedCombArray& decoded_array_comb_func()
    {
        for (std::size_t index = 0; index < 2; ++index)
            decoded_array_comb[index].op = input() + cpphdl::logic<8>(index);
        return decoded_array_comb;
    }

    ProjectedCombByteArray& decoded_array_op_comb_func()
    {
        for (std::size_t index = 0; index < 2; ++index)
            decoded_array_op_comb[index] = input() + cpphdl::logic<8>(index);
        return decoded_array_op_comb;
    }

    void _assign() {}
    void _work(bool) {}
    void _strobe() {}

private:
    ProjectedCombValue decoded_comb;
    cpphdl::logic<8> view_comb = 0;
    cpphdl::logic<8> single_comb = 0;
    cpphdl::logic<8> decoded_op_comb = 0;
    cpphdl::logic<8> decoded_result_comb = 0;
    ProjectedCombArray decoded_array_comb{};
    ProjectedCombByteArray decoded_array_op_comb{};
};

class ProjectedCombRoot : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) op = _ASSIGN_COMB(leaf.decoded_out__field_op());
    _PORT(cpphdl::logic<8>) result = _ASSIGN_COMB(leaf.decoded_out__field_result());
    _PORT(cpphdl::logic<8>) single = _ASSIGN_COMB(leaf.single_comb_func());
    _PORT(ProjectedCombByteArray) array_op = _ASSIGN_COMB(leaf.decoded_array_out__field_op());
    ProjectedCombLeaf leaf;

    void _assign()
    {
        leaf.input = _ASSIGN_COMB(input());
        leaf._assign();
    }
    void _work(bool reset) { leaf._work(reset); }
    void _strobe() { leaf._strobe(); }
};
