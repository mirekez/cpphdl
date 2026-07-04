#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"

using namespace cpphdl;

long _system_clock = 0;

struct CombSource {
    logic<1> value;
    int calls = 0;

    logic<1>& comb_func()
    {
        ++calls;
        value = logic<1>(calls & 1);
        return value;
    }
};

int main()
{
    CombSource source;
    _PORT(logic<1>) comb_port;
    comb_port = _ASSIGN_COMB(source.comb_func());

    (void)comb_port();
    (void)comb_port();
    if (source.calls != 2) {
        return 1;
    }

    source.calls = 0;
    _PORT(logic<1>) comb_value_port;
    comb_value_port = _ASSIGN_COMB(logic<1>(source.comb_func()));

    (void)comb_value_port();
    (void)comb_value_port();
    if (source.calls != 2) {
        return 4;
    }

    source.calls = 0;
    _PORT(logic<1>) reg_port;
    reg_port = _ASSIGN_REG(source.comb_func());

    (void)reg_port();
    (void)reg_port();
    if (source.calls != 1) {
        return 2;
    }

    ++_system_clock;
    (void)reg_port();
    if (source.calls != 2) {
        return 3;
    }

    return 0;
}
