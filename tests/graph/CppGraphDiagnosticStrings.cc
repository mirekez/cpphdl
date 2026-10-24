#include "cpphdl.h"
#include <string>

#ifdef CPP_GRAPH_STRING_ALLOCATOR
template<typename Element>
struct TraceAllocator : std::allocator<Element> {
    template<typename Other> struct rebind { using other = TraceAllocator<Other>; };
};
#endif

class DiagnosticStrings : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<8>) data_in;
    _PORT(cpphdl::logic<8>) result_out = _ASSIGN(data_in());
#ifdef CPP_GRAPH_STRING_ALLOCATOR
    std::basic_string<char, std::char_traits<char>, TraceAllocator<char>> cause;
#else
    std::string cause;
#endif

    void _work(bool reset) {
        if (reset) cause = "RESET";
        else if (uint64_t(data_in()) & 1) cause = "INSTRUCTION_ACCESS_FAULT";
        else cause = "LOAD_PAGE_FAULT";
#ifdef CPP_GRAPH_STRING_OBSERVABLE
        auto length = cause.size();
        (void)length;
#endif
#ifdef CPP_GRAPH_STRING_EFFECT
        cause = makeCause();
#endif
    }

#ifdef CPP_GRAPH_STRING_EFFECT
    const char* makeCause() { return "NOT_A_LITERAL_STORE"; }
#endif
};

extern DiagnosticStrings cpphdl_top;

#ifdef CPP_GRAPH_DIAGNOSTIC_STRINGS_RUN
#include "model.h"
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        model.data[0] = sample & 255;
        model.work_reset[0] = sample % 19 == 0;
        model.step();
        if (model.result[0] != (sample & 255)) return 1;
    }
    std::puts("ordinary C++ graph: 4096 discarded diagnostic-string stores passed");
}
#endif
