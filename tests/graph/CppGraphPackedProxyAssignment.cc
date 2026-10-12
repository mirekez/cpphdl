#include "cpphdl.h"

struct GraphProxyLeaf {
    cpphdl::logic<7> id;
    cpphdl::logic<66> data;
    static constexpr unsigned _size_bits() { return 73; }
    static constexpr unsigned __hdlcpp_offset_id = 66, __hdlcpp_offset_data = 0;
    cpphdl::logic<73> pack() const { return cpphdl::cat{id, data}; }
    template<size_t Width> GraphProxyLeaf& operator=(const cpphdl::logic<Width>& value) {
        data = value.bits(65, 0);
        id = value.bits(72, 66);
        return *this;
    }
};

class GraphPackedProxyAssignment : public cpphdl::Module {
public:
    using Bank = cpphdl::array<3, GraphProxyLeaf, true>;
    _PORT(cpphdl::logic<73>) data_in;
    _PORT(cpphdl::logic<2>) address_in;
    _PORT(cpphdl::logic<438>) result_out = _ASSIGN(storage.pack());
    cpphdl::array<2, Bank, true> storage;
    void _work(bool reset) {
        if (reset) storage = 0;
        else {
            storage[0][uint64_t(address_in())] = data_in();
            storage[1][2] = std::as_const(storage)[0][uint64_t(address_in())];
        }
    }
    void _strobe() {}
};

extern GraphPackedProxyAssignment cpphdl_top;

#ifdef CPP_GRAPH_PACKED_PROXY_ASSIGNMENT_RUN
#include "model.h"
#include <array>
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    std::array<std::array<uint32_t, 3>, 6> expected{};
    for (unsigned sample = 0; sample < 2000; ++sample) {
        const unsigned address = sample % 3;
        model.address[0] = address;
        model.data = {sample * 2654435761u, sample * 1234567u, sample & 511u};
        model.work_reset[0] = sample % 31 == 0;
        if (model.work_reset[0]) expected = {};
        else { expected[address] = model.data; expected[5] = model.data; }
        model.step();
        for (unsigned lane = 0; lane < 6; ++lane)
            for (unsigned bit = 0; bit < 73; ++bit) {
                const unsigned position = lane * 73 + bit;
                if (((model.result[position / 32] >> (position % 32)) & 1u) !=
                    ((expected[lane][bit / 32] >> (bit % 32)) & 1u)) return 1;
            }
    }
    std::puts("ordinary C++ graph: 2000 nested packed-struct proxy writes and 73-bit proxy copies match every bit");
}
#endif
