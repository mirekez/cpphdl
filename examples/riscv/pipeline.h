#pragma once

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
//     │Stage0-2│Stage1-1│Stage2-0│   <-  STATE
//     └────────┴────────┴────────┘
//                  x

#define STAGE_PARAMS typename,size_t,size_t

template <typename... Ts>
struct MergeState : Ts... {};

/////////////////////////////////////////////// Pipeline Stages auto-enumerator - gives ID and LENGTH to all pipeline stages

template <typename STATE,
          size_t LENGTH,
          size_t ID,
          template<STAGE_PARAMS> class T>
struct StageHolder
{
    using type = T<STATE,ID,LENGTH>;
};

template <typename STATE,
          size_t LENGTH,
          typename IndexSeq,
          template<STAGE_PARAMS> class... Ts>
struct MakeStagesTupleImpl;

template <typename STATE,
          size_t LENGTH,
          size_t... I,
          template<STAGE_PARAMS> class... Ts>
struct MakeStagesTupleImpl<STATE,LENGTH,std::index_sequence<I...>,Ts...>
{
    using type = std::tuple<typename StageHolder<STATE,LENGTH,I,Ts>::type...>;
};

template <template<STAGE_PARAMS> class... Ts>
using PipelineStages = MakeStagesTupleImpl<
                           MergeState<typename Ts<void,0,0>::State...>,
                           sizeof...(Ts),
                           std::make_index_sequence<sizeof...(Ts)>,
                           Ts...>;

/////////////////////////////////////////////// Pipeline primitive

template <typename>
struct Pipeline;

template <template<STAGE_PARAMS> class... Ts>
struct Pipeline<PipelineStages<Ts...>>
{
    using STATE = MergeState<typename Ts<void,0,0>::State...>;
    using STAGES = PipelineStages<Ts...>;

    static constexpr std::size_t LENGTH = sizeof...(Ts);
    STAGES::type members;
    cpphdl::array<STATE,LENGTH> state_comb;

public:
    void connect()
    {
        std::apply([&](auto&... stage) {
            (
                (
                    [&]{
                        stage.state_in = &state_comb;
                        stage.connect();
                    }()
                ),
                ...
            );
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
        std::apply([&](auto&... stage) {
            for (size_t y = 0; y < LENGTH; ++y) {
                size_t x = 0;
                size_t offset = 0;
                state_comb[y] = {};
                (
                    (
                        [&]{
                            using Stage = std::remove_reference_t<decltype(stage)>;
                            using State = typename Stage::State;

                            if (x <= y) {
                                *(State*)((uint8_t*)&state_comb[y] + offset) = (*stage.state_out)[y-x];
                            }
                            ++x;
                            offset += sizeof(State);
                        }()
                    ),
                    ...
                );
            }
        }, members);
    }

};

// PipelineStage primitive

class PipelineStage : public cpphdl::Module
{
    

};
