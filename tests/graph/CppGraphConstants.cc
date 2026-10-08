#include "cpphdl.h"
#include <array>

struct ConstantLeaf {
    cpphdl::logic<3> flags;
    int8_t delta;
    cpphdl::logic<73> wide;
    std::array<cpphdl::logic<5>, 3> lanes;
    static constexpr unsigned _size_bits() { return 99; }
    static constexpr unsigned __hdlcpp_offset_flags = 96;
    static constexpr unsigned __hdlcpp_offset_delta = 88;
    static constexpr unsigned __hdlcpp_offset_wide = 15;
    static constexpr unsigned __hdlcpp_offset_lanes = 0;
    cpphdl::logic<99> pack() const {
        return cpphdl::cat{flags, cpphdl::logic<8>(uint8_t(delta)), wide, lanes[2], lanes[1], lanes[0]};
    }
};

struct ConstantConfig {
    ConstantLeaf leaf;
    cpphdl::u<7> wrapped;
    __uint128_t big;
    std::array<uint16_t, 3> table;
    cpphdl::array<2, cpphdl::logic<4>, true> packed;
    static constexpr unsigned _size_bits() { return 290; }
    static constexpr unsigned __hdlcpp_offset_leaf = 0;
    static constexpr unsigned __hdlcpp_offset_wrapped = 99;
    static constexpr unsigned __hdlcpp_offset_big = 106;
    static constexpr unsigned __hdlcpp_offset_table = 234;
    static constexpr unsigned __hdlcpp_offset_packed = 282;
    cpphdl::logic<290> pack() const {
        return cpphdl::cat{packed.data, cpphdl::logic<16>(table[2]), cpphdl::logic<16>(table[1]),
            cpphdl::logic<16>(table[0]), cpphdl::logic<64>(uint64_t(big >> 64)), cpphdl::logic<64>(uint64_t(big)),
            cpphdl::logic<7>(uint64_t(wrapped)), leaf.pack()};
    }
};

inline constexpr ConstantConfig GraphConfig = [] {
    ConstantConfig config{};
    auto assign = [](auto& target, const auto& incoming) { target = incoming; };
    assign(config.leaf.flags, cpphdl::logic<3>(5));
    config.leaf.delta = -73;
    config.leaf.wide.bytes[0] = 0x9d;
    config.leaf.wide.bytes[8] = 0xa3;
    config.leaf.wide.bytes[9] = 1;
    config.leaf.lanes = {cpphdl::logic<5>(17), cpphdl::logic<5>(6)};
    config.wrapped.value = 113;
    config.big = (__uint128_t(0x8123456789abcdefull) << 64) | 0xfedcba9876543210ull;
    config.table = {0xabcd, 0x8123};
    config.packed.data = cpphdl::logic<8>(0x93);
    return config;
}();

