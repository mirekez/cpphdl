#include "PortBinding.cc"
#include <cstdio>
#ifdef PORT_BINDING_GRAPH
#include "model.h"
#endif
#ifdef PORT_BINDING_RTL
#include "VPortBinding.h"
#endif

long _system_clock = 0;
class Driver : public Module {
public:
    logic<64> data;
    int32_t signed_value;
    uint32_t unsigned_value;
    logic<8> pointer;
    PortBinding dut;
    void _assign() {
        dut.data_in = _ASSIGN(data);
        dut.source_signed_in = _ASSIGN(signed_value);
        dut.source_unsigned_in = _ASSIGN(unsigned_value);
        dut.source_pointer_in = _ASSIGN_REG(pointer);
        dut._assign();
    }
};

int main() {
    Driver driver;
    driver._assign();
#ifdef PORT_BINDING_GRAPH
    cpphdl_native::Model model;
#endif
#ifdef PORT_BINDING_RTL
    VPortBinding rtl;
#endif
    uint64_t random = 0x9123456789abcdefull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        // Even, nonzero values catch truth conversion implemented as truncation.
        const uint64_t data = sample == 0 ? 0 : sample == 1 ? 2 : sample == 2 ? ~0ull : random;
        const int32_t signed_value = sample == 0 ? 0 : sample == 1 ? -1 : int32_t(data);
        const uint32_t unsigned_value = uint32_t(data);
        const uint8_t pointer = uint8_t(data >> 32);
        driver.data = data;
        driver.signed_value = signed_value;
        driver.unsigned_value = unsigned_value;
        driver.pointer = pointer;
        ++_system_clock;
        auto& dut = driver.dut;
        const uint64_t expected[] = {uint32_t(data), 0x10000, uint64_t(int64_t(signed_value)),
            unsigned_value, data & 0x1fff, data != 0, uint64_t(int64_t(signed_value)), pointer,
            unsigned_value, data & 0x1fff, data != 0};
        const uint64_t native[] = {uint64_t(dut.address_out()), uint64_t(dut.constant_out()),
            uint64_t(dut.sign_extended_out()), uint64_t(dut.zero_extended_out()), uint64_t(dut.narrow_out()),
            dut.truth_out(), uint64_t(dut.scalar_out()), uint64_t(dut.pointer_out()), uint64_t(dut.same_width_out()),
            uint64_t(dut.direct_narrow_out()), dut.direct_truth_out()};
#ifdef PORT_BINDING_GRAPH
        model.data[0] = uint32_t(data); model.data[1] = uint32_t(data >> 32);
        model.source_signed[0] = uint32_t(signed_value);
        model.source_unsigned[0] = unsigned_value;
        model.source_pointer[0] = pointer;
        model.eval();
        const uint64_t other[] = {model.address[0], model.constant[0],
            model.sign_extended[0] | (uint64_t(model.sign_extended[1]) << 32),
            model.zero_extended[0] | (uint64_t(model.zero_extended[1]) << 32), model.narrow[0],
            model.truth[0], model.scalar[0] | (uint64_t(model.scalar[1]) << 32), model.pointer[0], model.same_width[0],
            model.direct_narrow[0], model.direct_truth[0]};
#elif defined(PORT_BINDING_RTL)
        rtl.data_in = data; rtl.source_signed_in = signed_value;
        rtl.source_unsigned_in = unsigned_value; rtl.source_pointer_in = pointer;
        rtl.eval();
        const uint64_t other[] = {rtl.address_out, rtl.constant_out, rtl.sign_extended_out, rtl.zero_extended_out,
            rtl.narrow_out, rtl.truth_out, rtl.scalar_out, rtl.pointer_out, rtl.same_width_out,
            rtl.direct_narrow_out, rtl.direct_truth_out};
#else
        const auto& other = native;
#endif
        for (unsigned port = 0; port < sizeof(expected) / sizeof(expected[0]); ++port) {
            if (native[port] != expected[port] || other[port] != expected[port]) {
                std::fprintf(stderr, "sample %u port %u: expected %llx C++ %llx other %llx\n", sample,
                    port, (unsigned long long)expected[port], (unsigned long long)native[port],
                    (unsigned long long)other[port]);
                return 1;
            }
        }
    }
    std::puts("4096 bound-port conversion samples passed");
}
