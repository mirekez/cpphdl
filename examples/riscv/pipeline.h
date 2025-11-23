#pragma once

#include "cpphdl.h"

#include <type_traits>
#include <tuple>
#include <utility>

// pipeline stages auto-enumerator - gives ID and LENGTH to all pipeline stages

template <size_t LENGTH, size_t ID, template<size_t,size_t> class T>
struct StageHolder
{
    using type = T<ID,LENGTH>;
};

template <size_t LENGTH, typename IndexSeq, template<size_t,size_t> class... Ts>
struct MakeStagesTupleImpl;

template <size_t LENGTH, size_t... I, template<size_t,size_t> class... Ts>
struct MakeStagesTupleImpl<LENGTH, std::index_sequence<I...>, Ts...>
{
    using type = std::tuple<typename StageHolder<LENGTH, I, Ts>::type...>;
};

template <template<size_t,size_t> class... Ts>
using PipelineStages = MakeStagesTupleImpl<sizeof...(Ts),std::make_index_sequence<sizeof...(Ts)>, Ts...>;

// states merger - builds overal state

template <typename... Ts>
struct MergeState : Ts... {};

template <typename>
struct Pipeline;

template <template<size_t,size_t> class... Ts>
struct Pipeline<PipelineStages<Ts...>>
{
    using STATE = MergeState<typename Ts<0,0>::State...>;
    using STAGES = PipelineStages<Ts...>;

    static constexpr std::size_t COUNT = sizeof...(Ts);
    STATE stages[COUNT];
    STAGES::type members;

public:
    void connect()
    {
        std::apply([](auto&... stage){
            (stage.connect(), ...);
        }, members);
    }

    void work(bool clk, bool reset)
    {
    }


    void strobe()
    {
    }

    void comb()
    {
    }

};

class PipelineStage : public cpphdl::Module
{
    

};
