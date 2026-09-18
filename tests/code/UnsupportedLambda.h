#include <cpphdl.h>
using namespace cpphdl;

// These are valid C++, but require closure/call lowering that the RTL
// converter does not implement. Never accept them by exporting a closure
// struct or treating operator() as a zero-argument port read.
class LambdaRepro : public Module {
public:
    logic<64> result;

    void _work(bool reset) {
        uint64_t accumulator = 0;
#if defined(LAMBDA_REFERENCE)
        auto append = [&](uint64_t value, uint64_t width) {
            accumulator = (accumulator << width) | value;
        };
        append(1, 1);
        append(2, 2);
        result = accumulator;
#elif defined(LAMBDA_VALUE)
        auto append = [accumulator](uint64_t value) mutable {
            accumulator += value;
            return accumulator;
        };
        append(1);
        result = append(5);
#elif defined(LAMBDA_CAPTURELESS)
        auto identity = [](uint64_t value) { return value; };
        result = identity(6);
#elif defined(LAMBDA_IMMEDIATE)
        [&](uint64_t value) { accumulator = value; }(6);
        result = accumulator;
#elif defined(LAMBDA_EXPLICIT)
        result = [accumulator](uint64_t value) {
            return accumulator + value;
        }.operator()(6);
#elif defined(LAMBDA_REFERENCE_ALIAS)
        const auto& identity = [](uint64_t value) { return value; };
        result = identity(6);
#else
#error "Select a lambda regression case"
#endif
    }
};

#ifdef LAMBDA_NATIVE
int main() {
    LambdaRepro dut;
    dut._work(false);
    return dut.result == 6 ? 0 : 1;
}
#endif
