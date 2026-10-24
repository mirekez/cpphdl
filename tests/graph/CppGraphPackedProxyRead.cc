#include "cpphdl.h"

template<typename Proxy>
cpphdl::logic<32> materializePackedLane(Proxy source) {
    return source;
}

template<typename Proxy>
void writePackedLane(Proxy destination, cpphdl::logic<32> value) {
    destination = value;
}

class GraphPackedProxyRead : public cpphdl::Module {
public:
    using Bank = cpphdl::array<2, cpphdl::logic<32>, true>;
    _PORT(cpphdl::logic<64>) data_in;
    _PORT(cpphdl::logic<1>) select_in;
    _PORT(cpphdl::logic<32>) result_out = _ASSIGN_COMB(selected_comb_func());
    _LAZY_COMB(bank_comb, Bank)
        bank_comb = Bank(data_in());
        return bank_comb;
    }
    _LAZY_COMB(selected_comb, cpphdl::logic<32>)
#ifdef PROXY_ALIAS_TEST
        Bank local(data_in());
        auto lane = local[uint64_t(select_in())];
        auto original = cpphdl::logic<32>(lane);
        writePackedLane(local[uint64_t(select_in())], original ^ cpphdl::logic<32>(0xa5a55a5a));
        selected_comb = cpphdl::logic<32>(local[uint64_t(select_in())]);
#else
        switch (uint64_t(select_in())) {
        case 0: selected_comb = materializePackedLane(bank_comb_func()[0]); break;
        default: selected_comb = materializePackedLane(bank_comb_func()[1]); break;
        }
#endif
        return selected_comb;
    }
    void _assign() {}
    void _work(bool) {}
    void _strobe() {}
};

extern GraphPackedProxyRead cpphdl_top;

#if defined(PACKED_PROXY_GRAPH) || defined(PACKED_PROXY_ORDINARY)
#include <cassert>
#include <cstdio>
#ifdef PACKED_PROXY_GRAPH
#include "model.h"
#else
long _system_clock = 0;
#endif
int main() {
#ifdef PACKED_PROXY_GRAPH
    cpphdl_native::Model model;
#else
    GraphPackedProxyRead model;
    cpphdl::logic<64> data;
    cpphdl::logic<1> select;
    model.data_in = _ASSIGN(data);
    model.select_in = _ASSIGN(select);
#endif
    uint64_t random = 0x43bd2714a53e7919ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const unsigned lane = sample & 1;
#ifdef PACKED_PROXY_GRAPH
        model.data = {uint32_t(random), uint32_t(random >> 32)};
        model.select[0] = lane;
        model.eval();
        const unsigned actual = model.result[0];
#else
        data = random;
        select = lane;
        ++_system_clock;
        const unsigned actual = uint64_t(model.result_out());
#endif
        const unsigned expected = uint32_t(random >> (lane * 32));
#ifdef PROXY_ALIAS_TEST
        assert(actual == (expected ^ 0xa5a55a5a));
#else
        assert(actual == expected);
#endif
    }
    std::puts("packed proxy scalar reads: 4096 cases passed");
}
#endif
