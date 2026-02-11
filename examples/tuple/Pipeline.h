#pragma once

extern long sys_clock;

#include "cpphdl.h"

#include <type_traits>
#include <tuple>
#include <utility>
//      ::State  ::State  ::State
//     ┌────────┐
//  y  │Stage0-0│
//     ├────────┼────────┐
//     │Stage0-1│Stage1-0│
//     ├────────┼────────┼────────┐
//     │Stage0-2│Stage1-1│Stage2-0│   <-  BIG_STATE
//     └────────┴────────┴────────┘
//                  x

// PipelineStage primitive

template<typename OWN_STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
struct PipelineStage : public cpphdl::Module
{
    using STATE = OWN_STATE;

    __PORT(cpphdl::array<BIG_STATE,LENGTH>)  state_in;
    __PORT(cpphdl::array<STATE,LENGTH-ID>)   state_out   = __VAR( state_reg );

    cpphdl::reg<cpphdl::array<STATE,LENGTH-ID>> state_reg;

    void _work(bool reset)
    {
        size_t i;

        for (i=1; i < LENGTH-ID; ++i) {
            state_reg.next[i] = state_reg[i-1];
        }
    }

    void _strobe()
    {
        state_reg.strobe();
    }
};

template <typename T>
struct DebugType;

#define __PARAMS__ typename,typename,size_t,size_t

template <typename... Ts>
struct MakeBigState : Ts... {};

/////////////////////////////////////////////// Pipeline Stages auto-enumerator - gives IDs and LENGTH to all pipeline stages

template <typename OWN_STATE, typename BIG_STATE, size_t LENGTH, size_t ID, template<__PARAMS__> class STAGE>
struct StageHolder
{
    using type = STAGE<OWN_STATE,BIG_STATE,ID,LENGTH>;
};

template <typename BIG_STATE, size_t LENGTH, typename IndexSeq, template<__PARAMS__> class... Ts>
struct MakeStagesTupleImpl;

template <typename BIG_STATE, size_t LENGTH, size_t... I, template<__PARAMS__> class... Ts>
struct MakeStagesTupleImpl<BIG_STATE,LENGTH,std::index_sequence<I...>,Ts...>
{
    using type = std::tuple<typename StageHolder<typename Ts<int,int,0,0>::State,BIG_STATE,LENGTH,I,Ts>::type...>;
};

template <template<__PARAMS__> class... Ts>
using PipelineStages = MakeStagesTupleImpl<
                           MakeBigState<typename Ts<int,int,0,0>::State...>,  // combine all states into one
                           sizeof...(Ts),
                           std::make_index_sequence<sizeof...(Ts)>,  // give ID to each stage
                           Ts...>;

/////////////////////////////////////////////// Pipeline primitive

using namespace cpphdl;

template <typename>
struct Pipeline;

template <template<__PARAMS__> class... Ts>
struct Pipeline<PipelineStages<Ts...>> : public Module
{
    using BIG_STATE = MakeBigState<typename Ts<int,int,0,0>::State...>;
    using STAGES = PipelineStages<Ts...>::type;

    static constexpr std::size_t LENGTH = sizeof...(Ts);
    STAGES members;

    __LAZY_COMB(states_comb, array<BIG_STATE,LENGTH>)

        byte y;
        byte x;
        byte offset;
        std::apply([&](auto&... stage) {
            for (y = 0; y < LENGTH; ++y) {
                x = 0;
                offset = 0;
                states_comb[y] = {};
                (
                    (
                        [&]{
                            using State = typename std::remove_reference_t<decltype(stage)>::STATE;
                            if (x <= y) {
                                *(State*)((uint8_t*)&states_comb[y] + offset) = stage.state_out()[y-x];  // assemble big state from own states for each y
                            }
                            ++x;
                            offset += sizeof(State);
                        }()
                    ),
                    ...
                );
            }
        }, members);
        return states_comb;
    }

public:
    void _connect()
    {
        std::apply([&](auto&... stage) {
            (
                (
                    [&]{
                        stage.state_in = __VAR( states_comb_func() );
                        stage._connect();
                    }()
                ),
                ...
            );
        }, members);
    }

    void _work(bool reset)
    {
        std::apply([&](auto&... stage) {
            (
                (
                    [&]{
                        stage._work(reset);
                    }()
                ),
                ...
            );
        }, members);
    }


    void _strobe()
    {
        std::apply([&](auto&... stage) {
            (
                (
                    [&]{
                        stage._strobe();
                    }()
                ),
                ...
            );
        }, members);
    }
};

