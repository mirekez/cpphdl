#pragma once

#include "cpphdl.h"

template<unsigned Offset>
class AliasLeafModel : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<8>& output_comb_func()
    {
        output_comb = input() + cpphdl::logic<8>(Offset);
        return output_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}

private:
    cpphdl::logic<8> output_comb = 0;
};

using AliasLeaf = AliasLeafModel<1>;

class AliasCollisionChild : public cpphdl::Module
{
public:
    cpphdl::logic<8> collision = 0;
};

class AliasForward : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(input());
};

template<unsigned Offset>
class AliasRootModel : public cpphdl::Module
{
public:
    static constexpr unsigned ChildBias = Offset + 2;
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(output_comb_func());
    AliasLeaf child;
    AliasCollisionChild collision_child;
    AliasForward forwarding;
    AliasForward comma_forwarding;
    cpphdl::reg<cpphdl::logic<8>> state{};
    cpphdl::logic<8> child_value = 0;
    cpphdl::logic<8> repeated_value = 0;
    cpphdl::logic<8> repeated_second_value = 0;
    cpphdl::logic<8> work_seed = 0;
    unsigned repeated_evaluations = 0;
    cpphdl::logic<8> collision = 4;
    cpphdl::logic<8> collision_value = 0;
    cpphdl::logic<8> forwarded_value = 0;
    cpphdl::logic<8> comma_forwarded_value = 0;
    unsigned output_evaluations = 0;

    cpphdl::logic<8>& output_comb_func()
    {
        ++output_evaluations;
        output_comb = state + cpphdl::logic<8>(Offset);
        return output_comb;
    }

    cpphdl::logic<8>& repeated_comb_func()
    {
        if constexpr (ChildBias > 0) {
            ++repeated_evaluations;
            if (forwarding.output() == cpphdl::logic<8>(255)) {
                repeated_comb = repeated_second_comb_func();
            }
            repeated_comb = work_seed + cpphdl::logic<8>(ChildBias);
        }
        return repeated_comb;
    }

    cpphdl::logic<8>& repeated_second_comb_func()
    {
        if constexpr (ChildBias > 1) {
            ++repeated_evaluations;
            if (forwarding.output() == cpphdl::logic<8>(255)) {
                repeated_second_comb = repeated_comb_func();
            }
            repeated_second_comb = work_seed + cpphdl::logic<8>(ChildBias + 1);
        }
        return repeated_second_comb;
    }

    cpphdl::logic<8>& collision_comb_func()
    {
        std::remove_cvref_t<decltype(forwarding.output())> typed_collision = collision;
        collision_comb = typed_collision;
        return collision_comb;
    }

    void _assign()
    {
        child.input = _ASSIGN_COMB(input());
        forwarding.input = _ASSIGN_COMB(input());
        // A comma binding still returns its final operand by reference.
        // Optimized pointer caching must pass the whole expression as one
        // addressof argument instead of parsing its operands as arguments.
        comma_forwarding.input = _ASSIGN_COMB((input(), input()));
    }
    void _work(bool reset)
    {
        state._next = reset ? cpphdl::logic<8>(0) : input();
        if constexpr (ChildBias > 0) {
            child_value = child.output() + cpphdl::logic<8>(ChildBias);
        }
        work_seed = input();
        collision_child.collision = input();
        collision_value = collision_comb_func();
        forwarded_value = forwarding.output();
        comma_forwarded_value = comma_forwarding.output();
        repeated_value = repeated_comb_func();
        repeated_value = repeated_comb_func();
        repeated_second_value = repeated_second_comb_func();
        repeated_second_value = repeated_second_comb_func();
    }
    void _strobe() { state.strobe(); }

private:
    cpphdl::logic<8> output_comb = 0;
    cpphdl::logic<8> repeated_comb = 0;
    cpphdl::logic<8> repeated_second_comb = 0;
    cpphdl::logic<8> collision_comb = 0;
};

using AliasRoot = AliasRootModel<3>;
