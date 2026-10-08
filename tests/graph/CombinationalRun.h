#include <cstdio>
#ifdef TEST_GRAPH
#include "model.h"
#endif

long _system_clock = 0;

class Driver : public cpphdl::Module {
public:
    cpphdl::logic<128> data;
    uint32_t selector;
    TestModule dut;
    void _assign() {
        dut.data_in = _ASSIGN(data);
        dut.selector_in = _ASSIGN(selector);
        dut._assign();
    }
};

int main() {
    Driver driver;
    driver._assign();
#ifdef TEST_GRAPH
    cpphdl_native::Model model;
#elif defined(TEST_RTL)
    RtlModule model;
#endif
    uint64_t random = 0x921baad012345678ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        uint32_t words[4];
        for (unsigned lane = 0; lane < 4; ++lane) {
            random ^= random << 13; random ^= random >> 7; random ^= random << 17;
            words[lane] = sample == 0 ? 0 : sample == 1 ? ~0u : uint32_t(random);
            driver.data.bits(lane * 32 + 31, lane * 32) = words[lane];
        }
        driver.selector = sample % 65;
        ++_system_clock;
        const uint64_t expected = oracle(words, driver.selector);
        const uint64_t native = uint64_t(driver.dut.result_out());
        uint64_t other = native;
#ifdef TEST_GRAPH
        for (unsigned lane = 0; lane < 4; ++lane) model.data[lane] = words[lane];
        model.selector[0] = driver.selector;
        model.eval();
        other = model.result[0] | (uint64_t(model.result[1]) << 32);
#elif defined(TEST_RTL)
        for (unsigned lane = 0; lane < 4; ++lane) model.data_in[lane] = words[lane];
        model.selector_in = driver.selector;
        model.eval();
        other = model.result_out;
#endif
        if (native != expected || other != expected) {
            std::fprintf(stderr, "sample %u selector %u: expected %llx native %llx other %llx\n",
                         sample, driver.selector, (unsigned long long)expected,
                         (unsigned long long)native, (unsigned long long)other);
            return 1;
        }
    }
    std::puts("4096 samples matched independent expectations");
}
