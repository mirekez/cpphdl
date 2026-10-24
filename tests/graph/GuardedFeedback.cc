#include "cpphdl.h"

class GuardedFeedback : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) state_in;
    _PORT(cpphdl::logic<8>) data_in;
    _PORT(cpphdl::logic<8>) first_out = _ASSIGN(first());
    _PORT(cpphdl::logic<8>) second_out = _ASSIGN(second());
    cpphdl::logic<8> first_value, second_value;

    cpphdl::logic<8>& first() {
        first_value = uint64_t(state_in()) == 0 ? data_in() ^ second() : cpphdl::logic<8>(5);
        return first_value;
    }
    cpphdl::logic<8>& second() {
#ifdef GRAPH_TRUE_FEEDBACK
        second_value = uint64_t(state_in()) == 0 ? data_in() ^ first() : cpphdl::logic<8>(7);
#else
        second_value = uint64_t(state_in()) == 1 ? data_in() ^ first() : cpphdl::logic<8>(7);
#endif
        return second_value;
    }
};

class GuardedSelector : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) state_in;
    _PORT(cpphdl::logic<8>) first_out = _ASSIGN(first());
    _PORT(cpphdl::logic<8>) second_out = _ASSIGN(second());
    cpphdl::logic<8> first_value, second_value;
    cpphdl::logic<8>& first() {
        first_value = uint64_t(state_in()) == 0 ? second() : cpphdl::logic<8>(0);
        return first_value;
    }
    cpphdl::logic<8>& second() {
        if (uint64_t(first())) second_value = uint64_t(state_in()) == 0 ? 7 : 9;
        else second_value = uint64_t(state_in()) == 0 ? 7 : 11;
        return second_value;
    }
};

class GuardedCoupled : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) state_in, other_in;
    _PORT(cpphdl::logic<8>) first_out = _ASSIGN(first());
    _PORT(cpphdl::logic<8>) second_out = _ASSIGN(second());
    _PORT(cpphdl::logic<8>) third_out = _ASSIGN(third());
    cpphdl::logic<8> first_value, second_value, third_value;
    cpphdl::logic<8>& first() {
        first_value = uint64_t(state_in()) == 0 ? second() :
            (uint64_t(other_in()) == 0 ? third() : cpphdl::logic<8>(5));
        return first_value;
    }
    cpphdl::logic<8>& second() {
        second_value = uint64_t(state_in()) == 1 ? first() : cpphdl::logic<8>(7);
        return second_value;
    }
    cpphdl::logic<8>& third() {
        third_value = uint64_t(other_in()) == 1 ? first() : cpphdl::logic<8>(9);
        return third_value;
    }
};

#ifdef GRAPH_GUARDED_COUPLED
extern GuardedCoupled cpphdl_top;
#elif defined(GRAPH_GUARDED_SELECTOR)
extern GuardedSelector cpphdl_top;
#else
extern GuardedFeedback cpphdl_top;
#endif

#ifdef GRAPH_GUARDED_FEEDBACK_RUN
#ifdef GRAPH_GUARDED_FEEDBACK_VERILATOR
#ifdef GRAPH_GUARDED_COUPLED
#include "VGuardedCoupled.h"
#elif defined(GRAPH_GUARDED_SELECTOR)
#include "VGuardedSelector.h"
#else
#include "VGuardedFeedback.h"
#endif
#else
#include "model.h"
#endif
#include <cstdio>
int main() {
#ifdef GRAPH_GUARDED_FEEDBACK_VERILATOR
#ifdef GRAPH_GUARDED_COUPLED
    VGuardedCoupled model;
#elif defined(GRAPH_GUARDED_SELECTOR)
    VGuardedSelector model;
#else
    VGuardedFeedback model;
#endif
#else
    cpphdl_native::Model model;
#endif
    for (unsigned state = 0; state < 4; ++state)
        for (unsigned data = 0; data < 256; ++data) {
#ifdef GRAPH_GUARDED_FEEDBACK_VERILATOR
            model.state_i = state;
#ifdef GRAPH_GUARDED_COUPLED
            model.other_i = data & 3;
#elif !defined(GRAPH_GUARDED_SELECTOR)
            model.data_i = data;
#endif
            model.eval();
            auto first = model.first_o, second = model.second_o;
#elif defined(GRAPH_GUARDED_FEEDBACK_SV)
            model.state_i[0] = state;
#ifdef GRAPH_GUARDED_COUPLED
            model.other_i[0] = data & 3;
#elif !defined(GRAPH_GUARDED_SELECTOR)
            model.data_i[0] = data;
#endif
            model.eval();
            auto first = model.first_o[0], second = model.second_o[0];
#else
            model.state[0] = state;
#ifdef GRAPH_GUARDED_COUPLED
            model.other[0] = data & 3;
#elif !defined(GRAPH_GUARDED_SELECTOR)
            model.data[0] = data;
#endif
            model.eval();
            auto first = model.first[0], second = model.second[0];
#endif
#ifdef GRAPH_GUARDED_COUPLED
            const auto expected = state == 0 ? 7 : (data & 3) == 0 ? 9 : 5;
            if (first != unsigned(expected)) return 1;
            if (second != unsigned(state == 1 ? expected : 7)) return 2;
#ifdef GRAPH_GUARDED_FEEDBACK_VERILATOR
            auto third = model.third_o;
#elif defined(GRAPH_GUARDED_FEEDBACK_SV)
            auto third = model.third_o[0];
#else
            auto third = model.third[0];
#endif
            if (third != unsigned((data & 3) == 1 ? expected : 9)) return 3;
#elif defined(GRAPH_GUARDED_SELECTOR)
            if (first != (state == 0 ? 7 : 0)) return 1;
            if (second != (state == 0 ? 7 : 11)) return 2;
#else
            if (first != (state == 0 ? data ^ 7 : 5)) return 1;
            if (second != (state == 1 ? data ^ 5 : 7)) return 2;
#endif
        }
    std::puts("guarded feedback: all 1024 input combinations passed");
}
#endif