template<ConstantConfig Config>
class GraphConstants : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) index_in;
    _PORT(cpphdl::logic<32>) data_in;
    _PORT(cpphdl::logic<290>) object_out = _ASSIGN(GraphConfig.pack());
    _PORT(cpphdl::logic<290>) parameter_out = _ASSIGN(Config.pack());
    _PORT(cpphdl::logic<99>) immediate_out = _ASSIGN(envelope_func());
    _PORT(cpphdl::logic<32>) effect_out = _ASSIGN(effect());
    _PORT(cpphdl::logic<64>) signword_out = _ASSIGN(uint64_t(int64_t(Config.leaf.delta)));
    _PORT(cpphdl::logic<32>) result_out = _ASSIGN_REG(state);
    _PORT(cpphdl::array<3, cpphdl::logic<8>>) copied_out = _ASSIGN_REG(copied_func());
    cpphdl::reg<cpphdl::logic<32>> state;
    cpphdl::array<3, cpphdl::logic<8>> copied;
    ConstantLeaf immediate_storage;
    cpphdl::logic<99> envelope;
    cpphdl::logic<99>& envelope_func() {
        envelope = immediate().pack();
        return envelope;
    }
    ConstantLeaf& immediate() {
        immediate_storage = [&] {
            ConstantLeaf result{};
            auto assign = [](auto& target, const auto& incoming) { target = incoming; };
            assign(result.flags, cpphdl::logic<3>(6));
            assign(result.delta, int8_t(-17));
            return result;
        }();
        return immediate_storage;
    }
    uint32_t effect() {
        uint32_t scratch = uint64_t(data_in());
        auto snapshot = [scratch](uint32_t add) { return scratch + add; };
        auto update = [](auto& target, const auto& incoming) {
            if constexpr (requires { target = incoming; }) target = incoming;
            else cpphdl::sv_assign_field(target, incoming);
        };
        update(scratch, scratch ^ 0x24681357u);
        scratch ^= snapshot(13);
        scratch ^= snapshot(29);
        auto result = [&] {
            scratch ^= 0x76543210u;
            return GraphConfig.leaf;
        }();
        return scratch ^ uint64_t(result.flags);
    }
    cpphdl::array<3, cpphdl::logic<8>>& copied_func() {
        cpphdl::sv_assign_field(copied, 0);
        [&]() {
            cpphdl::array<3, cpphdl::logic<8>> source{};
            for (unsigned lane = 0; lane < 3; ++lane) source[lane] = uint64_t(data_in()) >> (lane * 8);
            for (unsigned lane = 0; lane < 3; ++lane) copied[lane] = source[lane];
        }();
        return copied;
    }
    void _work(bool reset) {
        uint32_t scratch = uint64_t(data_in());
        [&]() { scratch ^= 0x12345678u; }();
        [&]() { scratch ^= 0x87654321u; return; }();
        if (reset) state._next = 0;
        else state._next = scratch ^ uint64_t(Config.leaf.lanes[uint64_t(index_in())]) ^
            uint64_t(GraphConfig.table[uint64_t(index_in())]);
    }
    void _strobe() { state.strobe(); }
};

GraphConstants<GraphConfig> cpphdl_top;

#ifdef CPP_GRAPH_CONSTANTS_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    cpphdl::logic<2> index;
    cpphdl::logic<32> data;
    cpphdl_top.index_in = _ASSIGN(index);
    cpphdl_top.data_in = _ASSIGN(data);
    uint32_t random = 23;
    for (unsigned sample = 0; sample < 20000; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        index = sample % 3; data = random;
        model.index[0] = sample % 3; model.data[0] = random;
        model.work_reset[0] = sample % 79 == 0;
        ++_system_clock;
        cpphdl_top._work(model.work_reset[0]); cpphdl_top._strobe();
        ++_system_clock;
        model.step();
        if (uint64_t(cpphdl_top.result_out()) != model.result[0]) return 1;
        if (cpphdl_top.effect_out() != model.effect[0]) return 5;
        auto immediate = cpphdl_top.immediate_out();
        for (unsigned bit = 0; bit < 99; ++bit)
            if (((model.immediate[bit / 32] >> (bit % 32)) & 1) != uint64_t(immediate[bit])) return 6;
        auto copied = cpphdl_top.copied_out();
        for (unsigned lane = 0; lane < 3; ++lane)
            if (((model.copied[0] >> (lane * 8)) & 255) != uint64_t(copied[lane])) return 4;
        if ((uint64_t(model.signword[0]) | (uint64_t(model.signword[1]) << 32)) != uint64_t(cpphdl_top.signword_out())) return 2;
        auto packed = cpphdl_top.object_out();
        auto parameter = cpphdl_top.parameter_out();
        for (unsigned bit = 0; bit < 290; ++bit)
            if (((model.object[bit / 32] >> (bit % 32)) & 1) != uint64_t(packed[bit]) ||
                ((model.parameter[bit / 32] >> (bit % 32)) & 1) != uint64_t(parameter[bit])) return 3;
    }
    std::puts("ordinary C++ graph: 20000 constexpr aggregate/NTTP samples, all 290 bits, signed fields and array fillers match");
}
#endif
