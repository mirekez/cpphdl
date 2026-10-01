#include "../Clocked.h"
#include "../Overrides.h"
#include <array>

namespace override_api {
uint32_t absent(uint32_t value);
#ifndef SYNTHESIS
uint32_t absent(uint32_t value) { return value * 7 + 11; }
#endif
inline uint32_t floating_body(uint32_t value) { return uint32_t(float(value) * 2.0f); }
inline uint32_t overloaded(uint32_t value) { return value + 3; }
inline uint64_t overloaded(uint64_t value) { return value + 5; }
}
HLS_OVERRIDE("override_api::absent")
uint32_t absent_rtl(uint32_t value) { return value * 7 + 11; }
HLS_OVERRIDE("override_api::floating_body")
uint32_t integer_rtl(uint32_t value) { return value * 2; }
HLS_OVERRIDE("override_api::overloaded")
uint32_t overload32_rtl(uint32_t value) { return value + 3; }
HLS_OVERRIDE("override_api::overloaded")
uint64_t overload64_rtl(uint64_t value) { return value + 5; }

struct OverridesMethods {
    std::array<uint32_t, 8> data;
    static constexpr bool singleClock(uint32_t) { return false; }
    struct Scope {
        uint32_t& cleaned;
        ~Scope() { ++cleaned; }
        uint32_t read() const { return cleaned; }
    };
    uint32_t forward_jump(uint32_t value) {
        uint32_t cleaned = 0;
        {
            Scope local{cleaned};
            if (value) goto done;
            cleaned += 10;
        }
    done:
        return cleaned;
    }
    uint32_t temporaries() {
        uint32_t cleaned = 0;
        uint32_t before = Scope{cleaned}.read();
        (void)Scope{cleaned};
        return before * 100 + cleaned;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { data[index] = value; return data[index]; }
        if (operation == 1) {
            uint64_t total = value + index;
            for (uint32_t i = 0; i < data.size(); ++i) total += override_api::absent(data[i]);
            return total;
        }
        if (operation == 2) { data.fill(value); return value; }
        if (operation == 4) return override_api::floating_body(value);
        if (operation == 5) return override_api::overloaded(value);
        if (operation == 6) return override_api::overloaded(uint64_t(value) << 32);
        if (operation == 7) return forward_jump(value);
        if (operation == 8) return temporaries();
        return data[index];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(4, 0, 123); test(5, 0, 17); test(6, 0, 23);
        test(7, 0, 0); test(7, 0, 1); test(8, 0, 0);
    }
#endif
};
class ClockedOverridesTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<OverridesMethods, 0, 64, 4096, true> worker;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    void _assign() {
        worker.command_valid_in = _ASSIGN(command_valid_in());
        worker.operation_in = _ASSIGN(operation_in());
        worker.index_in = _ASSIGN(index_in());
        worker.value_in = _ASSIGN(value_in());
        worker.response_ready_in = _ASSIGN(response_ready_in());
        worker._assign();
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};
#ifndef SYNTHESIS
#include "ClockedTest.h"
int main() { return clockedTest<OverridesMethods, ClockedOverridesTop>(); }
#endif
