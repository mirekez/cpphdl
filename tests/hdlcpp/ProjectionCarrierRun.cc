#include <cassert>
#include <cstdio>
#if defined(CARRIER_GRAPH)
#include "model.h"
#elif defined(CARRIER_RTL)
#include "VProjectionCarrier.h"
#else
#include "generated/ProjectionCarrier.h"
long _system_clock = 0;
#endif

int main() {
#if defined(CARRIER_GRAPH)
    cpphdl_native::Model model;
#elif defined(CARRIER_RTL)
    VProjectionCarrier model;
#else
    ProjectionCarrier<> model;
    ProjectionCarrierTypes::req_t request{};
    model.req_i_in = _ASSIGN(request);
#if defined(CARRIER_AR_BIND)
    model.req_i_in__field_ar = _ASSIGN(request.ar);
#endif
#if defined(CARRIER_VALID_BIND)
    model.req_i_in__field_ar_valid = _ASSIGN(request.ar_valid);
#endif
    model._assign();
#endif
    for (unsigned sample = 0; sample < 512; ++sample) {
#if defined(CARRIER_GRAPH)
        model.req_i[0] = sample;
        model.eval();
        const unsigned value = model.value_o[0];
#elif defined(CARRIER_RTL)
        model.req_i = sample;
        model.eval();
        const unsigned value = model.value_o;
#else
        request.ar_valid = sample >> 8;
        request.ar = sample;
        ++_system_clock;
        const unsigned value = uint64_t(model.value_o_out());
#endif
        assert(value == (sample & 255));
    }
    std::puts("aggregate projection carrier: 512 cases passed");
}
