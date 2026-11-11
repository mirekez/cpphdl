// Check both shallow/deep replacements through hdlcpp's native graph.
#include "model.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

int main() {
    cpphdl_native::Model g;
    constexpr unsigned depths[] = {4, 64, 65};
    std::array<std::array<std::array<uint32_t, 3>, 65>, 6> memory{};
    std::array<std::array<uint32_t, 3>, 6> expected{};
    uint32_t rng=0x549b7612;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    for (unsigned cycle=0; cycle<8192; ++cycle) {
        bool initialize=cycle<65;
        bool reset=!initialize && cycle%127<3;
        bool read=!initialize && (next()&1), write=initialize || (next()&1);
        unsigned wa=initialize ? cycle : next()%65;
        unsigned ra=cycle%3==0 ? wa : next()%65;
        unsigned mask=initialize ? 7 : next()%8;
        g.clk[0]=0; g.reset[0]=reset; g.read_in[0]=read; g.write_in[0]=write;
        g.read_address_in[0]=ra; g.write_address_in[0]=wa; g.write_mask_in[0]=mask;
        for (unsigned lane=0; lane<3; ++lane) g.data_in[lane]=next();
        g.step();
        if (!reset) for (unsigned bank=0; bank<6; ++bank) {
            unsigned depth=depths[bank/2];
            // Old data on collisions, including partially masked writes.
            if (read) expected[bank]=memory[bank][ra%depth];
            if (write) for (unsigned lane=0; lane<3; ++lane)
                if (bank%2==0 || (mask&(1<<lane)))
                    memory[bank][wa%depth][lane]=g.data_in[lane];
        }
        g.clk[0]=1; g.step(); g.step(); g.clk[0]=0; g.step();
        if (!initialize) for (unsigned bank=0; bank<6; ++bank)
            for (unsigned lane=0; lane<3; ++lane)
                if (g.result_out[bank*3+lane]!=expected[bank][lane])
                    throw std::runtime_error("RAM storage policy changed read/reset/mask semantics");
    }
    std::puts("PASS RAM storage: depths 4/64/65, plain/masked, read-first collisions, reset retention, holds, duplicate edges");
}
