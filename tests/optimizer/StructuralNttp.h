#pragma once

#include "cpphdl.h"

struct StructuralNttpConfig
{
    cpphdl::logic<1> enabled;
    unsigned width;

    constexpr bool operator==(const StructuralNttpConfig&) const = default;
};

inline constexpr StructuralNttpConfig structural_nttp_config{
    cpphdl::logic<1>(1), 3};

template<StructuralNttpConfig Config>
class StructuralNttpLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<Config.width>) input;
    _PORT(cpphdl::logic<Config.width>) output =
        _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<Config.width>& output_comb_func()
    {
        output_comb = Config.enabled
                          ? cpphdl::logic<Config.width>(input())
                          : cpphdl::logic<Config.width>(0);
        return output_comb;
    }

public:
    cpphdl::logic<Config.width> output_comb = 0;
};

template<StructuralNttpConfig Config>
struct StructuralNttpInterface
{
    struct request
    {
        cpphdl::logic<Config.width> value = 0;

        static constexpr size_t _size_bits()
        {
            return Config.width;
        }

        cpphdl::logic<Config.width> pack() const
        {
            return value;
        }

        request& operator=(const cpphdl::logic<Config.width>& packed)
        {
            value = packed;
            return *this;
        }
    };

    struct response
    {
        using field_type = cpphdl::logic<Config.width>;
    };
};

template<typename Request>
class StructuralNttpArrayProjectedLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::array<2, Request, true>) input;
    _PORT(cpphdl::logic<3>) output = _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<3>& output_comb_func()
    {
        output_comb = cpphdl::unpack_value<Request>(
                          cpphdl::pack_value<cpphdl::type_width<Request>()>(
                              input()[0]))
                          .value;
        return output_comb;
    }

public:
    cpphdl::logic<3> output_comb = 0;
};

template<typename Response>
class StructuralNttpProjectedLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<3>) output = _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<3>& output_comb_func()
    {
        output_comb = std::remove_cvref_t<decltype(
            []<typename Value>() {
                return typename Value::field_type{};
            }.template operator()<Response>())>(5);
        return output_comb;
    }

public:
    cpphdl::logic<3> output_comb = 0;
};

class StructuralNttpRoot : public cpphdl::Module
{
public:
    using structural_request_t =
        typename StructuralNttpInterface<structural_nttp_config>::request;

    StructuralNttpLeaf<structural_nttp_config> leaf;
    StructuralNttpProjectedLeaf<
        typename StructuralNttpInterface<structural_nttp_config>::response>
        projected_leaf;
    StructuralNttpArrayProjectedLeaf<
        typename StructuralNttpInterface<structural_nttp_config>::request>
        array_projected_leaf;
    _PORT(cpphdl::logic<3>) input;
    _PORT(cpphdl::logic<3>) output = _ASSIGN_COMB(leaf.output());
    cpphdl::logic<3> work_value = 0;
    cpphdl::logic<3> projected_value = 0;
    cpphdl::logic<3> array_projected_value = 0;
    cpphdl::array<2, structural_request_t, true> array_input_comb;

    cpphdl::array<2, structural_request_t, true>& array_input_comb_func()
    {
        structural_request_t value;
        value.value = input();
        array_input_comb[0] = value;
        array_input_comb[1] = structural_request_t{};
        return array_input_comb;
    }

    void _assign()
    {
        leaf.input = _ASSIGN_COMB(input());
        array_projected_leaf.input = _ASSIGN_COMB(array_input_comb_func());
    }

    void _work(bool)
    {
        work_value = output();
        projected_value = projected_leaf.output();
        array_projected_value = array_projected_leaf.output();
    }
};
